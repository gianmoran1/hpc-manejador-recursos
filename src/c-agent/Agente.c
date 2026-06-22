#define _GNU_SOURCE
#include <time.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h> /* For O_* constants */
#include <sys/stat.h> /* For mode constants */
#include <sys/mman.h> /* mmap */
#include <sys/wait.h> /* wait */
#include <unistd.h> /* ftruncate */
#include <pthread.h>
#include <sys/epoll.h>
#include <signal.h>
#include <arpa/inet.h>
#include <errno.h>
#include "Sockets.h"
#include "gestor_estado.h"
#include "cliente.h"
#include "controlador.h"

/*
 * Para probar, nos conectamos con: 
 * Simulando el cliente de erlang por nc localhost <puerto>
 *  nc 127.0.0.1 4040
 * Simulando otro nodo C por nc <ip_local> <puerto>
 *  nc 172.23.98.120 4040
 * Simulando un ANNOUNCE por echo "ANNOUNCE 10.0.0.5 4040 gpu:1" | nc -u -w1 127.0.0.1 12529
  */




#define CAP_CPU 4
#define CAP_GPU 1
#define CAP_MEM 8192

int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int erlangSocket = -1;
int timer_anuncios_fd;
char mi_ip_publica[16];
EstadoGlobal estado = NULL;




void agregar_fd_en_epoll(int fd, int flags) {
    struct epoll_event ev;
    ev.events = flags;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }
}

void agregar_cliente_en_epoll(ClienteConectado *cliente, int flags) {
    struct epoll_event ev;
    ev.events = flags;  
    ev.data.ptr = cliente; 
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cliente->fd, &ev) == -1) {
        perror("Error al agregar cliente al epoll");
        close(cliente->fd);
        exit(EXIT_FAILURE);
    }
    return;
}

void modificar_cliente_en_epoll (ClienteConectado *cliente, int flags) {
    struct epoll_event ev;
    ev.events = flags;  
    ev.data.ptr = cliente; 
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cliente->fd, &ev) == -1) {
        perror("Error al agregar cliente al epoll");
        close(cliente->fd);
        exit(EXIT_FAILURE);
    }
    return;

}


void aceptar_cliente(int fd_listo) {
    
    // Hay un nuevo cliente queriendo entrar. ¡Vamos a abrirle la puerta!
    struct sockaddr_in cliente_addr;
    socklen_t cliente_len = sizeof(cliente_addr);
    
    // acepto la coneccion en el socket
    int conn_sock = accept(fd_listo, (struct sockaddr *)&cliente_addr, &cliente_len);

    if (conn_sock == -1) {
        // Si hubo un error falso por ser no-bloqueante, lo ignoramos
        return; 
    }
    
    // Convierto el nuevo socket a no bloqueante
    set_nonblocking(conn_sock);

    ClienteConectado *clienteNuevo = crear_cliente_conectado(conn_sock, (fd_listo == lsock_local) ? 1 : 0);
    agregar_cliente_en_epoll(clienteNuevo, EPOLLIN | EPOLLONESHOT);
    
    if (fd_listo == lsock_local) { erlangSocket = conn_sock; 
        printf("¡Nueva conexión local aceptada para Erlang! (FD asignado: %d)\n", conn_sock);
    } else 
    printf("¡Nueva conexión de red aceptada! (FD asignado: %d)\n", conn_sock);
}

// Función auxiliar para extraer mensajes completos y limpiar el buffer
void procesar_mensajes_en_buffer(ClienteConectado *cliente) {
    char *salto_linea;
    
    // Mientras encontremos un '\n' en el buffer acumulado...
    while ((salto_linea = strchr(cliente->buffer, '\n')) != NULL) {
        
        // Calculamos la longitud y copiamos el mensaje limpio
        int longitud_mensaje = salto_linea - cliente->buffer;
        char mensaje_limpio[1024];
        strncpy(mensaje_limpio, cliente->buffer, longitud_mensaje);
        mensaje_limpio[longitud_mensaje] = '\0'; 

        // Lo mandamos a la lógica de negocio (el parseo)
        if (cliente->es_erlang) {
            procesar_mensaje_erlang(cliente, mensaje_limpio);
        } else {
            procesar_mensaje_red_c(cliente, mensaje_limpio);
        }

        // Limpiamos el buffer moviendo lo que sobró al principio
        int bytes_restantes = cliente->bytes_leidos - (longitud_mensaje + 1); 
        memmove(cliente->buffer, salto_linea + 1, bytes_restantes);
        cliente->bytes_leidos = bytes_restantes;
        
        // Aseguramos que el nuevo final tenga su fin de cadena por seguridad
        cliente->buffer[cliente->bytes_leidos] = '\0';
    }
}

// Callback de timeout: cuando un RESERVE lleva más de 30s encolado sin resolverse,
// le enviamos DENIED al coordinador que hizo la reserva para que haga rollback.
// El coordinador al recibir DENIED libera lo ya grantado en otros nodos y avisa JOB_DENIED a Erlang.
static void notificar_denied_timeout(int job_id, int socket_fd) {
    char msj[64];
    snprintf(msj, sizeof(msj), "DENIED %d\n", job_id);
    enviar_mensaje_tcp(socket_fd, msj);
}

void* bucle_principal(void* args) {
    (void)args;
    struct epoll_event eventos[MAX_EVENTS];

    for(;;) {
        // El hilo se duerme acá hasta que el epoll le avise de algo
        int nfds = epoll_wait(epoll_fd, eventos, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {

            ClienteConectado *entidad = (ClienteConectado *) eventos[n].data.ptr;
            int fd_listo = entidad->fd;

            // si el evento esta en un socket de escucha, es porque hay una nueva conexión queriendo entrar. 
            // Hay que aceptarla y agregarla al epoll para escuchar lo que mande ese nuevo cliente
            if ((fd_listo == lsock_publico) || (fd_listo == lsock_local)) {
                
                // Hay un nuevo Agente C queriendo conectarse
                aceptar_cliente(fd_listo);

            } 

            // si es un mensaje UDP, es porque algún nodo de la red se está anunciando con un ANNOUNCE.
            else if (fd_listo == usock_udp) {

                char buffer_udp[1024];
                int valido = atender_cliente_udp(usock_udp, buffer_udp, sizeof(buffer_udp));
                if (!valido) continue;

                printf("¡ANNOUNCE UDP recibido: %s\n", buffer_udp);

                // Formato: "IP_REMITENTE ANNOUNCE IP_NODE PUERTO cpu:X [gpu:Y] [mem:Z]"
                char ip_rem[50], cmd[32], ip_node[50];
                int puerto_node = 0, cpu = 0, gpu = 0, mem = 0;
                int n = sscanf(buffer_udp, "%49s %31s %49s %d",
                               ip_rem, cmd, ip_node, &puerto_node);
                if (n == 4 && strcmp(cmd, "ANNOUNCE") == 0) {
                    // Avanzar el cursor hasta los tokens de recursos
                    const char *ptr = buffer_udp;
                    for (int i = 0; i < 4; i++) {
                        while (*ptr && *ptr != ' ') ptr++;
                        while (*ptr == ' ') ptr++;
                    }
                    char tok[64];
                    while (sscanf(ptr, "%63s", tok) == 1) {
                        char res[16]; int val;
                        if (sscanf(tok, "%15[^:]:%d", res, &val) == 2) {
                            if      (strcmp(res, "cpu") == 0) cpu = val;
                            else if (strcmp(res, "gpu") == 0) gpu = val;
                            else if (strcmp(res, "mem") == 0) mem = val;
                        }
                        while (*ptr && *ptr != ' ') ptr++;
                        while (*ptr == ' ') ptr++;
                    }
                    gestor_procesar_anuncio(estado, ip_node, puerto_node, cpu, gpu, mem);
                }
            } 

             
            else if (fd_listo == timer_anuncios_fd) {

                uint64_t expiraciones;
                read(timer_anuncios_fd, &expiraciones, sizeof(expiraciones));

                // Expirar peticiones de RESERVE que llevan más de 30s esperando (anti-deadlock)
                gestor_expirar_pedidos(estado, notificar_denied_timeout);

                // Desconectar nodos que no enviaron ANNOUNCE en los últimos 15s
                gestor_desconectar_nodos(estado);

                // Anunciar los recursos disponibles actuales (no totales hardcodeados)
                char msj[256];
                pthread_mutex_lock(&estado->lock);
                int disp_cpu = estado->cpu->disponible;
                int disp_gpu = estado->gpu->disponible;
                int disp_mem = estado->mem->disponible;
                pthread_mutex_unlock(&estado->lock);

                snprintf(msj, sizeof(msj), "ANNOUNCE %s %d cpu:%d gpu:%d mem:%d\n",
                         mi_ip_publica, PUERTO_TCP, disp_cpu, disp_gpu, disp_mem);
                enviar_mensaje_udp(usock_udp, "255.255.255.255", PUERTO_UDP, msj);
            }

            // Es un cliente que ya estaba conectado mandando texto (RESERVE, JOB_REQUEST, etc)
            else {

                ClienteConectado *cliente = (ClienteConectado *) eventos[n].data.ptr;

                int ret = atender_cliente_tcp(cliente);
                if (ret == 0) {
                    // El socket se rompió o desconectó.
                    if (cliente->es_erlang) {
                        printf("ALERTA: Erlang (FD %d) se desconectó.\n", cliente->fd);
                    } else {
                        printf("Nodo de red (FD %d) desconectado. Liberando sus recursos...\n", cliente->fd);
                        manejar_desconexion_socket(estado, cliente->fd, callback_granted_red);
                        // Evitar que nodo_destruir intente close+free de un ClienteConectado
                        // que ya vamos a liberar nosotros justo debajo.
                        nodo_limpiar_conexion_por_fd(cliente->fd, estado->registro_nodos);
                    }
                    close(cliente->fd);
                    free(cliente);
                }
                else {
                    procesar_mensajes_en_buffer(cliente);
                    modificar_cliente_en_epoll(cliente, EPOLLIN | EPOLLONESHOT);
                }
            }
        }
    }
    return NULL;
}



int main() {

    signal(SIGPIPE, SIG_IGN);

    estado = estado_crear(CAP_CPU, CAP_GPU, CAP_MEM);

    obtener_mi_ip_local(mi_ip_publica);
    
    printf("Arrancando... Mi IP en la red es: %s\n", mi_ip_publica);

    // Creo el epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
	    quit("epoll_create1");
		exit(EXIT_FAILURE);
	}

    // Creo los tres sockets de escucha
    lsock_publico = mk_tcp_lsock(PUERTO_TCP, mi_ip_publica); 
    lsock_local   = mk_tcp_lsock(PUERTO_TCP, "127.0.0.1"); 
    usock_udp     = mk_udp_lsock(PUERTO_UDP);
    timer_anuncios_fd = mk_timer(5); // Creamos el timer cada 5 segundos
    
    // Registro los sockets de entrada y el timer en el epoll
    agregar_cliente_en_epoll(crear_cliente_conectado(lsock_publico, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(lsock_local, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(usock_udp, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(timer_anuncios_fd, 0), EPOLLIN | EPOLLEXCLUSIVE);
    
    

    printf("Servidor HPC iniciado. Escuchando en puerto %d...\n", PUERTO_TCP);

    // // Inicio los 4 hilos trabajadores
    pthread_t hilos[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&hilos[i], NULL, bucle_principal, NULL);
    }

    // Espero a que terminen (bucle infinito)
    for (int i = 0; i < 4; i++) {
        pthread_join(hilos[i], NULL);
    }

    return 0;
}





































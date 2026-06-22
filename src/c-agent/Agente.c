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




int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int erlangSocket = -1;
int timer_anuncios_fd;
char mi_ip_publica[16]; // Para guardar mi IP pública y usarla en los anuncios
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

// SI HACEMOS 4 HILOS, ESTA (CON OTRO NOMBRE) SERA LA FUNCION LLAMADA EN PTHREAD_CREATE PARA QUE CADA HILO EJECUTE EL BUCLE PRINCIPAL DE ATENCION DE EVENTOS DEL EPOLL.
// Función que ejecutarán los 4 hilos trabajadores para atender eventos del epoll
// bucle principal donde me quedo esperando eventos en el epoll y los atiendo según el tipo de evento que sea (nueva conexión TCP, mensaje UDP, mensaje en socket ya conectado, etc)
void* bucle_principal(void* args) {
    struct epoll_event eventos[MAX_EVENTS];

    for(;;) {
        // El hilo se duerme acá hasta que el epoll le avise de algo
        int nfds = epoll_wait(epoll_fd, eventos, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {
            int fd_listo = eventos[n].data.fd;  

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
                // valido = parsearlo
                if (!valido) {
                    // No había nada útil, seguimos esperando
                    continue;
                }
                printf("¡ANNOUNCE UDP recibido Mensaje: %s\n", buffer_udp);

            // santos_actualizar_tabla_nodos(ip_remitente, recuros, buffer_udp);
            // ENVIO MENSAJE A SANTOS PARA QUE ACTUALICE SU TABLA DE NODOS CON ESTE NUEVO NODO QUE SE ANUNCIA
                
            } 

             
            else if (fd_listo == timer_anuncios_fd) {

                // Sonó el reloj, lo vacio para que no siga sonando en loop
                uint64_t expiraciones;
                read(timer_anuncios_fd, &expiraciones, sizeof(expiraciones));

                // Armamos el mensaje a gritar usando la IP pública real
                // OJO: En la versión final, los recursos los sacás de variables dinámicas
                char msj[256];
                // msj = pedir_recursos_disponibles(); // ACA PEDIS LOS RECURSOS DISPONIBLES ACTUALES PARA ANUNCIARLOS EN EL MENSAJE
                sprintf(msj, "ANNOUNCE %s %d cpu:4 mem:8192\n", mi_ip_publica, PUERTO_TCP);
                
                enviar_mensaje_udp(usock_udp, "255.255.255.255", PUERTO_UDP, msj);             
            }

            // Es un cliente que ya estaba conectado mandando texto (RESERVE, JOB_REQUEST, etc)
            else {
                
                ClienteConectado *cliente = (ClienteConectado *) eventos[n].data.ptr;

                int estado = atender_cliente_tcp(cliente);
                if (estado == 0) {
                    // El socket se rompió o desconectó. Hacemos la limpieza acá.
                    if (cliente->es_erlang) {
                        printf("ALERTA: Erlang (FD %d) se desconectó.\n", cliente->fd);
                    } else {
                        printf("Nodo de red (FD %d) desconectado.\n", cliente->fd);
                        // ACÁ ES DONDE LLAMÁS A SANTOS
                        // santos_eliminar_nodo(cliente->fd);
                    }
                    
                    // Destruimos la ficha
                    close(cliente->fd);
                    free(cliente);
                }
                else {
                // Procesamos los mensajes completos que tengamos en el buffer 
                // Si llegamos acá, es porque ya leímos todo lo que había en la red
                procesar_mensajes_en_buffer(cliente);

                // Rearmo el epoll, vuelvo a vigilar el socket
                modificar_cliente_en_epoll(cliente, EPOLLIN | EPOLLONESHOT);
                }
            }
        }
    }
    return NULL;
}



int main() {

    signal(SIGPIPE, SIG_IGN);

	
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
    agregar_fd_en_epoll(lsock_publico, EPOLLIN | EPOLLEXCLUSIVE);
    agregar_fd_en_epoll(lsock_local, EPOLLIN | EPOLLEXCLUSIVE);
    agregar_fd_en_epoll(usock_udp, EPOLLIN | EPOLLEXCLUSIVE);
    agregar_fd_en_epoll(timer_anuncios_fd, EPOLLIN | EPOLLEXCLUSIVE);

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





































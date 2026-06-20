#define _GNU_SOURCE
#include <time.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
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
#include "Sockets.h"
#include "gestor_estado.h"
#include <errno.h>



/*
 * Para probar, nos conectamos con: 
 * Simulando el cliente de erlang por nc localhost <puerto>
 * Simulando otro nodo C por nc <ip_local> <puerto>
 * Simulando un ANNOUNCE por echo "ANNOUNCE 192.168.




  */








int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int erlangSocket = -1;
int timer_anuncios_fd;
char mi_ip_publica[16]; // Para guardar mi IP pública y usarla en los anuncios

typedef struct {
    int fd;                 // El socket del cliente
    int es_erlang;          // 1 si es el socket local de Erlang, 0 si es un socket de red
    char buffer[1024];      // El buffer donde acumulamos los pedazos de texto
    int bytes_leidos;       // Cuántos bytes llevamos acumulados
} ClienteConectado;


// --- MOCKS TEMPORALES PARA PROBAR EL FLUJO ---
void parsear_mensaje_erlang(int fd, char* msg) {
    // Imprime en Verde
    printf("\033[0;32m   [MOCK ERLANG] Mensaje completo extraído: '%s'\033[0m\n", msg);
}

void parsear_mensaje_red_c(int fd, char* msg) {
    // Imprime en Cian
    printf("\033[0;36m   [MOCK RED C] Mensaje completo extraído: '%s'\033[0m\n", msg);
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

    ClienteConectado *nuevo_cliente = malloc(sizeof(ClienteConectado));
    nuevo_cliente->fd = conn_sock;
    nuevo_cliente->es_erlang = (fd_listo == lsock_local) ? 1 : 0;
    nuevo_cliente->bytes_leidos = 0;
    memset(nuevo_cliente->buffer, 0, sizeof(nuevo_cliente->buffer));

    // Lo agregamos al epoll PARA LEER DATOS.
    struct epoll_event ev_cliente;
    ev_cliente.events = EPOLLIN | EPOLLONESHOT;
    ev_cliente.data.ptr = nuevo_cliente; // <--- EL TRUCO DE LA PROFE
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &ev_cliente) == -1) {
        perror("Error al agregar cliente al epoll");
        close(conn_sock);
        exit(EXIT_FAILURE);
    } else {
        if (fd_listo == lsock_local) { erlangSocket = conn_sock; 
            printf("¡Nueva conexión local aceptada para Erlang! (FD asignado: %d)\n", conn_sock);
        } else 
        printf("¡Nueva conexión de red aceptada! (FD asignado: %d)\n", conn_sock);
    }
    
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
            parsear_mensaje_erlang(cliente->fd, mensaje_limpio);
        } else {
            parsear_mensaje_red_c(cliente->fd, mensaje_limpio);
        }

        // Limpiamos el buffer moviendo lo que sobró al principio
        int bytes_restantes = cliente->bytes_leidos - (longitud_mensaje + 1); 
        memmove(cliente->buffer, salto_linea + 1, bytes_restantes);
        cliente->bytes_leidos = bytes_restantes;
        
        // Aseguramos que el nuevo final tenga su fin de cadena por seguridad
        cliente->buffer[cliente->bytes_leidos] = '\0';
    }
}


void atender_cliente_tcp(ClienteConectado *cliente) {
    ssize_t bytes_recibidos;

    // leo hasta vaicar lo que hay en el socket, y lo voy acumulando en el buffer de este cliente. 
    // Si el socket se queda sin datos,  salgo del bucle para procesar lo que ya tengo acumulado. 
    // Si recv me devuelve 0, es porque el cliente se desconectó, así que cierro todo y libero memoria.
    while (1) {
        // Calculamos cuánto espacio libre nos queda en la ficha de este cliente
        int espacio_disponible = sizeof(cliente->buffer) - cliente->bytes_leidos - 1;

        // PROTECCIÓN ANTI-DOS: Si se llenó el buffer y nunca mandó un '\n', es un nodo defectuoso
        if (espacio_disponible <= 0) {
            printf("[FD %d] Error: Mensaje demasiado largo sin '\\n'. Desconectando por seguridad.\n", cliente->fd);
            
            // if (!cliente->es_erlang) santos_eliminar_nodo(cliente->fd);
            close(cliente->fd);
            free(cliente);
            return;
        }

        // Leemos de la red y lo guardamos DIRECTAMENTE al final de lo que ya teníamos
        bytes_recibidos = recv(cliente->fd, cliente->buffer + cliente->bytes_leidos, espacio_disponible, 0);

        if (bytes_recibidos == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Red vacía, salimos a procesar
            } else {
                perror("Error leyendo del socket");
                return;
            }
        } 
        else if (bytes_recibidos == 0) {
            // ¡Desconexión limpia!
            if (cliente->es_erlang) {
                printf("[FD %d] ALERTA: Erlang se desconectó.\n", cliente->fd);
            } else {
                printf("[FD %d] Nodo de red desconectado.\n", cliente->fd);
                // santos_eliminar_nodo(cliente->fd);
            }
            close(cliente->fd);
            free(cliente);
            return;
        } 
        else {
            // Actualizamos el contador de bytes y cerramos el string
            cliente->bytes_leidos += bytes_recibidos;
            cliente->buffer[cliente->bytes_leidos] = '\0';
        }
    }

    // Procesamos los mensajes completos que tengamos en el buffer 
    // Si llegamos acá, es porque ya leímos todo lo que había en la red
    procesar_mensajes_en_buffer(cliente);

    // Rearmo el epoll, vuelvo a vigilar el socket
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = cliente; 
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cliente->fd, &ev) == -1) {
        perror("Error rearmando epoll_ctl_mod");
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

                // ¡Llegó un datagrama UDP! (Probablemente un ANNOUNCE)
                char buffer_udp[1024]; 
                struct sockaddr_in remitente_addr;
                socklen_t remitente_len = sizeof(remitente_addr);
                
                // Leemos el mensaje directamente del socket
                ssize_t bytes_leidos = recvfrom(usock_udp, buffer_udp, sizeof(buffer_udp) - 1, 0, 
                                               (struct sockaddr*)&remitente_addr, &remitente_len);
                
                if (bytes_leidos > 0) {


                    // Habria que chequear que el mensaje tenga sentido antes de procesarlo, 
                    // que sea un mensaje ANNOUNCE correcto. De ser asi, extraigo la ip y sus recursos anunciados y se los paso a Santos para que actualice su tabla de nodos.
                    // Bloque de prueba asumiendo que siempre llega ANNOUNCE correcto, para ver que se reciben los mensajes y de quien vienen. 
                    // Después habría que hacer el parseo real del mensaje y procesarlo acorde a lo que diga
                    
                    
                    
                    // Agregamos el fin de cadena (\0) para poder imprimirlo seguro en C
                    buffer_udp[bytes_leidos] = '\0'; 
                    
                    char ip_remitente[INET_ADDRSTRLEN];
                    // Extraemos la IP del nodo que nos gritó el ANNOUNCE para saber quién fue
                    inet_ntop(AF_INET, &(remitente_addr.sin_addr), ip_remitente, INET_ADDRSTRLEN);
                    
                    // Somos nosotros mismos. Ignoramos el mensaje y volvemos al epoll.
                    if (strcmp(ip_remitente, mi_ip_publica) == 0) continue; 
                    
                    printf("¡ANNOUNCE UDP recibido desde %s! Mensaje: %s\n", ip_remitente, buffer_udp);
                    
                    // Por ultimo aca llamo a la función de Santos:
                    // santos_actualizar_tabla_nodos(ip_remitente, recuros, buffer_udp);
                    // ENVIO MENSAJE A SANTOS PARA QUE ACTUALICE SU TABLA DE NODOS CON ESTE NUEVO NODO QUE SE ANUNCIA
                }
            } 


            else if (fd_listo == timer_anuncios_fd) {
                // Sonó el reloj, lo vacio para que no siga sonando en loop
                uint64_t expiraciones;
                read(timer_anuncios_fd, &expiraciones, sizeof(expiraciones));

                // Armamos el mensaje a gritar usando la IP pública real
                // OJO: En la versión final, los recursos los sacás de variables dinámicas
                char msj[256];
                // msj = pedir_recursos_disponibles(); // ACA PEDIS LOS RECURSOS DISPONIBLES ACTUALES PARA ANUNCIARLOS EN EL MENSAJE
                sprintf(msj, "ANNOUNCE %s %d cpu:4 mem:8192\n", mi_ip_publica, PUERTO);

                // Armo la informacion destino: 255.255.255.255 (A todos en la red local)
                struct sockaddr_in dest;
                dest.sin_family = AF_INET;
                dest.sin_port = htons(PUERTO);
                dest.sin_addr.s_addr = inet_addr("255.255.255.255"); 

                // Enviamos el broadcast
                sendto(usock_udp, msj, strlen(msj), 0, (struct sockaddr*)&dest, sizeof(dest));
                printf("[Timer] Broadcast enviado: %s", msj);
            }

            // Es un cliente que ya estaba conectado mandando texto (RESERVE, JOB_REQUEST, etc)
            else {
                
                ClienteConectado *cliente = (ClienteConectado *) eventos[n].data.ptr;

                atender_cliente_tcp(cliente);

                // if (fd_listo == erlangSocket) {
                    

                //     // me llegó un mensaje del socket local, que solo usa Erlang para hablar conmigo. Voy a ver que dice 
                //     // Ejecutar parseo_protocolo_erlang() y veo a que agente externo mandarle la peticion
                //     // Aca iria send(pedido,ip)
                    

                // } else {
                    
                    
                //     // me llegó un mensaje del socket público, que usan los otros nodos C para hablar conmigo. Voy a ver que dice
                //     // Ejecutar parseo_protocolo_red_c()
                //     // dependiendo que llego hago
                //     // pedir_recursos_a_santos() o 
                //     // send(respuesta,erlang) le respondo a erlang con el resultado del pedido al otro agente


                // }
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
    lsock_publico = mk_tcp_server(PUERTO, mi_ip_publica); // is_local = 0
    lsock_local   = mk_tcp_server(PUERTO, "127.0.0.1"); // is_local = 1
    usock_udp     = mk_udp_server(PUERTO);

    // Registro los tres sockets en el epoll
    struct epoll_event ev;

    // agrego el socket TCP de red
    ev.events = EPOLLIN;
    ev.data.fd = lsock_publico;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, lsock_publico, &ev) == -1 ){
		perror("epoll_ctl: listen_sock_TCP_red");
        exit(EXIT_FAILURE);
	}

	// agrego el socket TCP local
    ev.events = EPOLLIN;
    ev.data.fd = lsock_local;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, lsock_local, &ev) == -1){
		perror("epoll_ctl: listen_sock_TCP_local");
        exit(EXIT_FAILURE);
	}

	// agrego el socket UDP
    ev.events = EPOLLIN;
    ev.data.fd = usock_udp;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, usock_udp, &ev) == -1){
		perror("epoll_ctl: listen_sock_UDP");
        exit(EXIT_FAILURE);
	}

    timer_anuncios_fd = mk_timer(5); // Avisame cada 5 segundos

    ev.events = EPOLLIN;
    ev.data.fd = timer_anuncios_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_anuncios_fd, &ev);

    printf("Servidor HPC iniciado. Escuchando en puerto %d...\n", PUERTO);


    // Por ahora, el mismo hilo principal atiende todo.
    // Después lo podemos cambiar para que cada hilo ejecute esta función y así tener 4 hilos atendiendo eventos en paralelo.
    bucle_principal(NULL);  

    // LOGICA POR SI IMPLEMENTAMOS USAR 4 HILOS FIJOS QUE ATIENDAN TODOS LOS EVENTOS QUE PASAN

    // // Inicio los 4 hilos trabajadores
    // pthread_t hilos[4];
    // for (int i = 0; i < 4; i++) {
    //     pthread_create(&hilos[i], NULL, trabajador_epoll, NULL);
    // }

    // // Espero a que terminen (bucle infinito)
    // for (int i = 0; i < 4; i++) {
    //     pthread_join(hilos[i], NULL);
    // }

    return 0;
}





































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


int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int erlangSocket = -1;


// SI HACEMOS 4 HILOS, ESTA (CON OTRO NOMBRE) SERA LA FUNCION LLAMADA EN PTHREAD_CREATE PARA QUE CADA HILO EJECUTE EL BUCLE PRINCIPAL DE ATENCION DE EVENTOS DEL EPOLL.
// Función que ejecutarán los 4 hilos


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

    // Lo agregamos al epoll PARA LEER DATOS.
    struct epoll_event ev_cliente;
    ev_cliente.events = EPOLLIN | EPOLLONESHOT;
    ev_cliente.data.fd = conn_sock;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &ev_cliente) == -1) {
        perror("Error al agregar cliente al epoll");
        close(conn_sock);
        exit(EXIT_FAILURE);
    } else {
        if (fd_listo == lsock_local) erlangSocket = conn_sock; // Para debug, saber que este socket es el local (Erlang)
        printf("¡Nueva conexión de red aceptada! (FD asignado: %d)\n", conn_sock);
    }
                
}


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
                char buffer_udp[512]; // Tamaño de sobra para el string del TP
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
                    
                    // Extraemos la IP del nodo que nos gritó el ANNOUNCE para saber quién fue
                    char ip_remitente[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(remitente_addr.sin_addr), ip_remitente, INET_ADDRSTRLEN);
                    
                    
                    printf("¡ANNOUNCE UDP recibido desde %s! Mensaje: %s\n", ip_remitente, buffer_udp);
                    
                    // Por ultimo aca llamo a la función de Santos:
                    // santos_actualizar_tabla_nodos(ip_remitente, recuros, buffer_udp);
                    // ENVIO MENSAJE A SANTOS PARA QUE ACTUALICE SU TABLA DE NODOS CON ESTE NUEVO NODO QUE SE ANUNCIA
                }
            } 

            // Es un cliente que ya estaba conectado mandando texto (RESERVE, JOB_REQUEST, etc)
            else {
                printf("Mensaje recibido de un cliente ya conectado (FD: %d)\n", fd_listo);
                

                if (fd_listo == erlangSocket) {

                    // me llegó un mensaje del socket local, que solo usa Erlang para hablar conmigo. Voy a ver que dice 
                    // Ejecutar parseo_protocolo_erlang() y veo a que agente externo mandarle la peticion
                    // Aca iria send(pedido,ip)
                    
                } else {
                    
                    // me llegó un mensaje del socket público, que usan los otros nodos C para hablar conmigo. Voy a ver que dice
                    // Ejecutar parseo_protocolo_red_c()
                    // dependiendo que llego hago
                    // pedir_recursos_a_santos() o 
                    // send(respuesta,erlang) le respondo a erlang con el resultado del pedido al otro agente


                }
            }
        }
    }
    return NULL;
}

int main() {

    signal(SIGPIPE, SIG_IGN);

	char mi_ip_publica[16];
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





































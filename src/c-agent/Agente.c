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

#include <arpa/inet.h>

 /* Para probar, usar netcat Ej:
 *
 *      $ nc localhost <puerto>
 * 		y enviar el mensaje deseado
 */


#define MAX_EVENTS 10
#define PUERTO 9090


#define MAX_CANT 10
#define MAX_KEY 200
#define MAX_VAL 200


/*EPOLL*/

//creo la estructura epoll, y un array de la misma que es donde el kernel me va a listar los fd que tuvieron novedades.
struct epoll_event ev, eventos[MAX_EVENTS];
	


// Inicializo el candado
pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;



// A CAMBIAR, NO ME INTERESA ESTO, APARTE HAY QUE USAR ALGO EFICIENTE IE TABLA HASH XX
typedef struct Store_{
	char value[MAX_VAL];
	char key[MAX_KEY];
	int vacio;
} Store;


Store alm[MAX_CANT];
int ocupados = 0;


// ----------------------


void quit(char *s)
{
	perror(s);
	abort();
}


// // INEFICIENTE PARA LEER LINEAS PUES VOY CARACTER POR CARACTER Y ME PUEDEN CORTAR A LA MITAD SEGUN EL CHAT, PERO SI COPIO EL BUFFER Y  XX
// // LO LEO EN OTRO HILO APARTE NO SIRVE? XX
// int fd_readline(int fd, char *buf)
// {
// 	int rc;
// 	int i = 0;
	
// 	/*
// 	* Leemos de a un caracter (no muy eficiente...) hasta
// 	* completar una línea.
// 	*/
// while ((rc = read(fd, buf + i, 1)) > 0) {
// 	if (buf[i] == '\n')
// 	break;
// 	i++;
// }

// if (rc < 0)
// return rc;

// buf[i] = 0;
// return i;
// }


// // EL HANDLE_CONN ES LITERALMENTE LA FUNCION DE PARSEO QUE MANEJA TODO  XX
// //Hago todo void* para cumplir con el tipado de pthread_create
// void* handle_conn(int csock, int epoll_fd)
// {
// 	char buf[200];
//     char respuesta[200];
// 	int rc;
	
// 	// Inicializo el buffer en 0
// 	memset(buf, 0, sizeof(buf));
// 	/* Atendemos pedidos, uno por linea */
// 	rc = fd_readline(csock, buf);   
// 	// Hubo una lectura rara
// 	if (rc < 0)
// 	quit("read... raro");
// 	if (rc == 0) {
// 		/* linea vacia, se cerró la conexión */
// 		close(csock);
// 		return NULL;
// 	}
	
// 	if (strncmp(buf, "PUT ", 4) == 0) {
// 		char k[MAX_KEY], v[MAX_VAL];
// 		int ingresado = 0;
// 		// sscanf es útil aquí para separar por espacios
// 		if (sscanf(buf + 4, "%s %s", k, v) == 2) {
// 			for(int i = 0; i < MAX_CANT && !ingresado; i++){
// 				pthread_mutex_lock(&a);
// 				if ((alm[i].vacio == 0) && !(strcmp(alm[i].key,k))){
// 					strcpy(alm[i].value,v);
// 					ingresado = 1;
// 					printf("añandido\n");
// 				}
// 				pthread_mutex_unlock(&a);
// 			}
// 			for(int i = 0; i < MAX_CANT && !ingresado; i++){
// 				pthread_mutex_lock(&a);
// 				if (ocupados != MAX_CANT){
// 					if (alm[i].vacio == 1){
// 						strcpy(alm[i].key,k);
// 						strcpy(alm[i].value,v);
// 						alm[i].vacio = 0;
// 						ingresado = 1;
// 						printf("añandido\n");
// 						ocupados++;
// 					}
// 				}
// 				pthread_mutex_unlock(&a);
// 			}
// 		} else {
// 			sprintf(respuesta, "ERROR: Formato PUT incorrecto\n");
// 		}
// 	} 
// 	else if (strncmp(buf, "GET ", 4) == 0) {
// 		char k[MAX_KEY],v[MAX_VAL],reply[MAX_VAL+10];
// 		int encontrado = 0;
// 		sscanf(buf + 4, "%s", k);
// 		for(int i = 0; i < MAX_CANT && !encontrado; i++){
// 			pthread_mutex_lock(&a);
// 			if ((alm[i].vacio == 0) && !(strcmp(alm[i].key,k))){
// 				strcpy(v,alm[i].value);
// 				sprintf(reply, "%s\n", v);
// 				write(csock, reply, strlen(reply));
// 				encontrado = 1;
// 			}
// 			pthread_mutex_unlock(&a);
// 		}
// 		if (!encontrado){
// 			write(csock, "NOTFOUND\n", 10);
// 		}
		
// 	}
// 	else if (strncmp(buf, "DEL ", 4) == 0){
// 		char k[MAX_KEY],v[MAX_VAL];
// 		int encontrado = 0;
// 		sscanf(buf + 4, "%s", k);
// 		for(int i = 0; i < MAX_CANT && !encontrado; i++){
// 			pthread_mutex_lock(&a);
// 			if ((alm[i].vacio == 0) && !(strcmp(alm[i].key,k))){
// 				alm[i].vacio = 1;
// 				write(csock, "OK\n", 4);
// 				encontrado = 1;
// 				ocupados--;
// 			}
// 			pthread_mutex_unlock(&a);
// 		}
// 		if (!encontrado){
// 			write(csock, "NOTFOUND\n", 10);
// 		}
// 	}
	
	
	
// 	else {
// 		write(csock, "EINVAl\n", 8); 
// 		//close(csock);
// 	}

// 	ev.events = EPOLLIN | EPOLLONESHOT; 
// 	ev.data.fd = csock;
// 	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, csock, &ev) == -1) {
// 		perror("epoll_ctl: csock");
// 		exit(EXIT_FAILURE);
// 	}

	
	
	
// 	return NULL;
// }

// REARME DEL SOCKET EN EL EPOLL   XX
// ev.events = EPOLLIN | EPOLLONESHOT; 
// ev.data.fd = csock;
// if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, csock, &ev) == -1) {
// 	perror("epoll_ctl: csock");
// 	exit(EXIT_FAILURE);
// }


// --------------------------------------------------------------------------------------------------------------------------------------------------------------


int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;


// Le pasás un buffer vacío y te lo llena con tu IP (ej: "192.168.0.15")
void obtener_mi_ip_local(char *buffer_ip) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        strcpy(buffer_ip, "127.0.0.1"); // Fallback de seguridad
        return;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8"); // DNS de Google
    serv.sin_port = htons(53);

    // En UDP, connect() no envía nada a la red, solo asocia la dirección
    connect(sock, (const struct sockaddr*) &serv, sizeof(serv));

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    // getsockname lee la información local asignada a este socket
    getsockname(sock, (struct sockaddr*) &name, &namelen);

    // Convertimos la IP binaria a texto y la copiamos al buffer
    strcpy(buffer_ip, inet_ntoa(name.sin_addr));
    
    close(sock);
}


// CON ESTO LO QUE HAGO ES DECIRLE si te pido leer de este socket y no hay nada, no me congeles. XX
// Devolveme un error (EAGAIN o EWOULDBLOCK) de forma instantánea para que yo pueda seguir mi camino XX
// Función auxiliar para hacer que un socket sea no bloqueante
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);	// DAME LA CONFIGURACION ACTUAL DEL SOCKET XX
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);   // Agarrá la configuración actual, sumale la bandera O_NONBLOCK usando una compuerta lógica OR bit a bit (|), y guardala. XX
}

// Funcion que crea mi socket de escucha TCP, tanto local para el erlang como
// global para cualquier otro agente C
// is_local = 1 (Escucha solo a Erlang en 127.0.0.1)
// is_local = 0 (Escucha a otros Nodos C en la IP pública)
int mk_tcp_server(int port, const char* ip) {

	/* Crear socket */
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) quit("socket TCP");

    int yes = 1;
    if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0)
        quit("setsockopt TCP");

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    
    sa.sin_addr.s_addr = inet_addr(ip); 
    

    if (bind(lsock, (struct sockaddr *)&sa, sizeof sa) < 0)
        quit("bind TCP");

    if (listen(lsock, 10) < 0)
        quit("listen TCP");

    // ¡Clave para nuestra arquitectura de 1 hilo!
    set_nonblocking(lsock);

    return lsock;
}

// Funcion que crea mi socket de escucha UDP para escuchar los ANNOUNCE de toda la red
int mk_udp_server(int port) {
    int usock = socket(AF_INET, SOCK_DGRAM, 0);
    if (usock < 0) quit("socket UDP");

    int yes = 1;
    // SO_REUSEPORT permite que varios nodos en la misma PC de pruebas escuchen el mismo broadcast
    if (setsockopt(usock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0)
        quit("setsockopt UDP");

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(usock, (struct sockaddr *)&sa, sizeof sa) < 0)
        quit("bind UDP");

    set_nonblocking(usock);

    return usock;
}


// Función que ejecutarán los 4 hilos
void* trabajador_epoll(void* args) {
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

            if (fd_listo == lsock_publico) {
                // Hay un nuevo Agente C queriendo conectarse

                // Hay un nuevo cliente queriendo entrar. ¡Vamos a abrirle la puerta!
                struct sockaddr_in cliente_addr;
                socklen_t cliente_len = sizeof(cliente_addr);
                
                // acepto la coneccion en el socket
                int conn_sock = accept(fd_listo, (struct sockaddr *)&cliente_addr, &cliente_len);
                
                if (conn_sock == -1) {
                    // Si hubo un error falso por ser no-bloqueante, lo ignoramos
                    continue; 
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
                    printf("[Hilo %ld] ¡Nueva conexión de red aceptada! (FD asignado: %d)\n", pthread_self(), conn_sock);
                }
            } 
            else if (fd_listo == lsock_local) {

                // Hay un nuevo cliente queriendo entrar. ¡Vamos a abrirle la puerta!
                struct sockaddr_in cliente_addr;
                socklen_t cliente_len = sizeof(cliente_addr);
                
                // acepto la coneccion en el socket
                int conn_sock = accept(fd_listo, (struct sockaddr *)&cliente_addr, &cliente_len);
                
                if (conn_sock == -1) {
                    // Si hubo un error falso por ser no-bloqueante, lo ignoramos
                    continue; 
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
                    printf("[Hilo %ld] ¡Nueva conexión local aceptada! (FD asignado: %d)\n", pthread_self(), conn_sock);
                }
            } 
            else if (fd_listo == usock_udp) {
                // ¡Llegó un datagrama UDP! (Probablemente un ANNOUNCE)
                char buffer_udp[512]; // Tamaño de sobra para el string del TP
                struct sockaddr_in remitente_addr;
                socklen_t remitente_len = sizeof(remitente_addr);
                
                // Leemos el mensaje directamente del socket
                ssize_t bytes_leidos = recvfrom(usock_udp, buffer_udp, sizeof(buffer_udp) - 1, 0, 
                                               (struct sockaddr*)&remitente_addr, &remitente_len);
                
                if (bytes_leidos > 0) {
                    // Agregamos el fin de cadena (\0) para poder imprimirlo seguro en C
                    buffer_udp[bytes_leidos] = '\0'; 
                    
                    // Extraemos la IP del nodo que nos gritó el ANNOUNCE para saber quién fue
                    char ip_remitente[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(remitente_addr.sin_addr), ip_remitente, INET_ADDRSTRLEN);
                    
                    printf("[Hilo %ld] ¡ANNOUNCE UDP recibido desde %s! Mensaje: %s\n", 
                           pthread_self(), ip_remitente, buffer_udp);
                           
                    // Más adelante, acá llamarías a la función de Santos:
                    // santos_actualizar_tabla_nodos(ip_remitente, buffer_udp);
                }
            } 
            else {
                // Es un cliente que ya estaba conectado mandando texto (RESERVE, JOB_REQUEST, etc)
                printf("[Hilo %ld] Mensaje recibido de un cliente ya conectado (FD: %d)\n", pthread_self(), fd_listo);
                
                // Aca iría la lógica de lectura no bloqueante y el parseo

            }
        }
    }
    return NULL;
}

int main() {

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

    // Inicio los 4 hilos trabajadores
    pthread_t hilos[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&hilos[i], NULL, trabajador_epoll, NULL);
    }

    // Espero a que terminen (bucle infinito)
    for (int i = 0; i < 4; i++) {
        pthread_join(hilos[i], NULL);
    }

    return 0;
}





































// --------------------------------------------------------------------------------------------------------------------------------------------------------------

// void* manejar_cliente(void* args){
// 	int listen_sock = ((int*)args)[1];
// 	int epoll_fd = ((int*)args)[0];

// 	// bucle infinito donde manejamos las conexiones
// 	for(;;){
// 		// me quedo esperando (bloqueado) hasta que que ocurran eventos en los sockets vigilados por el epoll
// 		//EL -1 HACE QUE ESPERE SIEMPRE Y CUANDO PASA ALGO EL EPOLL_WAIT RETORNA LA CANT DE EVENTOS QUE OCURRIERON  XX
// 		int nfds = epoll_wait(epoll_fd, eventos, MAX_CANT, -1); 
// 		if (nfds == -1) {
// 			perror("epoll_wait");
// 			exit(EXIT_FAILURE);
// 		}
// 		for (int n = 0; n < nfds; ++n) {
// 			if (eventos[n].data.fd == listen_sock) {
// 				int conn_sock = accept(listen_sock, NULL, NULL);
// 				if (conn_sock == -1) {
// 					perror("accept");
// 					exit(EXIT_FAILURE);
// 				}
// 				ev.events = EPOLLIN | EPOLLONESHOT; 			// IMPORTANTE, LUEGO DEL HANDLE DEBO REARMAR EL SOCKET EN EL EPOLL XX
																
// 				ev.data.fd = conn_sock;
// 				if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
// 					perror("epoll_ctl: conn_sock");
// 					exit(EXIT_FAILURE);
// 				}
// 			}
// 			else{
// 				// ACA VA A IR MI LOGICA PARA MANEJAR LOS PEDIDOS, YA SEAN LOCALES O DE OTRO AGENTE C  XX
// 				handle_conn(eventos[n].data.fd, epoll_fd);
// 				//do_use_fd(events[n].data.fd);
// 			}
// 		}

// 	}
// 	return NULL;
// }





// int main(){

// 	// Creo el epoll, el cual es un fd especial epollfd que actuará como nuestro monitor de eventos (bandera = 0 => devuelve un file descriptor)
// 	int epollfd = epoll_create1(0); 


// 	if (epollfd == -1) {
// 		perror("epoll_create1");
// 		exit(EXIT_FAILURE);
// 	}

	
	
// 	/* Crear socket de escucha (HACER UNO LOCAL Y OTRO TCP)*/
	
// 	int lsock;
// 	lsock = mk_lsock();
	

// 	// ARGUMENTOS QUE LE PASO A LA FUNCION DEL HILO, ESTE NECESITA SABER CUAL ES EL EPOLL Y EL SOCKET DE ESCUCHA, AGREGAR EL OTRO SOCKET  XX
// 	int args[2] = {epollfd, lsock};

// 	/* en el puerto lsock avisame cuando haya un epollin*/
// 	// inicializo la estructura con evento y dato
// 	ev.events = EPOLLIN; //constante, hay datos listos para entrar
// 	ev.data.fd = lsock; //el lugar que monitoreo.						// SEGURAMENTE SEA UNA ESTRUCTURA XX
	
	
	
	
	
// 	// Inicializo el array del almacenamiento
// 	for(int i = 0; i< MAX_CANT; i++){
// 		// alm[i] = malloc(sizeof(Store));
// 		strcpy(alm[i].value,"0");
// 		strcpy(alm[i].key,"0");
// 		alm[i].vacio = 1; 
// 	}
	

// 	// agrego al epoll el lsock y lo comienzo a vigilar con las especificaciones de ev
// 	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, lsock, &ev) == -1){ //epolladd agrega el lsock a la lista de vigilancia del epoll
// 		perror("epoll_ctl: listen_sock");
//         exit(EXIT_FAILURE);
// 	}


// 	pthread_t h1, h2, h3, h4;
// 	pthread_create(&h1, NULL, manejar_cliente, args);
// 	pthread_create(&h2, NULL, manejar_cliente, args);
// 	pthread_create(&h3, NULL, manejar_cliente, args);
// 	pthread_create(&h4, NULL, manejar_cliente, args);
// 	pthread_join(h1, NULL);
// 	pthread_join(h2, NULL);
// 	pthread_join(h3, NULL);
// 	pthread_join(h4, NULL);


// 	return 0;
// }
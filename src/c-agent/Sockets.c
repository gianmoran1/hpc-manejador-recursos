#include "Sockets.h"



void quit(char *s)
{
	perror(s);
	abort();
}


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
int mk_tcp_lsock(int port, const char* ip) {

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
int mk_udp_lsock(int port) {
    int usock = socket(AF_INET, SOCK_DGRAM, 0);
    if (usock < 0) quit("socket UDP");

    int yes = 1;
    // SO_REUSEPORT permite que varios nodos en la misma PC de pruebas escuchen el mismo broadcast
    if (setsockopt(usock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0)
        quit("setsockopt UDP");

    int broadcast_enable = 1;
    setsockopt(usock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(usock, (struct sockaddr *)&sa, sizeof sa) < 0)
        quit("bind UDP");

    set_nonblocking(usock);

    return usock;
}



// Crea un temporizador que "dispara" cada X segundos
int mk_timer(int segundos) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd == -1) quit("timerfd_create");

    struct itimerspec ts;
    ts.it_value.tv_sec = segundos;      // Primer disparo en X segundos
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = segundos;   // Repetir cada X segundos
    ts.it_interval.tv_nsec = 0;
    
    if (timerfd_settime(tfd, 0, &ts, NULL) == -1) quit("timerfd_settime");

    return tfd;
}

// Conecta a un nodo y lo deja registrado en el epoll.
// Devuelve el nuevo FD si tuvo éxito, o -1 si falló.
int conectar_a_nodo(const char *ip_destino, int puerto_destino) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    set_nonblocking(sockfd);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(puerto_destino);
    dest.sin_addr.s_addr = inet_addr(ip_destino);

    printf("Iniciando conexión a %s:%d...\n", ip_destino, puerto_destino);
    int res = connect(sockfd, (struct sockaddr *)&dest, sizeof(dest));
    
    if (res < 0 && (errno != EINPROGRESS)) {
        close(sockfd);
        return -1;
    }

    if (res < 0 && (errno == EINPROGRESS)) {
        fd_set set_escritura;
        FD_ZERO(&set_escritura);
        FD_SET(sockfd, &set_escritura);
        
        struct timeval timeout;
        timeout.tv_sec = 2; // Límite para conectarse
        timeout.tv_usec = 0;

        int listos = select(sockfd + 1, NULL, &set_escritura, NULL, &timeout);
        
        if (listos <= 0) {
            printf("Timeout: El nodo %s:%d no responde.\n", ip_destino, puerto_destino);
            close(sockfd);
            return -1;
        }
        
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0) {
            printf("Conexión rechazada por %s:%d\n", ip_destino, puerto_destino);
            close(sockfd);
            return -1;
        }
    }
    printf("¡Conexión establecida exitosamente con %s en el FD %d!\n", ip_destino, sockfd);


    return sockfd;
}
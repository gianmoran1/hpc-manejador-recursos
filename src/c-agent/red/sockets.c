#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/timerfd.h> 
#include <time.h>
#include <errno.h>

#include "red/sockets.h"
#include "config.h"

#define BACKLOG_TCP  10
#define IP_PARA_OBTENER_IP_LOCAL "8.8.8.8"
#define PUERTO_PARA_OBTENER_IP_LOCAL 53

void quit(char *s)
{
	perror(s);
	abort();
}

void obtener_mi_ip_local(char *buffer_ip) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        strcpy(buffer_ip, IP_LOCAL); // Fallback de seguridad
        return;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(IP_PARA_OBTENER_IP_LOCAL);
    serv.sin_port = htons(PUERTO_PARA_OBTENER_IP_LOCAL);

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

// Pone el socket en modo no bloqueante: si un recv/accept no tiene datos listos,
// devuelve EAGAIN/EWOULDBLOCK al instante en vez de dormir al hilo. Es lo que le
// permite al loop de epoll drenar cada socket hasta vaciarlo sin quedarse colgado.
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // Lee las flags actuales del fd
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK); // Reescribe agregando O_NONBLOCK
}

// Crea un socket TCP de escucha no bloqueante en ip:port. Lo usa el agente dos
// veces: una con la IP pública (para otros agentes C) y otra con "127.0.0.1"
// (para Erlang). Aborta vía quit() ante cualquier error. Devuelve el fd de escucha.
int mk_tcp_lsock(int port, const char* ip) {
	// Crea el socket
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0)
        quit("socket TCP");

    // setsockopt configura una opción del socket.
    // SO_REUSEADDR permite volver a bindear el puerto de inmediato.
    int yes = 1;
    if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0)
        quit("setsockopt TCP");

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = inet_addr(ip);

    // Le bindea el puerto e ip.
    if (bind(lsock, (struct sockaddr *)&sa, sizeof sa) < 0)
        quit("bind TCP");

    // El backlog es el largo máximo de la cola de conexiones ya establecidas 
    // que esperan un accept().
    if (listen(lsock, BACKLOG_TCP) < 0)
        quit("listen TCP");

    set_nonblocking(lsock);

    return lsock;
}

// Crea el socket UDP no bloqueante que recibe los ANNOUNCE de toda la red,
// bindeado a INADDR_ANY:port y habilitado para broadcast.
int mk_udp_lsock(int port) {
    int usock = socket(AF_INET, SOCK_DGRAM, 0);
    if (usock < 0) quit("socket UDP");

    int yes = 1;
    if (setsockopt(usock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes) < 0)
        quit("setsockopt UDP");

    int broadcast_enable = 1;
    setsockopt(usock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable,
                sizeof(broadcast_enable));

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(usock, (struct sockaddr *)&sa, sizeof sa) < 0)
        quit("bind UDP");

    set_nonblocking(usock);

    return usock;
}

// Crea un timerfd no bloqueante que "dispara" cada 'segundos'
// segundos, con el primer disparo también a los 'segundos'. Se registra en epoll
// como un fd más. Aborta vía quit() ante error. Devuelve el fd del timer.
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

// Devuelve 1 si hay un mensaje listo en 'buffer_destino', o 0 si no hay nada.
int atender_cliente_udp(int usock_udp, char *buffer_destino, size_t tamano_maximo) {
    char buffer_red[512];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);

    ssize_t bytes_recibidos = recvfrom(usock_udp, buffer_red, 
        sizeof(buffer_red) - 1, 0, (struct sockaddr *)&src_addr, &src_len);
    
    if (bytes_recibidos <= 0) {
        // Red vacía o error, avisamos que no hay nada
        return 0; 
    }

    buffer_red[bytes_recibidos] = '\0'; 
    char ip_remitente[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(src_addr.sin_addr), ip_remitente, INET_ADDRSTRLEN);
    // Escribimos el resultado directamente en la variable del llamador
    // Armo el mensaje como ip ANNOUNCE puerto recursos
    snprintf(buffer_destino, tamano_maximo, "%s %s", ip_remitente, buffer_red);
    return 1; 
}

// Envía un datagrama UDP a una IP y puerto específicos.
// Devuelve 1 si fue un éxito, o -1 si hubo un error.
int enviar_mensaje_udp(int usock_udp, const char *ip_destino, 
                        int puerto_destino, const char *mensaje) {
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(puerto_destino);
    dest.sin_addr.s_addr = inet_addr(ip_destino);

    ssize_t enviados = sendto(usock_udp, mensaje, strlen(mensaje), 0, 
                                (struct sockaddr*)&dest, sizeof(dest));

    if (enviados <= 0) {
        perror("Error enviando mensaje UDP");
        return -1;
    }

    return 1;
}

int atender_cliente_tcp(ClienteConectado *cliente) {
    ssize_t bytes_recibidos;

    // Leo hasta vaciar lo que hay en el socket, y lo voy acumulando en el 
    // buffer de este cliente. Si el socket se queda sin datos, salgo del bucle 
    // para procesar lo que ya tengo acumulado. Si recv me devuelve 0, es 
    // porque el cliente se desconectó, así que cierro todo y libero memoria.
    while (1) {
        // Calculamos cuánto espacio libre nos queda en la ficha de este cliente
        int espacio_disponible = sizeof(cliente->buffer) - cliente->bytes_leidos - 1;

        if (espacio_disponible <= 0) {
            perror("Error mensaje demasiado largo");
            return 0;
        }

        // Leemos de la red y lo guardamos DIRECTAMENTE al final de lo que ya teníamos
        bytes_recibidos = recv(cliente->fd, cliente->buffer + cliente->bytes_leidos, espacio_disponible, 0);

        if (bytes_recibidos == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Red vacía, salimos a procesar
            } else {
                perror("Error leyendo del socket");
                return 0;
            }
        } 
        else if (bytes_recibidos == 0) 
            return 0;
        else {
            // Actualizamos el contador de bytes y cerramos el string
            cliente->bytes_leidos += bytes_recibidos;
            cliente->buffer[cliente->bytes_leidos] = '\0';
        }
    }
    return 1; 
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
            // Timeout
            close(sockfd);
            return -1;
        }
        
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0) {
            // Conexion rechazada
            close(sockfd);
            return -1;
        }
    }

    return sockfd;
}


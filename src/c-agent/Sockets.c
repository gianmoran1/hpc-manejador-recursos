#include "Sockets.h"


extern char mi_ip_publica[16];



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



int atender_cliente_tcp(ClienteConectado *cliente) {
    ssize_t bytes_recibidos;

    // leo hasta vaciar lo que hay en el socket, y lo voy acumulando en el buffer de este cliente. 
    // Si el socket se queda sin datos,  salgo del bucle para procesar lo que ya tengo acumulado. 
    // Si recv me devuelve 0, es porque el cliente se desconectó, así que cierro todo y libero memoria.
    while (1) {
        // Calculamos cuánto espacio libre nos queda en la ficha de este cliente
        int espacio_disponible = sizeof(cliente->buffer) - cliente->bytes_leidos - 1;

        // PROTECCIÓN ANTI-DOS: Si se llenó el buffer y nunca mandó un '\n', es un nodo defectuoso
        if (espacio_disponible <= 0) {
            printf("[FD %d] Error: Mensaje demasiado largo sin '\\n'. Desconectando por seguridad.\n", cliente->fd);
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
        else if (bytes_recibidos == 0) return 0;
            // // ¡Desconexión limpia!             XXXXX SE SACA
            // if (cliente->es_erlang) {
            //     printf("[FD %d] ALERTA: Erlang se desconectó.\n", cliente->fd);
            // } else {
            //     printf("[FD %d] Nodo de red desconectado.\n", cliente->fd);
            //     // santos_eliminar_nodo(cliente->fd);
            // }
            // close(cliente->fd);
            // free(cliente);
        else {
            // Actualizamos el contador de bytes y cerramos el string
            cliente->bytes_leidos += bytes_recibidos;
            cliente->buffer[cliente->bytes_leidos] = '\0';
        }
    }
    return 1; 

}

// Devuelve 1 si hay un mensaje listo en 'buffer_destino', o 0 si no hay nada o no nos interesa porque es nuestro mensaje.
int atender_cliente_udp(int usock_udp, char *buffer_destino, size_t tamano_maximo) {
    char buffer_red[512]; // Un temporal cortito solo para sacar los datos del enchufe
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);

    ssize_t bytes_recibidos = recvfrom(usock_udp, buffer_red, sizeof(buffer_red) - 1, 0, (struct sockaddr *)&src_addr, &src_len);
    
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


// Envía un mensaje a un cliente ya conectado. Devuelve 1 si se envió bien, o -1 si hubo un error (socket caído, etc).
int enviar_mensaje_tcp(int fd, const char *mensaje) {
    ssize_t enviados = send(fd, mensaje, strlen(mensaje), 0);
    
    if (enviados <= 0) {
        printf("Error: No se pudo enviar al FD %d (socket roto o caído).\n", fd);
        return -1; 
    }
    
    printf("Mensaje enviado correctamente al FD %d: %s", fd, mensaje);
    return 1;
}


// Envía un datagrama UDP a una IP y puerto específicos.
// Devuelve 1 si fue un éxito, o -1 si hubo un error.
int enviar_mensaje_udp(int usock_udp, const char *ip_destino, int puerto_destino, const char *mensaje) {
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(puerto_destino);
    dest.sin_addr.s_addr = inet_addr(ip_destino);

    ssize_t enviados = sendto(usock_udp, mensaje, strlen(mensaje), 0, (struct sockaddr*)&dest, sizeof(dest));

    if (enviados <= 0) {
        perror("Error enviando mensaje UDP");
        return -1;
    }

    // Como es UDP, no sabemos si el otro lo recibió, solo sabemos que salió de nuestra placa de red.
    // printf("Datagrama UDP enviado a %s:%d -> %s", ip_destino, puerto_destino, mensaje);
    return 1;
}
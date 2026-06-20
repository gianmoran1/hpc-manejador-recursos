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
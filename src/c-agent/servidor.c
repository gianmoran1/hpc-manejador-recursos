#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <pthread.h>

#include "config.h"
#include "servidor.h"
#include "gestor_estado.h"
#include "controlador.h"
#include "red/sockets.h"
#include "red/cliente.h"

// Descriptores del servidor.
int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int erlangSocket = -1;
int timer_anuncios_fd;
int timer_timeout;
char mi_ip_publica[16];

static void servidor_modificar_cliente_en_epoll(ClienteConectado *cliente, int flags) {
    struct epoll_event ev;
    ev.events = flags;
    ev.data.ptr = cliente;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cliente->fd, &ev) == -1) {
        perror("Error al modificar cliente en el epoll");
        close(cliente->fd);
        exit(EXIT_FAILURE);
    }
}

static void servidor_aceptar_cliente(int fd_listo) {
    struct sockaddr_in cliente_addr;
    socklen_t cliente_len = sizeof(cliente_addr);

    // Acepto la conexion en el socket
    int conn_sock = accept(fd_listo, (struct sockaddr *)&cliente_addr, &cliente_len);
    if (conn_sock == -1) {
        // Si hubo un error falso por ser no-bloqueante, lo ignoramos
        return;
    }

    // Convierto el nuevo socket a no bloqueante
    set_nonblocking(conn_sock);

    ClienteConectado *clienteNuevo =
        crear_cliente_conectado(conn_sock, (fd_listo == lsock_local) ? 1 : 0);
    servidor_agregar_cliente_en_epoll(clienteNuevo, EPOLLIN | EPOLLONESHOT);

    if (fd_listo == lsock_local) {
        erlangSocket = conn_sock;
        printf("[CONEXIONES] Nueva conexión local aceptada para Erlang (FD asignado: %d)\n", conn_sock);
    } else
        printf("[CONEXIONES] Nueva conexión de red aceptada (FD asignado: %d)\n", conn_sock);
}

static void servidor_gestion_anuncio_recibido() {
    char buffer_udp[1024];
    int valido = atender_cliente_udp(usock_udp, buffer_udp, sizeof(buffer_udp));
    if (!valido) 
        return;
    printf("[CONEXIONES] ANNOUNCE UDP recibido: %s\n", buffer_udp);
    controlador_anuncio_recibido(buffer_udp);
}

static void servidor_gestion_cliente(ClienteConectado* cliente) {
    int ret = atender_cliente_tcp(cliente);
    if (ret == 0) { // El socket se rompió o desconectó.
        printf("[CONEXIONES] Nodo de red (FD %d) desconectado.\n", cliente->fd);
        controlador_desconexion_cliente(cliente->fd);
        destruir_cliente(cliente);
    } else {
        printf("[CONEXIONES] Mensaje recibido de cliente (FD %d).\n", cliente->fd);
        controlador_mensaje_cliente(cliente);
        servidor_modificar_cliente_en_epoll(cliente, EPOLLIN | EPOLLONESHOT);
    }
}

void* servidor_bucle_principal(void* args) {
    (void)args;
    struct epoll_event eventos[MAX_EVENTS];

    while(1) {
        // El hilo se duerme acá hasta que el epoll le avise de algo
        int nfds = epoll_wait(epoll_fd, eventos, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; n++) {
            ClienteConectado *entidad = (ClienteConectado *) eventos[n].data.ptr;
            int fd_listo = entidad->fd;

            // Socket de escucha: hay una nueva conexión queriendo entrar.
            if ((fd_listo == lsock_publico) || (fd_listo == lsock_local))
                servidor_aceptar_cliente(fd_listo);
            // UDP: algún nodo se está anunciando con un ANNOUNCE.
            else if (fd_listo == usock_udp)
                servidor_gestion_anuncio_recibido();
            else if (fd_listo == timer_anuncios_fd)
                controlador_anuncio_recursos();
            else if (fd_listo == timer_timeout)
                timer_deadlock_nodos();
            // Cliente ya conectado mandando texto (RESERVE, JOB_REQUEST, etc).
            else // No es EPOLLEXCLUSIVE, race condition posible.
                servidor_gestion_cliente((ClienteConectado *) eventos[n].data.ptr);
        }
    }
    return NULL;
}

void servidor_agregar_cliente_en_epoll(ClienteConectado *cliente, int flags) {
    struct epoll_event ev;
    ev.events = flags;
    ev.data.ptr = cliente;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cliente->fd, &ev) == -1) {
        perror("Error al agregar cliente al epoll");
        close(cliente->fd);
        exit(EXIT_FAILURE);
    }
}

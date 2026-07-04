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

// Estado global y descriptores del servidor.
int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int erlangSocket = -1;
int timer_anuncios_fd;
int timer_timeout;
char mi_ip_publica[16];
EstadoGlobal estado = NULL;

void agregar_cliente_en_epoll(ClienteConectado *cliente, int flags) {
    struct epoll_event ev;
    ev.events = flags;
    ev.data.ptr = cliente;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cliente->fd, &ev) == -1) {
        perror("Error al agregar cliente al epoll");
        close(cliente->fd);
        exit(EXIT_FAILURE);
    }
}

void modificar_cliente_en_epoll(ClienteConectado *cliente, int flags) {
    struct epoll_event ev;
    ev.events = flags;
    ev.data.ptr = cliente;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cliente->fd, &ev) == -1) {
        perror("Error al modificar cliente en el epoll");
        close(cliente->fd);
        exit(EXIT_FAILURE);
    }
}

void aceptar_cliente(int fd_listo) {
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
    agregar_cliente_en_epoll(clienteNuevo, EPOLLIN | EPOLLONESHOT);

    if (fd_listo == lsock_local) {
        erlangSocket = conn_sock;
        printf("¡Nueva conexión local aceptada para Erlang! (FD asignado: %d)\n", conn_sock);
    } else
        printf("¡Nueva conexión de red aceptada! (FD asignado: %d)\n", conn_sock);
}

void* bucle_principal(void* args) {
    (void)args;
    struct epoll_event eventos[MAX_EVENTS];

    while(1) {
        // El hilo se duerme acá hasta que el epoll le avise de algo
        int nfds = epoll_wait(epoll_fd, eventos, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {
            ClienteConectado *entidad = (ClienteConectado *) eventos[n].data.ptr;
            int fd_listo = entidad->fd;

            // Socket de escucha: hay una nueva conexión queriendo entrar.
            if ((fd_listo == lsock_publico) || (fd_listo == lsock_local))
                aceptar_cliente(fd_listo);

            // UDP: algún nodo se está anunciando con un ANNOUNCE.
            else if (fd_listo == usock_udp)
                if (parseo_anuncio() == 0)
                    continue;

            else if (fd_listo == timer_anuncios_fd)
                anunciar_recursos();

            else if (fd_listo == timer_timeout)
                timer_deadlock_nodos();

            // Cliente ya conectado mandando texto (RESERVE, JOB_REQUEST, etc).
            else {
                ClienteConectado *cliente = (ClienteConectado *) eventos[n].data.ptr;
                int ret = atender_cliente_tcp(cliente);
                if (ret == 0) {
                    // El socket se rompió o desconectó.
                    printf("Nodo de red (FD %d) desconectado. Liberando sus recursos...\n", cliente->fd);
                    manejar_desconexion_socket(estado, cliente->fd, callback_granted_red);
                    // Evitar que nodo_destruir intente close+free de un ClienteConectado
                    // que ya vamos a liberar nosotros justo debajo.
                    nodo_limpiar_conexion_por_fd(cliente->fd, estado->registro_nodos);
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

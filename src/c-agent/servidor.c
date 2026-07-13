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
#include <errno.h>

#include "servidor.h"
#include "gestor_estado.h"
#include "controlador.h"
#include "red/sockets.h"

#define TAM_MAX_TABLA_FD 1024
#define TAM_BUFFER_UDP 1024
#define MAX_EVENTS 10 // Eventos devueltos por cada epoll_wait()

int epoll_fd;
int lsock_publico;
int lsock_local;
int usock_udp;
int timer_anuncios_fd;
int timer_timeout;
char mi_ip_publica[TAM_BUFFER_IP];
ClienteConectado *clientes_por_fd[TAM_MAX_TABLA_FD];
pthread_mutex_t lock_tabla_fd = PTHREAD_MUTEX_INITIALIZER;

static void servidor_rearmar_cliente(ClienteConectado *cliente);
static void servidor_desconectar_cliente(ClienteConectado *cliente);
static void servidor_gestion_cliente(ClienteConectado *cliente);
static int servidor_drenar_salida(ClienteConectado *cliente);
static ClienteConectado* servidor_cliente_por_fd(int fd);
static void servidor_armar_epollout(ClienteConectado *cliente);
static void servidor_aceptar_cliente(int fd_listo);
static void servidor_gestion_anuncio_recibido();

void servidor_descubrimiento_inicial(int sec) {
    controlador_emitir_anuncio();
    sleep(sec);
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

    // Registro el cliente en la tabla de clientes indexados por el fd.
    if (cliente->fd >= 0 && cliente->fd < TAM_MAX_TABLA_FD) {
        pthread_mutex_lock(&lock_tabla_fd);
        clientes_por_fd[cliente->fd] = cliente;
        pthread_mutex_unlock(&lock_tabla_fd);
    }
}

void* servidor_bucle_principal(void* args) {
    (void)args;
    struct epoll_event eventos[MAX_EVENTS];

    while(1) {
        // El hilo se duerme acá hasta que el epoll le avise de algo.
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
                controlador_timer_deadlock_y_desconexion();

            // Cliente ya conectado: el evento puede traer EPOLLIN (algo para leer),
            // EPOLLOUT (hay lugar para drenar la salida pendiente), o error.
            else {
                ClienteConectado *cliente = (ClienteConectado *) eventos[n].data.ptr;
                unsigned ev = eventos[n].events;

                if (ev & (EPOLLHUP | EPOLLERR)) {
                    servidor_desconectar_cliente(cliente);
                    continue;
                }
                // Drenamos primero (libera espacio); si el drenado desconectó, no leemos.
                if (ev & EPOLLOUT) {
                    if (!servidor_drenar_salida(cliente))
                        continue;
                }
                if (ev & EPOLLIN)
                    servidor_gestion_cliente(cliente);
            }
        }
    }
    return NULL;
}

int enviar_mensaje_tcp(int fd, const char *mensaje) {
    ClienteConectado *cliente = servidor_cliente_por_fd(fd);
    size_t largo = strlen(mensaje);

    // Fd sin cliente en el mapa (socket de escucha/timer, o ya cerrado): envío directo.
    if (cliente == NULL)
        return send(fd, mensaje, largo, MSG_NOSIGNAL) > 0 ? 1 : -1;

    pthread_mutex_lock(&cliente->lock_salida);

    size_t enviado = 0;
    // Solo intentamos mandar directo si NO hay nada encolado (para no romper el
    // orden: lo que ya está en el buffer tiene que salir primero).
    if (cliente->bytes_salida == 0) {
        ssize_t n = send(fd, mensaje, largo, MSG_NOSIGNAL);
        if (n > 0) {
            enviado = (size_t)n;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            pthread_mutex_unlock(&cliente->lock_salida);
            return -1;  // socket roto de verdad
        }
        // n < 0 con EAGAIN → 'enviado' queda en 0: se buffea todo.
        if (enviado == largo) {                         // entró todo
            pthread_mutex_unlock(&cliente->lock_salida);
            return 1;                                    // no hace falta EPOLLOUT
        }
    }

    // Quedó un remanente (parcial/EAGAIN, o ya había cola): al buffer de salida.
    size_t restante = largo - enviado;
    if (cliente->bytes_salida + restante > sizeof(cliente->buffer_salida)) {
        // El peer no drena y el buffer se llenó → backpressure sostenido: cortamos.
        pthread_mutex_unlock(&cliente->lock_salida);
        return -1;
    }
    memcpy(cliente->buffer_salida + cliente->bytes_salida, mensaje + enviado, restante);
    cliente->bytes_salida += restante;

    servidor_armar_epollout(cliente);  // que el epoll nos avise cuando haya lugar

    pthread_mutex_unlock(&cliente->lock_salida);
    return 1;
}

// Re-armado final del cliente en el epoll: recalcula la máscara (EPOLLIN siempre
// + EPOLLOUT si quedó salida pendiente) y hace el MOD, TODO bajo lock_salida, para
// que el cálculo y el armado sean atómicos y un envío concurrente no pierda el
// EPOLLOUT. Lo llaman el fin de lectura y el fin de drenado: los únicos que pueden
// re-armar EPOLLIN, porque saben que nadie más está leyendo ese fd.
static void servidor_rearmar_cliente(ClienteConectado *cliente) {
    pthread_mutex_lock(&cliente->lock_salida);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT | (cliente->bytes_salida > 0 ? EPOLLOUT : 0);
    ev.data.ptr = cliente;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cliente->fd, &ev);  // si falla (fd cerrándose), se ignora
    pthread_mutex_unlock(&cliente->lock_salida);
}

// Desconexión de un cliente: libera sus recursos en el gestor, lo saca del mapa
// fd->cliente y destruye el ClienteConectado (close + free). Compartida por el
// camino de lectura, el de drenado y los eventos EPOLLHUP/EPOLLERR.
static void servidor_desconectar_cliente(ClienteConectado *cliente) {
    printf("[CONEXIONES] Nodo de red (FD %d) desconectado.\n", cliente->fd);
    controlador_desconexion_cliente(cliente->fd);
    if (cliente->fd >= 0 && cliente->fd < TAM_MAX_TABLA_FD) {
        pthread_mutex_lock(&lock_tabla_fd);
        clientes_por_fd[cliente->fd] = NULL;
        pthread_mutex_unlock(&lock_tabla_fd);
    }
    destruir_cliente(cliente);
}

static void servidor_gestion_cliente(ClienteConectado* cliente) {
    int ret = atender_cliente_tcp(cliente);
    if (ret == 0) { // el socket se rompió o se cerró
        servidor_desconectar_cliente(cliente);
    } else {
        printf("[CONEXIONES] Mensaje recibido de cliente (FD %d).\n", cliente->fd);
        controlador_mensaje_cliente(cliente);
        servidor_rearmar_cliente(cliente); // re-arma según haya salida pendiente
    }
}

// Drena el buffer de salida cuando el epoll avisa EPOLLOUT (hay lugar en el buffer
// de envío del kernel). Manda lo que pueda; el re-armado deja EPOLLOUT prendido si
// quedó algo, o lo apaga si drenó todo. Devuelve 1 si el cliente sigue vivo, 0 si
// el socket se rompió y se desconectó (el caller no debe seguir usándolo).
static int servidor_drenar_salida(ClienteConectado *cliente) {
    int roto = 0;
    pthread_mutex_lock(&cliente->lock_salida);
    while (cliente->bytes_salida > 0) {
        ssize_t n = send(cliente->fd, cliente->buffer_salida,
                         cliente->bytes_salida, MSG_NOSIGNAL);
        if (n > 0) {
            cliente->bytes_salida -= (int)n;
            memmove(cliente->buffer_salida, cliente->buffer_salida + n,
                    cliente->bytes_salida);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;                                // el buffer del kernel se llenó otra vez
        } else {
            roto = 1;                             // socket roto de verdad
            break;
        }
    }
    pthread_mutex_unlock(&cliente->lock_salida);

    if (roto) {
        servidor_desconectar_cliente(cliente);
        return 0;
    }
    servidor_rearmar_cliente(cliente);
    return 1;
}

static ClienteConectado* servidor_cliente_por_fd(int fd){
    if (fd < 0 || fd >= TAM_MAX_TABLA_FD) return NULL;
    pthread_mutex_lock(&lock_tabla_fd);
    ClienteConectado *c = clientes_por_fd[fd];
    pthread_mutex_unlock(&lock_tabla_fd);
    return c;
}

// Arma EPOLLOUT en el cliente SIN tocar EPOLLIN. Se usa desde el camino de envío
// cuando quedó salida pendiente.
// CLAVE: no re-arma EPOLLIN. Si otro hilo está leyendo este mismo fd (su evento
// EPOLLONESHOT ya se consumió), volver a armar EPOLLIN dispararía una SEGUNDA
// lectura concurrente sobre el mismo buffer de lectura → corrupción. EPOLLIN lo
// restaura el re-armado final (fin de lectura o fin de drenado), que sí sabe que
// nadie más está leyendo. Mientras tanto, los reads de este fd quedan en pausa
// (los datos esperan en el buffer del kernel, no se pierden) hasta que se drene.
static void servidor_armar_epollout(ClienteConectado *cliente) {
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    ev.data.ptr = cliente;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cliente->fd, &ev);  // si falla (fd cerrándose), se ignora
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

    // EPOLLONESHOT hace que se derpierte un solo hilo y lo desactiva
    // hasta que se re-arme con EPOLL_CTL_MOD.
    servidor_agregar_cliente_en_epoll(clienteNuevo, EPOLLIN | EPOLLONESHOT);

    if (fd_listo == lsock_local)
        printf("[CONEXIONES] Nueva conexión local aceptada para Erlang (FD asignado: %d)\n", conn_sock);
    else
        printf("[CONEXIONES] Nueva conexión de red aceptada (FD asignado: %d)\n", conn_sock);
}

static void servidor_gestion_anuncio_recibido() {
    char buffer_udp[TAM_BUFFER_UDP];
    int valido = atender_cliente_udp(usock_udp, buffer_udp, sizeof(buffer_udp));
    if (!valido)
        return;
    printf("[CONEXIONES] ANNOUNCE UDP recibido: %s\n", buffer_udp);
    controlador_anuncio_recibido(buffer_udp);
}

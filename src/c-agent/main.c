#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <sys/epoll.h>

#include "config.h"
#include "servidor.h"
#include "gestor_estado.h"
#include "controlador.h"
#include "red/sockets.h"
#include "red/cliente.h"

#include "modelo/estado.h"

int main(void) {
    signal(SIGPIPE, SIG_IGN); // Evita que escribir en un socket cerrado mate el proceso.

    estado = estado_crear(CAP_CPU, CAP_GPU, CAP_MEM);
    obtener_mi_ip_local(mi_ip_publica);

    // Registrarse en la propia tabla de nodos para verse a sí mismo en GET_NODES.
    gestor_procesar_anuncio(estado, mi_ip_publica, PUERTO_TCP,
                            CAP_CPU, CAP_GPU, CAP_MEM);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        quit("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // Sockets de escucha: TCP público (otros agentes C), TCP local (Erlang) y UDP.
    lsock_publico = mk_tcp_lsock(PUERTO_TCP, mi_ip_publica);
    lsock_local   = mk_tcp_lsock(PUERTO_TCP, IP_LOCAL);
    usock_udp     = mk_udp_lsock(PUERTO_UDP);

    timer_anuncios_fd = mk_timer(INTERVALO_ANUNCIO);
    timer_timeout     = mk_timer(INTERVALO_MANTENIMIENTO);

    // Registro los sockets de entrada y los timers en el epoll con EPOLLEXCLUSIVE
    // (fds compartidos permanentes: el kernel despierta a un solo hilo).
    servidor_agregar_cliente_en_epoll(crear_cliente_conectado(lsock_publico, 0), 
                                        EPOLLIN | EPOLLEXCLUSIVE);
    servidor_agregar_cliente_en_epoll(crear_cliente_conectado(lsock_local, 0), 
                                        EPOLLIN | EPOLLEXCLUSIVE);
    servidor_agregar_cliente_en_epoll(crear_cliente_conectado(usock_udp, 0), 
                                        EPOLLIN | EPOLLEXCLUSIVE);
    servidor_agregar_cliente_en_epoll(crear_cliente_conectado(timer_anuncios_fd, 0),
                                        EPOLLIN | EPOLLEXCLUSIVE);
    servidor_agregar_cliente_en_epoll(crear_cliente_conectado(timer_timeout, 0), 
                                        EPOLLIN | EPOLLEXCLUSIVE);

    printf("[CONEXIONES] Servidor HPC iniciado, mi IP en la red es: %s. ", mi_ip_publica);
    printf("Escuchando en puerto %d...\n", PUERTO_TCP);

    // Lanzo los hilos trabajadores; todos corren el mismo bucle sobre el epoll.
    pthread_t hilos[NUM_HILOS];
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_create(&hilos[i], NULL, servidor_bucle_principal, NULL);

    // Espero a que terminen (en operación normal, nunca: es un bucle infinito).
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_join(hilos[i], NULL);

    return 0;
}

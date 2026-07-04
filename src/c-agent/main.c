#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <sys/epoll.h>

#include "config.h"
#include "servidor.h"
#include "gestor_estado.h"
#include "red/sockets.h"
#include "red/cliente.h"

/*
 * Punto de entrada del agente C. Prepara todo el andamiaje y lanza los workers:
 *   1. Ignora SIGPIPE (escribir en un socket cerrado devuelve error, no mata).
 *   2. Crea el estado global, descubre la IP local y se registra a sí mismo en
 *      la tabla de nodos (así Erlang se ve en GET_NODES).
 *   3. Crea el epoll, los sockets de escucha (TCP público, TCP local, UDP) y los
 *      dos timers, y los registra en el epoll con EPOLLEXCLUSIVE.
 *   4. Lanza NUM_HILOS hilos corriendo bucle_principal y los espera (el server
 *      corre indefinidamente, así que el join no retorna en operación normal).
 */
int main(void) {
    signal(SIGPIPE, SIG_IGN); // Evita que escribir en un socket cerrado mate el proceso.

    estado = estado_crear(CAP_CPU, CAP_GPU, CAP_MEM);
    obtener_mi_ip_local(mi_ip_publica);

    // Registrarse en la propia tabla de nodos para verse a sí mismo en GET_NODES.
    gestor_procesar_anuncio(estado, mi_ip_publica, PUERTO_TCP,
                            CAP_CPU, CAP_GPU, CAP_MEM);

    printf("Arrancando... Mi IP en la red es: %s\n", mi_ip_publica);

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
    // (fds compartidos permanentes: el kernel despierta a un solo hilo y evita el
    // thundering herd de los 4 workers).
    agregar_cliente_en_epoll(crear_cliente_conectado(lsock_publico, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(lsock_local, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(usock_udp, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(timer_anuncios_fd, 0), EPOLLIN | EPOLLEXCLUSIVE);
    agregar_cliente_en_epoll(crear_cliente_conectado(timer_timeout, 0), EPOLLIN | EPOLLEXCLUSIVE);

    printf("Servidor HPC iniciado. Escuchando en puerto %d...\n", PUERTO_TCP);

    // Lanzo los hilos trabajadores; todos corren el mismo bucle sobre el epoll.
    pthread_t hilos[NUM_HILOS];
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_create(&hilos[i], NULL, bucle_principal, NULL);

    // Espero a que terminen (en operación normal, nunca: es un bucle infinito).
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_join(hilos[i], NULL);

    return 0;
}

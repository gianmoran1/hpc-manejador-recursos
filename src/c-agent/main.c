#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <sys/epoll.h>

#include "servidor.h"      // Creo los clientes del epoll.
#include "gestor_estado.h" // Registro el nodo en mi tabla de nodos.
#include "controlador.h"   // El estado del programa vive en el controlador.
#include "red/sockets.h"   // Obtengo mi IP, creo sockets.
#include "red/cliente.h"   // Creo el cliente.
#include "modelo/estado.h" // Creo el estado.

#define CAP_CPU 4
#define CAP_GPU 1
#define CAP_MEM 8192
#define NUM_HILOS 4
#define INTERVALO_ANUNCIO 5          // Cada cuánto se emite un ANNOUNCE.
#define INTERVALO_MANTENIMIENTO 15   // Cada cuánto se expiran pedidos y nodos.
#define TIEMPO_DESCUBRIMIENTO 2

int main() {
    signal(SIGPIPE, SIG_IGN); // Evita que escribir en un socket cerrado mate el proceso.

    estado = estado_crear(CAP_CPU, CAP_GPU, CAP_MEM);
    obtener_mi_ip_local(mi_ip_publica);

    // Registramos nuestro nodo en la tabla de nodos del estado.
    gestor_procesar_anuncio(estado, mi_ip_publica, PUERTO_TCP,
                            CAP_CPU, CAP_GPU, CAP_MEM);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        quit("epoll_create1");
        exit(EXIT_FAILURE);
    }

    lsock_publico = mk_tcp_lsock(PUERTO_TCP, mi_ip_publica);
    lsock_local = mk_tcp_lsock(PUERTO_TCP, IP_LOCAL);
    usock_udp = mk_udp_lsock(PUERTO_UDP);
    timer_anuncios_fd = mk_timer(INTERVALO_ANUNCIO);
    timer_timeout = mk_timer(INTERVALO_MANTENIMIENTO);

    // Me anuncio y espero que lleguen anuncios al socket UDP.
    printf("[CONEXIONES] Descubriendo nodos (%d sec)...\n", TIEMPO_DESCUBRIMIENTO);
    servidor_descubrimiento_inicial(TIEMPO_DESCUBRIMIENTO);

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

    // Lanzo los hilos trabajadores, todos corren el mismo bucle sobre el epoll.
    pthread_t hilos[NUM_HILOS];
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_create(&hilos[i], NULL, servidor_bucle_principal, NULL);

    // Bucle infinito.
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_join(hilos[i], NULL);

    return 0;
}

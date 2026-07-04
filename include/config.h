#ifndef __CONFIG_H__
#define __CONFIG_H__

// Constantes de configuración del agente: valores hardcodeados que se usan en
// varios módulos (red, capacidades, temporizadores, buffers).

// Capacidad de recursos del nodo
#define CAP_CPU 4
#define CAP_GPU 1
#define CAP_MEM 8192

// Red
#define PUERTO_TCP   4040
#define PUERTO_UDP   12529
#define IP_LOCAL     "127.0.0.1"
#define IP_BROADCAST "255.255.255.255"
#define BACKLOG_TCP  10   // cola de conexiones pendientes de accept() en listen()
#define MAX_EVENTS   10   // eventos devueltos por cada epoll_wait()

// Concurrencia
#define NUM_HILOS 4       // hilos worker que comparten el epoll

// Buffers
#define TAM_BUFFER_MENSAJE 1024  // buffer de acumulación por cliente TCP
#define TAM_BUFFER_ANUNCIO 256   // mensaje ANNOUNCE saliente

// Temporizadores (segundos)
#define INTERVALO_ANUNCIO       5    // cada cuánto se emite un ANNOUNCE
#define INTERVALO_MANTENIMIENTO 15   // cada cuánto se expiran pedidos y nodos
#define TIEMPO_ESPERA_RESERVA   30.0 // un RESERVE encolado más tiempo expira (anti-deadlock)
#define TIEMPO_VIDA_NODO        15.0 // sin ANNOUNCE en este lapso, el nodo se desconecta

#endif /* __CONFIG_H__ */

#ifndef __SERVIDOR_H__
#define __SERVIDOR_H__

#include "red/cliente.h"
#include "config.h"

// Descriptores del servidor.
extern int epoll_fd;
extern int lsock_publico;
extern int lsock_local;
extern int usock_udp;
extern int timer_anuncios_fd;
extern int timer_timeout;
extern char mi_ip_publica[TAM_BUFFER_IP];

/*
 * Descubrimiento inicial al arrancar: emite un ANNOUNCE y espera la cantidad
 * de segundos indicadas por sec.
 */
void servidor_descubrimiento_inicial(int sec);

/*
 * Registra un ClienteConectado (o un socket de escucha/timer envuelto en uno)
 * en el epoll global con la máscara de eventos dada. Guarda el puntero al
 * cliente en data.ptr. Ante un fallo de epoll_ctl cierra el fd y aborta.
 */
void servidor_agregar_cliente_en_epoll(ClienteConectado *cliente, int flags);

/*
 * Bucle de eventos que corre cada hilo worker: epoll_wait + dispatch por fd.
 * No retorna en operación normal (bucle infinito).
 */
void* servidor_bucle_principal(void* args);

/*
 * Envía 'mensaje' por TCP al fd. Si el kernel no lo acepta entero (buffer de
 * envío lleno o envío parcial), buffera el remanente en el cliente y arma
 * EPOLLOUT para drenarlo. Devuelve 1 si se aceptó/bufferó, -1 si el socket está
 * roto o el buffer de salida se desbordó.
 */
int enviar_mensaje_tcp(int fd, const char *mensaje);

#endif /* __SERVIDOR_H__ */

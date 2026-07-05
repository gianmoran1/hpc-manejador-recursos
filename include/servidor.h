#ifndef __SERVIDOR_H__
#define __SERVIDOR_H__

#include "red/cliente.h"
#include "gestor_estado.h"

// Descriptores del servidor (definidos en servidor.c).
extern int epoll_fd;
extern int lsock_publico;
extern int lsock_local;
extern int usock_udp;
extern int erlangSocket;      // fd de la conexión activa con Erlang (-1 si no hay)
extern int timer_anuncios_fd;
extern int timer_timeout;
extern char mi_ip_publica[16];

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

#endif /* __SERVIDOR_H__ */

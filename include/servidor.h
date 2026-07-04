#ifndef __SERVIDOR_H__
#define __SERVIDOR_H__

#include "red/cliente.h"
#include "gestor_estado.h"

// Estado global y descriptores del servidor (definidos en servidor.c).
extern int epoll_fd;
extern int lsock_publico;
extern int lsock_local;
extern int usock_udp;
extern int erlangSocket;      // fd de la conexión activa con Erlang (-1 si no hay)
extern int timer_anuncios_fd;
extern int timer_timeout;
extern char mi_ip_publica[16];
extern EstadoGlobal estado;

/*
 * Registra un ClienteConectado (o un socket de escucha/timer envuelto en uno)
 * en el epoll global con la máscara de eventos dada. Guarda el puntero al
 * cliente en data.ptr. Ante un fallo de epoll_ctl cierra el fd y aborta.
 */
void agregar_cliente_en_epoll(ClienteConectado *cliente, int flags);

/*
 * Modifica los eventos monitoreados de un cliente ya registrado en el epoll.
 * Se usa para rearmar el EPOLLONESHOT tras procesar datos de una conexión.
 */
void modificar_cliente_en_epoll(ClienteConectado *cliente, int flags);

/*
 * Acepta una conexión entrante en el socket de escucha fd_listo, la marca
 * como no bloqueante y la registra en el epoll (EPOLLIN | EPOLLONESHOT).
 */
void aceptar_cliente(int fd_listo);

/*
 * Bucle de eventos que corre cada hilo worker: epoll_wait + dispatch por fd.
 * No retorna en operación normal (bucle infinito).
 */
void* bucle_principal(void* args);

#endif /* __SERVIDOR_H__ */

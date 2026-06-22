#ifndef __AGENTE_H__
#define __AGENTE_H__

#include <sys/epoll.h>
#include "cliente.h"

// 1. Exponemos las variables globales que otros módulos necesiten leer/usar
extern int epoll_fd;
extern char mi_ip_publica[16];
extern int erlangSocket; // El controlador va a necesitar saber cuál es el socket de Erlang para responderle

void agregar_fd_en_epoll(int fd, int flags);

void agregar_cliente_en_epoll(ClienteConectado *cliente, int flags);

void modificar_cliente_en_epoll(ClienteConectado *cliente, int flags);

// 3. Exponemos el procesador de buffer para que Sockets.c pueda llamarlo
void procesar_mensajes_en_buffer(ClienteConectado *cliente);

#endif /* __AGENTE_H__ */
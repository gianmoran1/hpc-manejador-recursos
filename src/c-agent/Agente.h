#ifndef __AGENTE_H__
#define __AGENTE_H__

#include <sys/epoll.h>
#include "cliente.h"

//variables globales que otros módulos necesiten leer/usar
extern int epoll_fd;
extern char mi_ip_publica[16];
extern int erlangSocket; // El controlador va a necesitar saber cuál es el socket de Erlang para responderle

/*
 * Añade un file descriptor crudo al epoll.
 * Recibe el file descriptor a monitorear y la máscara de eventos (ej. EPOLLIN).
 */
void agregar_fd_en_epoll(int fd, int flags);

/*
 * Registra un nuevo cliente (o socket de escucha) en el epoll global.
 * Recibe un puntero a la estructura del cliente que contiene el FD y la 
 * máscara de eventos para el epoll (ej. EPOLLIN | EPOLLONESHOT).
 */
void agregar_cliente_en_epoll(ClienteConectado *cliente, int flags);

/*
 * Modifica los eventos monitoreados de un cliente ya existente en el epoll.
 * Utilizado principalmente para rearmar el EPOLLONESHOT tras procesar datos.
 * Recibe el puntero a la estructura del cliente y la nueva máscara de eventos.
 */
void modificar_cliente_en_epoll(ClienteConectado *cliente, int flags);


/*
 * Extrae y procesa todos los mensajes completos (terminados en '\n') del buffer.
 * Corta los mensajes, los envía al controlador según su origen (Erlang o Red C), 
 * y desplaza la memoria restante al principio del buffer.
 * Recibe el puntero al cliente cuyo buffer se va a procesar.
 */
void procesar_mensajes_en_buffer(ClienteConectado *cliente);

#endif /* __AGENTE_H__ */
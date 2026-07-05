#ifndef __CLIENTE_H__
#define __CLIENTE_H__

#include "config.h"

/*
 * ClienteConectado — estado por socket de una conexión TCP activa.
 *
 * Es la capa de transporte del agente: agrupa el file descriptor con su buffer
 * de acumulación de bytes. El loop de epoll guarda un puntero a esta estructura
 * en `epoll_event.data.ptr`, de modo que al despertar un evento ya tiene a mano
 * el fd y los bytes leídos hasta ahora sin buscarlos en ninguna tabla.
 *
 * El buffer acumula lo que va llegando por la red (que puede venir en pedazos)
 * hasta que se detecta un '\n'; recién ahí procesar_mensajes_en_buffer() corta
 * un mensaje completo. El tope de TAM_BUFFER_MENSAJE también actúa como
 * protección anti-DoS: un mensaje sin '\n' que llene el buffer corta la conexión.
 */
typedef struct {
    int fd;                         // El socket del cliente
    int es_erlang;                  // 1 si es el socket local de Erlang, 0 si es un socket de red C
    char buffer[TAM_BUFFER_MENSAJE];// Buffer donde acumulamos los bytes hasta formar un mensaje
    int bytes_leidos;               // Cuántos bytes válidos hay acumulados en 'buffer'
} ClienteConectado;

/*
 * Reserva memoria e inicializa la estructura de un nuevo cliente.
 * Recibe el file descriptor del socket asociado al cliente y un valor de 1
 * si la conexión proviene del nodo local de Erlang, o 0 si es un nodo de red C.
 * Devuelve un puntero a la nueva estructura ClienteConectado inicializada.
 */
ClienteConectado* crear_cliente_conectado(int fd, int es_erlang);

void destruir_cliente(ClienteConectado* cliente);

#endif /* __CLIENTE_H__ */

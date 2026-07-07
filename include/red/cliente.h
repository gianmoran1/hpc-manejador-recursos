#ifndef __CLIENTE_H__
#define __CLIENTE_H__

#include "config.h"
#include <pthread.h>

#define TAM_BUFFER_SALIDA 8192


/*
 * ClienteConectado - estado por socket de una conexión TCP activa.
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
    // El socket del cliente.
    int fd;
    int es_erlang;
    // Buffer donde acumulamos los bytes hasta formar un mensaje recibido.
    char buffer[TAM_BUFFER_MENSAJE];
    // Cuántos bytes válidos hay acumulados en 'buffer'.
    int bytes_leidos;
    // Buffer donde almacenamos los mensajes para enviar.
    char buffer_salida[TAM_BUFFER_SALIDA];
    int bytes_salida;
    pthread_mutex_t lock_salida;
} ClienteConectado;

/*
 * Reserva memoria e inicializa la estructura de un nuevo cliente.
 * Recibe el file descriptor del socket asociado al cliente y un valor de 1
 * si la conexión proviene del nodo local de Erlang, o 0 si es un nodo de red C.
 * Devuelve un puntero a la nueva estructura ClienteConectado inicializada.
 */
ClienteConectado* crear_cliente_conectado(int fd, int es_erlang);

/*
 * Libera la memoria pedida por el cliente. 
 */
void destruir_cliente(ClienteConectado* cliente);

#endif /* __CLIENTE_H__ */

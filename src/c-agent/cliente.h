#ifndef __CLIENTE_H__
#define __CLIENTE_H__

typedef struct {
    int fd;                 // El socket del cliente
    int es_erlang;          // 1 si es el socket local de Erlang, 0 si es un socket de red
    char buffer[1024];      // El buffer donde acumulamos los pedazos de texto
    int bytes_leidos;       // Cuántos bytes llevamos acumulados
} ClienteConectado;

/*
 * Reserva memoria e inicializa la estructura de un nuevo cliente.
 * Recibe el file descriptor del socket asociado al cliente y un valor de 1 
 * si la conexión proviene del nodo local de Erlang, o 0 si es un nodo de red C.
 * Devuelve un puntero a la nueva estructura ClienteConectado inicializada.
 */
ClienteConectado* crear_cliente_conectado(int fd, int es_erlang);


#endif /* __CLIENTE_H__ */
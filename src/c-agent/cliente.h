#ifndef __CLIENTE_H__
#define __CLIENTE_H__

typedef struct {
    int fd;                 // El socket del cliente
    int es_erlang;          // 1 si es el socket local de Erlang, 0 si es un socket de red
    char buffer[1024];      // El buffer donde acumulamos los pedazos de texto
    int bytes_leidos;       // Cuántos bytes llevamos acumulados
} ClienteConectado;


ClienteConectado* crear_cliente_conectado(int fd, int es_erlang);


#endif /* __CLIENTE_H__ */
#include "cliente.h"
#include <stdlib.h>
#include <string.h>

ClienteConectado* crear_cliente_conectado(int fd, int es_erlang) {
    ClienteConectado *cliente = malloc(sizeof(ClienteConectado));
    cliente->fd = fd;
    cliente->es_erlang = es_erlang;
    cliente->bytes_leidos = 0;
    memset(cliente->buffer, 0, sizeof(cliente->buffer));
    return cliente;
}

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "red/cliente.h"

ClienteConectado* crear_cliente_conectado(int fd, int es_erlang) {
    ClienteConectado *cliente = malloc(sizeof(ClienteConectado));
    assert(cliente);
    cliente->fd = fd;
    cliente->es_erlang = es_erlang;
    cliente->bytes_leidos = 0;
    memset(cliente->buffer, 0, sizeof(cliente->buffer)); // Buffers limpio
    memset(cliente->buffer_salida, 0, sizeof(cliente->buffer_salida));
    cliente->bytes_salida = 0;
    pthread_mutex_init(&cliente->lock_salida, NULL);
    return cliente;
}

void destruir_cliente(ClienteConectado* cliente) {
    close(cliente->fd);
    pthread_mutex_destroy(&cliente->lock_salida);
    free(cliente);
}

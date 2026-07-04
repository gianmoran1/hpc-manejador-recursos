#include "red/cliente.h"
#include "controlador.h"
#include <stdlib.h>
#include <string.h>

ClienteConectado* crear_cliente_conectado(int fd, int es_erlang) {
    ClienteConectado *cliente = malloc(sizeof(ClienteConectado));
    cliente->fd = fd;
    cliente->es_erlang = es_erlang;
    cliente->bytes_leidos = 0;
    memset(cliente->buffer, 0, sizeof(cliente->buffer)); // Buffer limpio
    return cliente;
}

// Extrae del buffer todos los mensajes completos (terminados en '\n') y los
// despacha al controlador según el origen. Tras cada mensaje, desplaza el
// remanente al principio del buffer.
void procesar_mensajes_en_buffer(ClienteConectado *cliente) {
    char *salto_linea;

    while ((salto_linea = strchr(cliente->buffer, '\n')) != NULL) {
        // Cortamos el mensaje limpio hasta el '\n'
        int longitud_mensaje = salto_linea - cliente->buffer;
        char mensaje_limpio[TAM_BUFFER_MENSAJE];
        strncpy(mensaje_limpio, cliente->buffer, longitud_mensaje);
        mensaje_limpio[longitud_mensaje] = '\0';

        if (cliente->es_erlang)
            procesar_mensaje_erlang(cliente, mensaje_limpio);
        else
            procesar_mensaje_red_c(cliente, mensaje_limpio);

        // Movemos lo que sobró al principio del buffer
        int bytes_restantes = cliente->bytes_leidos - (longitud_mensaje + 1);
        memmove(cliente->buffer, salto_linea + 1, bytes_restantes);
        cliente->bytes_leidos = bytes_restantes;
        cliente->buffer[cliente->bytes_leidos] = '\0';
    }
}

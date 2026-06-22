#ifndef __CONTROLADOR_H__
#define __CONTROLADOR_H__

#include "cliente.h" // Necesitamos saber qué es un ClienteConectado

void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg);
void procesar_mensaje_red_c(ClienteConectado *cliente, char* msg);

// Callback C-C para notificar GRANTED a nodos coordinadores con reservas encoladas.
// Expuesto para usarlo en Agente.c al manejar desconexiones.
void callback_granted_red(int job_id, int socket_fd);

#endif /* __CONTROLADOR_H__ */
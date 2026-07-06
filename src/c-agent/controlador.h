#ifndef __CONTROLADOR_H__
#define __CONTROLADOR_H__

#include "red/cliente.h"
#include "gestor_estado.h"

extern EstadoGlobal estado;

void controlador_anuncio_recibido(char* buffer_udp);

void controlador_anuncio_recursos();

void controlador_desconexion_cliente(int cliente_fd);

void controlador_mensaje_cliente(ClienteConectado *cliente);


/*
 * Callback asíncrono para el Gestor de Estado.
 * Se dispara cuando se liberan recursos y una petición que estaba encolada 
 * finalmente puede ser satisfecha, enviando el GRANTED al nodo coordinador.
 * Recibe el identificador del trabajo y el file descriptor del nodo a conceder.
 */
void callback_granted_red(int job_id, int socket_fd);

/*
 * Handler del timer de mantenimiento: expira los RESERVE encolados vencidos
 * (anti-deadlock) y desconecta los nodos sin ANNOUNCE reciente.
 */
void controlador_timer(void);

#endif /* __CONTROLADOR_H__ */
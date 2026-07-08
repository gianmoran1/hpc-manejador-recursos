#ifndef __CONTROLADOR_H__
#define __CONTROLADOR_H__

#include "red/cliente.h"
#include "modelo/estado.h"

extern EstadoGlobal estado;

/*
 * Parsea un ANNOUNCE recibido por UDP y, si es válido, registra/actualiza el
 * nodo en la tabla de nodos.
 */
void controlador_anuncio_recibido(char* buffer_udp);

/*
 * Emite por broadcast UDP un ANNOUNCE con los recursos disponibles del nodo.
 */
void controlador_emitir_anuncio();

/*
 * Handler del timer de anuncios: consume el disparo y reemite el ANNOUNCE.
 */
void controlador_anuncio_recursos();

/*
 * Handler del timer de mantenimiento: expira los RESERVE encolados vencidos
 * (anti-deadlock) y desconecta los nodos sin ANNOUNCE reciente.
 */
void controlador_timer_deadlock_y_desconexion(void);

/*
 * Maneja la desconexión de un cliente: libera sus recursos y desvincula su
 * conexión cacheada del registro de nodos.
 */
void controlador_desconexion_cliente(int cliente_fd);

/*
 * Corta y despacha todos los mensajes completos (terminados en '\n') que haya
 * acumulados en el buffer del cliente, según sea Erlang o un agente C.
 */
void controlador_mensaje_cliente(ClienteConectado *cliente);

/*
 * Callback asíncrono para el Gestor de Estado: se dispara cuando se liberan
 * recursos y una petición encolada finalmente puede satisfacerse, enviando el
 * GRANTED al nodo coordinador (job_id + fd del socket a conceder).
 */
void callback_granted_red(int job_id, int socket_fd);

#endif /* __CONTROLADOR_H__ */

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
 * Parsea y ejecuta comandos provenientes de Erlang.
 * Maneja solicitudes locales como GET_NODES, JOB_REQUEST y JOB_RELEASE, 
 * orquestando las peticiones a la red si es necesario.
 * Recibe la estructura del cliente local (Erlang) y la cadena limpia con el comando.
 */
void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg);

/*
 * Parsea y ejecuta comandos provenientes del clúster (Red C).
 * Maneja el protocolo distribuido recibiendo RESERVE, GRANTED, DENIED y RELEASE.
 * Recibe la estructura del nodo remoto que envió el mensaje y la cadena limpia.
 */
void procesar_mensaje_red_c(ClienteConectado *cliente, char* msg);

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
void timer_deadlock_nodos(void);

#endif /* __CONTROLADOR_H__ */
#ifndef __CONTROLADOR_H__
#define __CONTROLADOR_H__

#include "red/cliente.h"
#include "modelo/estado.h"

extern EstadoGlobal estado;

/*
 * Dado un buffer con el contenido del mensaje announce emitido por UDP, la
 * funcion parsea el mensaje y en caso de ser correcto, agrega el nodo a la
 * tabla de nodos. 
 */
void controlador_anuncio_recibido(char* buffer_udp);

/**
 * Emite por broadcast UDP un ANNOUNCE con los recursos disponibles del nodo. 
 */
void controlador_emitir_anuncio();

/**
 * La funcion emite el anuncio de recursos de manera periódica segun el tiempo
 * indicado por el timer de anuncios.
 */
void controlador_anuncio_recursos();

/*
 * Handler del timer de mantenimiento: expira los RESERVE encolados vencidos
 * (anti-deadlock) y desconecta los nodos sin ANNOUNCE reciente.
 */
void controlador_timer_deadlock_y_desconexion(void);




void controlador_desconexion_cliente(int cliente_fd);

void controlador_mensaje_cliente(ClienteConectado *cliente);


/*
 * Callback asíncrono para el Gestor de Estado.
 * Se dispara cuando se liberan recursos y una petición que estaba encolada 
 * finalmente puede ser satisfecha, enviando el GRANTED al nodo coordinador.
 * Recibe el identificador del trabajo y el file descriptor del nodo a conceder.
 */
void callback_granted_red(int job_id, int socket_fd);

#endif /* __CONTROLADOR_H__ */
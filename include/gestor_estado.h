#ifndef __GESTOR_ESTADO_H__
#define __GESTOR_ESTADO_H__

#include "red/cliente.h"
#include "modelo/peticiones.h"
#include "modelo/estado.h"

/*
 * Registra/actualiza un nodo en la tabla de nodos a partir de un ANNOUNCE.
 */
void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu,
                            int gpu, int mem);

/*
 * Escribe en *cpu, *gpu y *mem las unidades actualmente disponibles de cada
 * recurso local.
 */
void gestor_recursos_disponibles(EstadoGlobal estado, int *cpu, int *gpu, int *mem);

/*
 * Maneja un RESERVE <job_id> <recurso> <cantidad>.
 * Devuelve 1 si se concedió (GRANTED), 0 si quedó encolado, -1 si se rechazó
 * (DENIED).
 */
int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id,
    int socket_origen, int cantidad);

/*
 * Maneja un RELEASE <job_id> <recurso> <cantidad>: devuelve el recurso y
 * reparte a los pedidos encolados. avisar_red(job_id, socket_origen) se llama
 * por cada pedido que pasa a concedido, para mandarle el GRANTED por TCP.
 */
void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id,
    int socket_origen, int cantidad, void (*avisar_red)(int, int));

/*
 * Higiene de colas locales: desencola (en silencio) los RESERVE encolados que
 * superaron TIEMPO_ESPERA_RESERVA. No notifica a nadie; el reintento lo dispara
 * el timeout del coordinador (gestor_expirar_peticiones).
 */
void gestor_expirar_pedidos(EstadoGlobal estado);

/*
 * Lado coordinador: expira las PeticionMulti que superaron TIEMPO_ESPERA_RESERVA
 * sin completarse (respondidos < total). Por cada una llama cb_timeout(p) —para
 * mandar RELEASE a sus nodos y JOB_TIMEOUT a Erlang— y luego la destruye.
 */
void gestor_expirar_peticiones(EstadoGlobal estado, void (*cb_timeout)(PeticionMulti));

/*
 * Maneja la desconexión de un socket liberando todos los recursos asociados y
 * repartiéndolos a los encolados vía avisar_red.
 */
void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, void (*avisar_red)(int, int));

/*
 * Arma la respuesta a GET_NODES ("NODES IP:puerto:cpu:X:mem:Y:gpu:Z[;...]").
 * El caller debe hacer free() sobre el puntero devuelto.
 */
char* gestor_get_nodes(EstadoGlobal estado);

/*
 * Elimina del registro los nodos sin ANNOUNCE reciente. Pensada para el timer
 * de mantenimiento.
 */
void gestor_desconectar_nodos(EstadoGlobal estado);

/*
 * Inserta la peticion en la tabla de pendientes (toma el lock internamente).
 */
void gestor_registrar_peticion(EstadoGlobal estado, PeticionMulti p);

/*
 * Busca una peticion por job_id. LLAMAR CON estado->lock YA TOMADO.
 */
PeticionMulti gestor_buscar_peticion(EstadoGlobal estado, int job_id);

/*
 * Elimina y libera la peticion con ese job_id. LLAMAR CON estado->lock YA TOMADO.
 */
void gestor_eliminar_peticion(EstadoGlobal estado, int job_id);

/*
 * Lookup atómico: escribe el puerto del nodo y el fd de su conexión cacheada
 * (o -1 si no tiene) en los punteros de salida. Devuelve 1 si el nodo existe.
 */
int gestor_obtener_destino(EstadoGlobal estado, char* ip, int* puerto_out, int* fd_cacheado_out);

/*
 * Cachea una conexión saliente ya establecida para el nodo (ip, puerto).
 */
void gestor_registrar_conexion(EstadoGlobal estado, char* ip,
    ClienteConectado* cliente);

/*
 * Desvincula (conexion = NULL) la conexión cacheada que tenga ese fd.
 */
void gestor_limpiar_conexion_por_fd(EstadoGlobal estado, int fd);

#endif /* __GESTOR_ESTADO_H__ */

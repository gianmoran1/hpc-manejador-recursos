#ifndef __GESTOR_ESTADO_H__
#define __GESTOR_ESTADO_H__

#include "modelo/peticiones.h"
#include "modelo/estado.h"

/*
 * Agrega un nodo dado a la tabla de nodos. 
 */
void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu, 
                            int gpu, int mem);

/* 
 * Escribe en *cpu, *gpu y *mem las unidades actualmente disponibles de cada
 * recurso local.
 */
void gestor_recursos_disponibles(EstadoGlobal estado, int *cpu, int *gpu, int *mem);

/*RESERVE <job_id> <recurso> <cantidad>*/
/*maneja una reserva de recurso (reserve), 1 es GRANTED, 0 es encolado, -1 es DENIED*/
int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id, 
    int socket_origen, int cantidad);

/*RELEASE <job_id> <recurso> <cantidad>*/
/*maneja la liberación de un recurso (release), Requiere pasar una función de callback (void cb_red(int job_id, int socket_origen)
para enviar de forma asíncrona el mensaje GRANTED por TCP a los sockets que correspondan*/
void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id, 
    int cantidad, void (*avisar_red)(int, int));

void gestor_liberar_job(EstadoGlobal estado, int job_id, void (*avisar_red)(int, int));
/* Higiene de colas locales: desencola (en silencio) los RESERVE encolados que
 * superaron TIEMPO_ESPERA_RESERVA. No notifica a nadie; el reintento lo dispara
 * el timeout del coordinador (gestor_expirar_peticiones). */
void gestor_expirar_pedidos(EstadoGlobal estado);

/* Lado coordinador: expira las PeticionMulti que superaron TIEMPO_ESPERA_RESERVA
 * sin completarse (respondidos < total). Por cada una llama cb_timeout(p) —para
 * mandar RELEASE a sus nodos y JOB_TIMEOUT a Erlang— y luego la destruye. */
void gestor_expirar_peticiones(EstadoGlobal estado, void (*cb_timeout)(PeticionMulti));

/*maneja la desconexión de un socket, liberando todos los recursos asociados a ese socket y avisando a los clientes afectados */
void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, void (*avisar_red)(int, int));

/*obtiene la lista de nodos registrados (GET_NODES) NODES IP:Puerto:cpu:X...
SE DEBE HACER FREE SOBRE EL PUNTERO DEVUELTO*/
char* gestor_get_nodes(EstadoGlobal estado);

/*desconecta todos los nodos registrados
tambien a ser llamada en el loop, puesto que desconecta al que no haya hecho su annunce */
void gestor_desconectar_nodos(EstadoGlobal estado);

/* Inserta la peticion en la lista (toma el lock internamente) */
void gestor_registrar_peticion(EstadoGlobal estado, PeticionMulti p);

/* Busca por job_id. LLAMAR CON estado->lock YA TOMADO. */
PeticionMulti gestor_buscar_peticion(EstadoGlobal estado, int job_id);

/* Elimina y libera la peticion con ese job_id. LLAMAR CON estado->lock YA TOMADO. */
void gestor_eliminar_peticion(EstadoGlobal estado, int job_id);

/* Lookup atómico: escribe el puerto del nodo y el fd de su conexión cacheada
 * (o -1 si no tiene) en los punteros de salida. Devuelve 1 si el nodo existe. */
int gestor_obtener_destino(EstadoGlobal estado, char* ip, int* puerto_out, int* fd_cacheado_out);

/* Cachea una conexión saliente ya establecida para el nodo (ip, puerto). */
void gestor_registrar_conexion(EstadoGlobal estado, char* ip, int puerto, 
    ClienteConectado* cliente);

/* Desvincula (conexion = NULL) la conexión cacheada que tenga ese fd. */
void gestor_limpiar_conexion_por_fd(EstadoGlobal estado, int fd);


#endif /* __GESTOR_ESTADO_H__ */

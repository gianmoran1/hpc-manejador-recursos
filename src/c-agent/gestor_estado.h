#ifndef __GESTOR_ESTADO_H__
#define __GESTOR_ESTADO_H__

/*INTERFAZ PRINCIPAL DEL GESTOR DE ESTADO, PARA USAR EN EL LOOP DE EPOLL*/

#include "recursos.h"
#include "jobs.h"
#include "nodos.h"
#include "transacciones.h"
#include "tablahash.h"
#include <pthread.h>

typedef struct estadoGlobal_ {
    RecursoLocal cpu;
    RecursoLocal gpu;
    RecursoLocal mem;
    TablaJobs libro_contable;
    TablaNodos registro_nodos;
    TablaHash peticiones_pendientes;
    pthread_mutex_t lock;
} *EstadoGlobal;


/*crea un estado global*/
EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem);
/*destruye un estado global*/
void estado_destruir(EstadoGlobal estado);

/*RESERVE <job_id> <recurso> <cantidad>*/
/*maneja una reserva de recurso (reserve), 1 es GRANTED, 0 es encolado, -1 es DENIED*/
int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id, int socket_origen, int cantidad);

/*RELEASE <job_id> <recurso> <cantidad>*/
/*maneja la liberación de un recurso (release), Requiere pasar una función de callback (void cb_red(int job_id, int socket_origen)
para enviar de forma asíncrona el mensaje GRANTED por TCP a los sockets que correspondan*/
void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id, int cantidad, void (*avisar_red)(int, int));
void gestor_liberar_job(EstadoGlobal estado, int job_id, void (*avisar_red)(int, int));
void gestor_expirar_pedidos(EstadoGlobal estado, void (*avisar_timeout)(int, int));

/*maneja la desconexión de un socket, liberando todos los recursos asociados a ese socket y avisando a los clientes afectados */
void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, void (*avisar_red)(int, int));

/*procesa un anuncio de nodo (annunce) ANNOUNCE <puerto> <recursos>*/
void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu, int gpu, int mem);

/*obtiene la lista de nodos registrados (GET_NODES) NODES IP:Puerto:cpu:X...
SE DEBE HACER FREE SOBRE EL PUNTERO DEVUELTO*/
char* gestor_get_nodes(EstadoGlobal estado);

/*desconecta todos los nodos registrados
tambien a ser llamada en el loop, puesto que desconecta al que no haya hecho su annunce */
void gestor_desconectar_nodos(EstadoGlobal estado);

/* ── Gestión de peticiones multi-recurso ────────────────────────────────── */

/* Inserta la peticion en la lista (toma el lock internamente) */
void gestor_registrar_peticion(EstadoGlobal estado, PeticionMulti p);

/* Busca por job_id. LLAMAR CON estado->lock YA TOMADO. */
PeticionMulti gestor_buscar_peticion(EstadoGlobal estado, int job_id);

/* Elimina y libera la peticion con ese job_id. LLAMAR CON estado->lock YA TOMADO. */
void gestor_eliminar_peticion(EstadoGlobal estado, int job_id);

Nodo gestor_buscar_nodo_por_ip(char* ip, EstadoGlobal estado);


#endif /* __GESTOR_ESTADO_H__ */
#ifndef __JOBS_H__
#define __JOBS_H__

#include "estructuras/tablahash.h"
#include "estructuras/glist.h"

// La estructura de la deuda de un trabajo
typedef struct jobActivo_ {
    int job_id;
    int socket_origen;
    int cpu_usada;
    int gpu_usada;
    int mem_usada;
} *JobActivo;

typedef struct tablaJobs_ {
    TablaHash tabla; 
    GList lista;    
} *TablaJobs;

/**
 * Crea y devuelve una tabla de trabajos vacía.
 */
TablaJobs crear_tabla_jobs();

/**
 * Destruye la tabla de trabajos completa. Libera cada JobActivo a través de
 * la tabla hash (que es la dueña real de la memoria) y recién después libera
 * los nodos de la lista con un destructor no-op, para no liberar el mismo
 * JobActivo dos veces.
 */
void destruir_tabla_jobs(TablaJobs tabla_jobs);

/**
 * Registra que el job job_id tiene asignadas cantidad unidades de recurso,
 * pedidas desde socket. Si el job ya existía en la tabla, suma la cantidad
 * al campo correspondiente (permite acumular reservas de varios recursos, o
 * repetidas del mismo, para el mismo job_id). Si es la primera vez que se ve
 * este job_id, crea la ficha nueva con socket_origen = socket.
 */
void registrar_asignacion(TablaJobs tabla_jobs, int job_id, int socket,
    char* recurso, int cantidad);

/**
 * Descuenta cantidad unidades de recurso de lo que job_id tenía asignado.
 * Devuelve la cantidad efectivamente liberada, que nunca supera lo que el
 * job tenía registrado (un release excesivo no deja saldo negativo).
 * Si tras liberar el job queda en (cpu, gpu, mem) == (0, 0, 0), se elimina
 * de la tabla y de la lista. Devuelve 0 si job_id no existe en la tabla o
 * si recurso no es "cpu"/"gpu"/"mem".
 */
int registrar_liberacion(TablaJobs tabla_jobs, int job_id, char* recurso,
    int cantidad);

/**
 * Libera todos los recursos de los jobs cuyo socket_origen sea socket_caido
 * (pensado para cuando ese socket se desconecta inesperadamente). Acumula
 * los totales liberados por recurso y dispara
 * liberar_recurso_cb("cpu"|"gpu"|"mem", total) una vez por cada recurso con
 * total > 0, para que el caller redistribuya esos recursos a la cola de
 * pendientes correspondiente.
 */
void liberar_recursos_socket(TablaJobs tabla_jobs, int socket_caido,
    void (*liberar_recurso_cb)(char*, int));

#endif /* __JOBS_H__ */
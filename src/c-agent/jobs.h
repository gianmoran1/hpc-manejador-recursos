#ifndef __JOBS_H__
#define __JOBS_H__

#include "./estructuras/tablahash.h"
#include "./estructuras/glist.h"

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

/*crea una tabla de trabajos vacía */
TablaJobs crear_tabla_jobs();
/*destruye una tabla de trabajos */
void destruir_tabla_jobs(TablaJobs tabla_jobs);

/*registra la asignación de un recurso a un trabajo */
void registrar_asignacion(TablaJobs tabla_jobs, int job_id, int socket, char* recurso, int cantidad);
/*registra la liberación de un recurso de un trabajo */
int registrar_liberacion(TablaJobs tabla_jobs, int job_id, char* recurso, int cantidad);
/*libera los recursos asociados a un socket caído */
void liberar_recursos_socket(TablaJobs tabla_jobs, int socket_caido, void (*liberar_recurso_cb)(char*, int));

#endif /* __JOBS_H__ */
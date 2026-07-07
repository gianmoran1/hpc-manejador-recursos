#include "jobs.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void* job_no_copia(void* dato) {
    return dato;
}

static void job_destruir(void* dato) {
    free(dato);
}

static int job_comparar(void* dato1, void* dato2) {
    return (((JobActivo)dato1)->job_id - ((JobActivo)dato2)->job_id);
}

static unsigned job_hash(void* dato) {
    return (unsigned)((JobActivo)dato)->job_id;
}

// No-op a propósito: lista y tabla hash comparten los mismos punteros JobActivo.
// La dueña de esa memoria es la tabla hash; si este destructor de la lista
// liberara el JobActivo sería un double free.
static void no_destruye_job(__attribute__((unused)) void* dato) {
}

TablaJobs crear_tabla_jobs() {
    TablaJobs tj = malloc(sizeof(struct tablaJobs_));
    assert(tj);
    tj->tabla = tablahash_crear(TAM_INICIAL_TABLA_HASH, job_no_copia, 
        job_comparar, job_destruir, job_hash);
    tj->lista = glist_crear();
    return tj;
}

void destruir_tabla_jobs(TablaJobs tabla_jobs) {
    tablahash_destruir(tabla_jobs->tabla);
    glist_destruir(tabla_jobs->lista, no_destruye_job);
    free(tabla_jobs);
}

void registrar_asignacion(TablaJobs tabla_jobs, int job_id, int socket, 
                            char* recurso, int cantidad) {
    struct jobActivo_ busqueda;
    busqueda.job_id = job_id;

    // Buscamos si el trabajo ya nos debía algo antes
    JobActivo job = (JobActivo)tablahash_buscar(tabla_jobs->tabla, &busqueda);

    if (job == NULL) {
        // Primera vez que este trabajo pide algo, lo creamos
        job = malloc(sizeof(struct jobActivo_));
        job->job_id = job_id;
        job->socket_origen = socket;
        job->cpu_usada = 0;
        job->gpu_usada = 0;
        job->mem_usada = 0;

        tablahash_insertar(tabla_jobs->tabla, job);
        tabla_jobs->lista = glist_agregar_inicio(tabla_jobs->lista, job, 
                                                job_no_copia);
    }

    // Asignamos los recursos
    if (strcmp(recurso, RECURSO_CPU) == 0) job->cpu_usada += cantidad;
    else if (strcmp(recurso, RECURSO_GPU) == 0) job->gpu_usada += cantidad;
    else if (strcmp(recurso, RECURSO_MEM) == 0) job->mem_usada += cantidad;
}

int registrar_liberacion(TablaJobs tabla_jobs, int job_id, char* recurso, 
                        int cantidad) {
    struct jobActivo_ busqueda;
    busqueda.job_id = job_id;

    JobActivo job = (JobActivo)tablahash_buscar(tabla_jobs->tabla, &busqueda);
    if (job == NULL) return 0; // No existe el job

    int liberado = 0;
    if (strcmp(recurso, RECURSO_CPU) == 0) {
        liberado = (cantidad > job->cpu_usada) ? job->cpu_usada : cantidad;
        job->cpu_usada -= liberado;
    } else if (strcmp(recurso, RECURSO_GPU) == 0) {
        liberado = (cantidad > job->gpu_usada) ? job->gpu_usada : cantidad;
        job->gpu_usada -= liberado;
    } else if (strcmp(recurso, RECURSO_MEM) == 0) {
        liberado = (cantidad > job->mem_usada) ? job->mem_usada : cantidad;
        job->mem_usada -= liberado;
    } else {
        return 0;
    }

    // Si el trabajo ya no consume recursos lo eliminamos.
    if (job->cpu_usada == 0 && job->gpu_usada == 0 && job->mem_usada == 0) {
        // Primero desenlazamos el nodo de la lista (solo libera el GNode,
        // no el JobActivo) y recién después tablahash_eliminar libera el
        // JobActivo. Si se invierte el orden, el puntero job de la lista
        // queda colgando antes de poder compararlo.
        GList temp = tabla_jobs->lista;
        if (temp != NULL) {
            if (temp->data == job) {
                tabla_jobs->lista = temp->next;
                free(temp);
            } else {
                while (temp->next != NULL && temp->next->data != job) {
                    temp = temp->next;
                }
                if (temp->next != NULL) {
                    GList borrar = temp->next;
                    temp->next = temp->next->next;
                    free(borrar);
                }
            }
        }
        tablahash_eliminar(tabla_jobs->tabla, job);
    }
    return liberado;
}

void liberar_recursos_socket(TablaJobs tabla_jobs, int socket_caido, 
                                void (*liberar_recurso_cb)(char*, int)) {
    GList temp = tabla_jobs->lista;
    if (temp == NULL) return;

    int total_cpu_liberada = 0;
    int total_gpu_liberada = 0;
    int total_mem_liberada = 0;

    // Liberamos los trabajos que pertenezcan al socket caido
    while (temp != NULL) {
        JobActivo job = (JobActivo)temp->data;
        if (job->socket_origen == socket_caido) {
            GList next = temp->next;

            total_cpu_liberada += job->cpu_usada;
            total_gpu_liberada += job->gpu_usada;
            total_mem_liberada += job->mem_usada;

            tablahash_eliminar(tabla_jobs->tabla, job); // Esto libera la memoria del job
            free(temp); // Esto libera la memoria del nodo de la lista
            temp = next;
            tabla_jobs->lista = temp;
        } else {
            break;
        }
    }

    // Limpiamos el resto de la lista (solo si quedaron elementos)
    if (temp != NULL) {
        while (temp->next != NULL) {
            JobActivo job = (JobActivo)temp->next->data;
            if (job->socket_origen == socket_caido) {
                GList next = temp->next->next;

                total_cpu_liberada += job->cpu_usada;
                total_gpu_liberada += job->gpu_usada;
                total_mem_liberada += job->mem_usada;

                // Tambien la eliminamos de la tabla.
                tablahash_eliminar(tabla_jobs->tabla, job);
                free(temp->next);
                temp->next = next;
            } else {
                temp = temp->next;
            }
        }
    }

    // Disparamos los callbacks con el total acumulado.
    if (total_cpu_liberada > 0) liberar_recurso_cb(RECURSO_CPU, total_cpu_liberada);
    if (total_gpu_liberada > 0) liberar_recurso_cb(RECURSO_GPU, total_gpu_liberada);
    if (total_mem_liberada > 0) liberar_recurso_cb(RECURSO_MEM, total_mem_liberada);
}
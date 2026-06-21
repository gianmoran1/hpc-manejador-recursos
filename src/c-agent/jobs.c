#include "jobs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


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

static void no_destruye_job(__attribute__((unused)) void* dato) {
    // Para la limpieza de la lista sin romper el hash
}

// -----------------------------------------------------------------

TablaJobs crear_tabla_jobs() {
    TablaJobs tj = malloc(sizeof(struct tablaJobs_));
    tj->tabla = tablahash_crear(100, job_no_copia, job_comparar, job_destruir, job_hash);
    tj->lista = glist_crear();
    return tj;
}

void destruir_tabla_jobs(TablaJobs tabla_jobs) {
    tablahash_destruir(tabla_jobs->tabla);
    glist_destruir(tabla_jobs->lista, no_destruye_job);
    free(tabla_jobs);
}

void registrar_asignacion(TablaJobs tabla_jobs, int job_id, int socket, char* recurso, int cantidad) {
    struct jobActivo_ busqueda;
    busqueda.job_id = job_id;

    // Buscamos si el trabajo ya nos debía algo antes
    JobActivo job = (JobActivo)tablahash_buscar(tabla_jobs->tabla, &busqueda);

    if (job == NULL) {
        // Primera vez que este trabajo pide algo, creamos su ficha
        job = malloc(sizeof(struct jobActivo_));
        job->job_id = job_id;
        job->socket_origen = socket;
        job->cpu_usada = 0;
        job->gpu_usada = 0;
        job->mem_usada = 0;

        tablahash_insertar(tabla_jobs->tabla, job);
        tabla_jobs->lista = glist_agregar_inicio(tabla_jobs->lista, job, job_no_copia);
    }

    // Anotamos la nueva deuda en su cuenta
    if (strcmp(recurso, "cpu") == 0) job->cpu_usada += cantidad;
    else if (strcmp(recurso, "gpu") == 0) job->gpu_usada += cantidad;
    else if (strcmp(recurso, "mem") == 0) job->mem_usada += cantidad;
}

// jobs.c
int registrar_liberacion(TablaJobs tabla_jobs, int job_id, char* recurso, int cantidad) {
    struct jobActivo_ busqueda;
    busqueda.job_id = job_id;

    JobActivo job = (JobActivo)tablahash_buscar(tabla_jobs->tabla, &busqueda);
    if (job == NULL) return 0; // No existe el job

    int liberado = 0;
    if (strcmp(recurso, "cpu") == 0) {
        liberado = (cantidad > job->cpu_usada) ? job->cpu_usada : cantidad;
        job->cpu_usada -= liberado;
    } else if (strcmp(recurso, "gpu") == 0) {
        liberado = (cantidad > job->gpu_usada) ? job->gpu_usada : cantidad;
        job->gpu_usada -= liberado;
    } else if (strcmp(recurso, "mem") == 0) {
        liberado = (cantidad > job->mem_usada) ? job->mem_usada : cantidad;
        job->mem_usada -= liberado;
    } else {
        return 0;
    }

    // Si el trabajo quedó sin deudas, eliminarlo
    if (job->cpu_usada == 0 && job->gpu_usada == 0 && job->mem_usada == 0) {
        // ... (código existente para eliminar de la lista)
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


// NUESTRO ESCUDO CONTRA TRAGEDIAS (Refactorizado para concurrencia segura)
void liberar_recursos_socket(TablaJobs tabla_jobs, int socket_caido, void (*liberar_recurso_cb)(char*, int)) {
    GList temp = tabla_jobs->lista;
    if (temp == NULL) return;

    // Acumuladores para guardar el botín antes de repartirlo
    int total_cpu_liberada = 0;
    int total_gpu_liberada = 0;
    int total_mem_liberada = 0;

    // ==========================================
    // FASE 1: RECOLECCIÓN Y LIMPIEZA
    // ==========================================
    
    // 1. Limpiamos las cabezas que pertenezcan al socket caído
    while (temp != NULL) {
        JobActivo job = (JobActivo)temp->data;
        if (job->socket_origen == socket_caido) {
            GList next = temp->next;

            // En lugar de llamar al callback, guardamos el oro en los bolsillos
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

    // 2. Limpiamos el resto de la lista (solo si quedaron elementos)
    if (temp != NULL) {
        while (temp->next != NULL) {
            JobActivo job = (JobActivo)temp->next->data;
            if (job->socket_origen == socket_caido) {
                GList next = temp->next->next;

                // Acumulamos el oro
                total_cpu_liberada += job->cpu_usada;
                total_gpu_liberada += job->gpu_usada;
                total_mem_liberada += job->mem_usada;

                tablahash_eliminar(tabla_jobs->tabla, job);
                free(temp->next);
                temp->next = next;
            } else {
                temp = temp->next;
            }
        }
    }

    // ==========================================
    // FASE 2: REPARTICIÓN (Totalmente segura)
    // ==========================================
    
    // Ahora que la lista está intacta y nadie la está tocando, 
    // disparamos los callbacks con el total acumulado.
    if (total_cpu_liberada > 0) liberar_recurso_cb("cpu", total_cpu_liberada);
    if (total_gpu_liberada > 0) liberar_recurso_cb("gpu", total_gpu_liberada);
    if (total_mem_liberada > 0) liberar_recurso_cb("mem", total_mem_liberada);
}
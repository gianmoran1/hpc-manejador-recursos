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

void registrar_liberacion(TablaJobs tabla_jobs, int job_id, char* recurso, int cantidad) {
    struct jobActivo_ busqueda;
    busqueda.job_id = job_id;

    JobActivo job = (JobActivo)tablahash_buscar(tabla_jobs->tabla, &busqueda);
    if (job == NULL) return; // Escudo por si mandan un RELEASE de algo inexistente

    if (strcmp(recurso, "cpu") == 0) job->cpu_usada -= cantidad;
    else if (strcmp(recurso, "gpu") == 0) job->gpu_usada -= cantidad;
    else if (strcmp(recurso, "mem") == 0) job->mem_usada -= cantidad;

    // Si el trabajo ya pagó todas sus deudas, lo borramos por completo
    if (job->cpu_usada == 0 && job->gpu_usada == 0 && job->mem_usada == 0) {
        tablahash_eliminar(tabla_jobs->tabla, job);

        // Lo desvinculamos de tu lista iteradora (Estilo C clásico)
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
    }
}

// NUESTRO ESCUDO CONTRA TRAGEDIAS
// Recorre la lista y, si el socket coincide, devuelve los recursos mediante el callback
void liberar_recursos_socket(TablaJobs tabla_jobs, int socket_caido, void (*liberar_recurso_cb)(char*, int)) {
    GList temp = tabla_jobs->lista;
    if (temp == NULL) return;

    // 1. Limpiamos las cabezas que pertenezcan al socket caído
    while (temp != NULL) {
        JobActivo job = (JobActivo)temp->data;
        if (job->socket_origen == socket_caido) {
            GList next = temp->next;

            // Usamos el callback para avisarle al tesorero que recupere el oro
            if (job->cpu_usada > 0) liberar_recurso_cb("cpu", job->cpu_usada);
            if (job->gpu_usada > 0) liberar_recurso_cb("gpu", job->gpu_usada);
            if (job->mem_usada > 0) liberar_recurso_cb("mem", job->mem_usada);

            tablahash_eliminar(tabla_jobs->tabla, job);
            free(temp);
            temp = next;
            tabla_jobs->lista = temp;
        } else {
            break;
        }
    }

    if (temp == NULL) return;

    // 2. Limpiamos el resto de la lista
    while (temp->next != NULL) {
        JobActivo job = (JobActivo)temp->next->data;
        if (job->socket_origen == socket_caido) {
            GList next = temp->next->next;

            if (job->cpu_usada > 0) liberar_recurso_cb("cpu", job->cpu_usada);
            if (job->gpu_usada > 0) liberar_recurso_cb("gpu", job->gpu_usada);
            if (job->mem_usada > 0) liberar_recurso_cb("mem", job->mem_usada);

            tablahash_eliminar(tabla_jobs->tabla, job);
            free(temp->next);
            temp->next = next;
        } else {
            temp = temp->next;
        }
    }
}
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "gestor_estado.h"
#include "config.h"
#include "modelo/recursos.h"
#include "modelo/jobs.h"
#include "modelo/nodos.h"
#include "modelo/peticiones.h"
#include "estructuras/tablahash.h"

// Contexto para recolectar, bajo lock, los job_id de peticiones vencidas.
struct ctx_expira_peticion { time_t ahora; int *vencidos; int n; };

// Puente para el callback_tragedia: liberar_recursos_socket solo pasa
// (recurso, cantidad), así que el estado y la función de aviso de red se
// comparten por acá mientras dura manejar_desconexion_socket (bajo lock).
static EstadoGlobal estado_actual = NULL;
static void (*aviso_red_actual)(int, int) = NULL;

static void* no_copia_solicitud(void* dato);
static void destruir_solicitud(void* dato);
static RecursoLocal obtener_recurso(EstadoGlobal estado, char* nombre);
static void manejar_release_aux(EstadoGlobal estado, char* nombre_recurso,
    int job_id, int cantidad, void (*avisar_red)(int, int));
static void visitar_peticion_vencida(void *dato, void *extra);
static void callback_tragedia(char* nombre_recurso, int cantidad);

void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu,
                            int gpu, int mem) {
    pthread_mutex_lock(&estado->lock);
    nodos_procesar_anuncio(estado->registro_nodos, ip, puerto, cpu, gpu, mem);
    pthread_mutex_unlock(&estado->lock);
}

void gestor_recursos_disponibles(EstadoGlobal estado, int *cpu, int *gpu, int *mem) {
    pthread_mutex_lock(&estado->lock);
    *cpu = estado->cpu->disponible;
    *gpu = estado->gpu->disponible;
    *mem = estado->mem->disponible;
    pthread_mutex_unlock(&estado->lock);
}

int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id,
                            int socket_origen, int cantidad) {
    pthread_mutex_lock(&estado->lock);
    int resultado = -1; // DENIED por defecto
    RecursoLocal rec = obtener_recurso(estado, nombre_recurso);

    if (rec != NULL && cantidad > 0 && cantidad <= rec->capacidad_total) {
        if (rec->disponible >= cantidad && cola_es_vacia(rec->pendientes)) {
            rec->disponible -= cantidad;
            registrar_asignacion(estado->libro_contable, job_id, socket_origen, nombre_recurso, cantidad);
            resultado = 1; // GRANTED
        } else {
            SolicitudPendiente nueva = malloc(sizeof(struct solicitudPendiente_));
            nueva->job_id = job_id;
            nueva->socket_origen = socket_origen;
            nueva->cantidad_pedida = cantidad;
            nueva->instante_llegada = time(NULL);

            rec->pendientes = cola_encolar(rec->pendientes, nueva, no_copia_solicitud);
            resultado = 0; // Encolado
        }
    }

    pthread_mutex_unlock(&estado->lock);
    return resultado;
}

void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id,
                            int cantidad, void (*avisar_red)(int, int)) {
    pthread_mutex_lock(&estado->lock);
    manejar_release_aux(estado, nombre_recurso, job_id, cantidad, avisar_red);
    pthread_mutex_unlock(&estado->lock);
}

void gestor_expirar_pedidos(EstadoGlobal estado) {
    pthread_mutex_lock(&estado->lock);
    time_t ahora = time(NULL);
    RecursoLocal recursos[] = {estado->cpu, estado->gpu, estado->mem};

    for (int i = 0; i < 3; i++) {
        RecursoLocal rec = recursos[i];
        int termine = 0;

        if (rec ==  NULL) continue;
        while (!cola_es_vacia(rec->pendientes) && !termine) {
            SolicitudPendiente solicitud =
                (SolicitudPendiente)cola_inicio(rec->pendientes, no_copia_solicitud);
            if (!solicitud) break;

            if (difftime(ahora, solicitud->instante_llegada) >= TIEMPO_ESPERA_RESERVA) {
                // Desencolar en silencio: el reintento lo dispara el timeout del
                // coordinador (gestor_expirar_peticiones), no el dueño del recurso.
                printf("[TIMEOUT] job %d caducó encolado en %s (desencolado).\n",
                    solicitud->job_id, rec->nombre);
                rec->pendientes = cola_desencolar(rec->pendientes, destruir_solicitud);
            } else {
                termine = 1;
            }
        }
    }
    pthread_mutex_unlock(&estado->lock);
}

void gestor_expirar_peticiones(EstadoGlobal estado, void (*cb_timeout)(PeticionMulti)) {
    pthread_mutex_lock(&estado->lock);
    int cap = tablahash_nelems(estado->peticiones_pendientes);
    if (cap > 0) {
        // Recolectamos primero: no se puede eliminar durante el recorrido.
        int *vencidos = malloc(cap * sizeof(int));
        struct ctx_expira_peticion ctx = { time(NULL), vencidos, 0 };
        tablahash_recorrer(estado->peticiones_pendientes, visitar_peticion_vencida, &ctx);

        for (int i = 0; i < ctx.n; i++) {
            struct peticionMulti_ busq;
            busq.job_id = vencidos[i];
            PeticionMulti p = tablahash_buscar(estado->peticiones_pendientes, &busq);
            if (p) {
                if (cb_timeout) cb_timeout(p);   // RELEASE a los nodos + JOB_TIMEOUT
                tablahash_eliminar(estado->peticiones_pendientes, &busq);
            }
        }
        free(vencidos);
    }
    pthread_mutex_unlock(&estado->lock);
}

void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido,
                                void (*avisar_red)(int, int)) {
    pthread_mutex_lock(&estado->lock);
    estado_actual = estado;
    aviso_red_actual = avisar_red;
    liberar_recursos_socket(estado->libro_contable, socket_caido, callback_tragedia);
    estado_actual = NULL;
    aviso_red_actual = NULL;
    pthread_mutex_unlock(&estado->lock);
}

char* gestor_get_nodes(EstadoGlobal estado) {
    pthread_mutex_lock(&estado->lock);
    char* resultado = get_nodes(estado->registro_nodos);
    pthread_mutex_unlock(&estado->lock);
    return resultado;
}

void gestor_desconectar_nodos(EstadoGlobal estado) {
    pthread_mutex_lock(&estado->lock);
    // Desconecta tras 15 segundos.
    gestor_desconectar_nodos_timeout(estado->registro_nodos);
    pthread_mutex_unlock(&estado->lock);
}

void gestor_registrar_peticion(EstadoGlobal estado, PeticionMulti p) {
    pthread_mutex_lock(&estado->lock);
    tablahash_insertar(estado->peticiones_pendientes, p);
    pthread_mutex_unlock(&estado->lock);
}

PeticionMulti gestor_buscar_peticion(EstadoGlobal estado, int job_id) {
    struct peticionMulti_ busqueda;
    busqueda.job_id = job_id;
    return (PeticionMulti)tablahash_buscar(estado->peticiones_pendientes, &busqueda);
}

void gestor_eliminar_peticion(EstadoGlobal estado, int job_id) {
    struct peticionMulti_ busqueda;
    busqueda.job_id = job_id;
    tablahash_eliminar(estado->peticiones_pendientes, &busqueda);
}

int gestor_obtener_destino(EstadoGlobal estado, char* ip, int* puerto_out, int* fd_cacheado_out) {
    pthread_mutex_lock(&estado->lock);
    Nodo n = buscar_nodo_por_ip(ip, estado->registro_nodos);
    int encontrado = (n != NULL);
    if (encontrado) {
        *puerto_out = n->puerto;
        *fd_cacheado_out = (n->conexion != NULL) ? n->conexion->fd : -1;
    }
    pthread_mutex_unlock(&estado->lock);
    return encontrado;
}

void gestor_registrar_conexion(EstadoGlobal estado, char* ip, int puerto, ClienteConectado* cliente) {
    pthread_mutex_lock(&estado->lock);
    nodo_registrar_conexion(ip, puerto, cliente, estado->registro_nodos);
    pthread_mutex_unlock(&estado->lock);
}

void gestor_limpiar_conexion_por_fd(EstadoGlobal estado, int fd) {
    pthread_mutex_lock(&estado->lock);
    nodo_limpiar_conexion_por_fd(fd, estado->registro_nodos);
    pthread_mutex_unlock(&estado->lock);
}

// Callbacks para la Cola de pendientes de cada RecursoLocal.

static void* no_copia_solicitud(void* dato) { return dato; }
static void destruir_solicitud(void* dato) { free(dato); }

static RecursoLocal obtener_recurso(EstadoGlobal estado, char* nombre) {
    if (strcmp(nombre, RECURSO_CPU) == 0) return estado->cpu;
    if (strcmp(nombre, RECURSO_GPU) == 0) return estado->gpu;
    if (strcmp(nombre, RECURSO_MEM) == 0) return estado->mem;
    return NULL;
}

// Núcleo del release (asume estado->lock tomado): devuelve lo liberado al
// recurso y reparte a los que estaban encolados mientras alcance.
static void manejar_release_aux(EstadoGlobal estado, char* nombre_recurso,
                        int job_id, int cantidad, void (*avisar_red)(int, int)) {
    RecursoLocal rec = obtener_recurso(estado, nombre_recurso);
    if (!rec || cantidad <= 0) return;

    // Liberar solo lo que realmente tiene asignado en el libro contable
    int liberado = registrar_liberacion(estado->libro_contable, job_id,
                                        nombre_recurso, cantidad);
    if (liberado == 0) return; // Nada que liberar

    // Devuelvo los recursos
    rec->disponible += liberado;

    // Revisamos la cola de este recurso específico y repartimos al siguiente en espera
    while (!cola_es_vacia(rec->pendientes)) {
        SolicitudPendiente solicitud =
            (SolicitudPendiente)cola_inicio(rec->pendientes, no_copia_solicitud);
        if (solicitud == NULL) break;

        if (rec->disponible >= solicitud->cantidad_pedida) {
            rec->disponible -= solicitud->cantidad_pedida;
            registrar_asignacion(estado->libro_contable, solicitud->job_id,
                solicitud->socket_origen, nombre_recurso, solicitud->cantidad_pedida);
            if (avisar_red) avisar_red(solicitud->job_id, solicitud->socket_origen);
            rec->pendientes = cola_desencolar(rec->pendientes, destruir_solicitud);
        } else {
            break;
        }
    }
}

static void visitar_peticion_vencida(void *dato, void *extra) {
    PeticionMulti p = (PeticionMulti)dato;
    struct ctx_expira_peticion *ctx = (struct ctx_expira_peticion *)extra;
    if (p->respondidos < p->total &&
        difftime(ctx->ahora, p->instante_creacion) >= TIEMPO_ESPERA_RESERVA) {
        ctx->vencidos[ctx->n++] = p->job_id;
    }
}

static void callback_tragedia(char* nombre_recurso, int cantidad) {
    // Aca nos saltamos registrar_liberacion porque liberar_recursos_socket
    // ya destruyó la ficha entera del cliente desconectado. Solo devolvemos
    // los recursos usados por algún job.
    RecursoLocal rec = obtener_recurso(estado_actual, nombre_recurso);
    if (!rec) return;

    rec->disponible += cantidad;

    while (!cola_es_vacia(rec->pendientes)) {
        SolicitudPendiente solicitud =
            (SolicitudPendiente)cola_inicio(rec->pendientes, no_copia_solicitud);

        if (solicitud == NULL) break;

        if (rec->disponible >= solicitud->cantidad_pedida) {
            rec->disponible -= solicitud->cantidad_pedida;
            registrar_asignacion(estado_actual->libro_contable, solicitud->job_id,
                solicitud->socket_origen, nombre_recurso, solicitud->cantidad_pedida);
            if (aviso_red_actual) aviso_red_actual(solicitud->job_id,
                                                    solicitud->socket_origen);
            rec->pendientes = cola_desencolar(rec->pendientes, destruir_solicitud);
        } else {
            break;
        }
    }
}

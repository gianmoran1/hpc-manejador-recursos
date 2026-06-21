#include "gestor_estado.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>


#define TIEMPO_ESPERA 30.0

// Helpers de memoria para la Cola
static void* no_copia_solicitud(void* dato) { return dato; }
static void destruir_solicitud(void* dato) { free(dato); }

static RecursoLocal obtener_recurso(EstadoGlobal estado, char* nombre) {
    if (strcmp(nombre, "cpu") == 0) return estado->cpu;
    if (strcmp(nombre, "gpu") == 0) return estado->gpu;
    if (strcmp(nombre, "mem") == 0) return estado->mem;
    return NULL;
}

EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem) {
    EstadoGlobal e = malloc(sizeof(struct estadoGlobal_));
    e->cpu = recurso_crear("cpu", cap_cpu);
    e->gpu = recurso_crear("gpu", cap_gpu);
    e->mem = recurso_crear("mem", cap_mem);
    e->libro_contable = crear_tabla_jobs();
    e->registro_nodos = crear_tabla_nodos();
    pthread_mutex_init(&e->lock, NULL);
    return e;
}

void estado_destruir(EstadoGlobal estado) {
    recurso_destruir(estado->cpu);
    recurso_destruir(estado->gpu);
    recurso_destruir(estado->mem);
    destruir_tabla_jobs(estado->libro_contable);
    destruir_tabla_nodos(estado->registro_nodos);
    free(estado);
}

// -----------------------------------------------------------------------------
// OPERACIONES MAESTRAS
// -----------------------------------------------------------------------------

int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id, int socket_origen, int cantidad) {
    pthread_mutex_lock(&estado->lock);

    int resultado = -1; // DENIED por defecto
    RecursoLocal rec = obtener_recurso(estado, nombre_recurso);

    if (rec != NULL && cantidad > 0 && cantidad <= rec->capacidad_total) {
        if (rec->disponible >= cantidad && cola_es_vacia(rec->pendientes)) {
            rec->disponible -= cantidad;
            registrar_asignacion(estado->libro_contable, job_id, socket_origen, nombre_recurso, cantidad);
            resultado = 1; // Granted
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

void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id, int cantidad, void (*avisar_red)(int, int)) {
    pthread_mutex_lock(&estado->lock);

    RecursoLocal rec = obtener_recurso(estado, nombre_recurso);
    // ¡CORRECCIÓN!: Desbloqueamos antes del return de emergencia
    if (!rec || cantidad <= 0) {
        pthread_mutex_unlock(&estado->lock); 
        return; 
    }

    int liberado = registrar_liberacion(estado->libro_contable, job_id, nombre_recurso, cantidad);
    if (liberado == 0) {
        pthread_mutex_unlock(&estado->lock); // ¡CORRECCIÓN!
        return; 
    }

    // Devolver solo lo liberado a la disponibilidad
    rec->disponible += liberado;

    // Atender solicitudes pendientes (igual que antes, pero con el valor correcto)
    while (!cola_es_vacia(rec->pendientes)) {
        SolicitudPendiente solicitud = (SolicitudPendiente)cola_inicio(rec->pendientes, no_copia_solicitud);
        if (solicitud == NULL) break;

        if (rec->disponible >= solicitud->cantidad_pedida) {
            rec->disponible -= solicitud->cantidad_pedida;
            registrar_asignacion(estado->libro_contable, solicitud->job_id, solicitud->socket_origen, nombre_recurso, solicitud->cantidad_pedida);
            if (avisar_red) avisar_red(solicitud->job_id, solicitud->socket_origen);
            rec->pendientes = cola_desencolar(rec->pendientes, destruir_solicitud);
        } else {
            break;
        }
    }
    pthread_mutex_unlock(&estado->lock);
}


void gestor_expirar_pedidos(EstadoGlobal estado, void (*avisar_timeout)(int, int)) {
    pthread_mutex_lock(&estado->lock);
    time_t ahora = time(NULL);
    // Un simple arreglo nos permite limpiar los 3 recursos de un tirazo
    RecursoLocal recursos[] = {estado->cpu, estado->gpu, estado->mem};

    for (int i = 0; i < 3; i++) {
        RecursoLocal rec = recursos[i];
        int termine = 0;

        if (rec ==  NULL) continue;
        
        while (!cola_es_vacia(rec->pendientes) && !termine) {
            SolicitudPendiente solicitud = (SolicitudPendiente)cola_inicio(rec->pendientes, no_copia_solicitud);
            if (!solicitud) break;

            if (difftime(ahora, solicitud->instante_llegada) >= TIEMPO_ESPERA) {
                printf("[TIMEOUT] El job_id %d caducó en recurso %s.\n", solicitud->job_id, rec->nombre);
                if (avisar_timeout) avisar_timeout(solicitud->job_id, solicitud->socket_origen);
                rec->pendientes = cola_desencolar(rec->pendientes, destruir_solicitud);
            } else {
                termine = 1;
            }
        }
    }
    pthread_mutex_unlock(&estado->lock);
}

// -----------------------------------------------------------------------------
// PROTOCOLO DE EMBARGO (Tragedia de desconexión)
// -----------------------------------------------------------------------------
static void (*aviso_red_actual)(int, int) = NULL;
static EstadoGlobal estado_actual = NULL;

static void callback_tragedia(char* nombre_recurso, int cantidad) {
    // Aca nos saltamos registrar_liberacion porque liberar_recursos_socket 
    // ya destruyó la ficha entera del cliente desconectado. Solo repartimos el oro recupeado.
    RecursoLocal rec = obtener_recurso(estado_actual, nombre_recurso);
    if (!rec) return;
    
    rec->disponible += cantidad;
    
    while (!cola_es_vacia(rec->pendientes)) {
        SolicitudPendiente solicitud = (SolicitudPendiente)cola_inicio(rec->pendientes, no_copia_solicitud);
        if (solicitud == NULL) break;

        if (rec->disponible >= solicitud->cantidad_pedida) {
            rec->disponible -= solicitud->cantidad_pedida;
            registrar_asignacion(estado_actual->libro_contable, solicitud->job_id, solicitud->socket_origen, nombre_recurso, solicitud->cantidad_pedida);
            if (aviso_red_actual) aviso_red_actual(solicitud->job_id, solicitud->socket_origen);
            rec->pendientes = cola_desencolar(rec->pendientes, destruir_solicitud);
        } else {
            break;
        }
    }
}

void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, void (*avisar_red)(int, int)) {
    pthread_mutex_lock(&estado->lock);
    estado_actual = estado;
    aviso_red_actual = avisar_red;
    
    liberar_recursos_socket(estado->libro_contable, socket_caido, callback_tragedia);
    
    estado_actual = NULL;
    aviso_red_actual = NULL;
    pthread_mutex_unlock(&estado->lock);
}


// -----------------------------------------------------------------------------
// OPERACIONES DEL RADAR DE NODOS (Protocolo Erlang / UDP)
// -----------------------------------------------------------------------------

void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu, int gpu, int mem) {
    pthread_mutex_lock(&estado->lock);
    // Redirige la orden directamente al módulo de nodos pasando el registro correspondiente
    procesar_anuncio(estado->registro_nodos, ip, puerto, cpu, gpu, mem);
    pthread_mutex_unlock(&estado->lock);
}

char* gestor_get_nodes(EstadoGlobal estado) {
    pthread_mutex_lock(&estado->lock);
    
    // ¡CORRECCIÓN!: Guardamos el resultado temporalmente para poder abrir la puerta
    char* resultado = get_nodes(estado->registro_nodos);
    
    pthread_mutex_unlock(&estado->lock);
    return resultado;
}

void gestor_desconectar_nodos(EstadoGlobal estado) {
     pthread_mutex_lock(&estado->lock);
    // Ejecuta la guillotina sobre los nodos que superaron los 15s de inactividad
    desconectar(estado->registro_nodos);
    pthread_mutex_unlock(&estado->lock);
}
#include "gestor_estado.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>


#define TIEMPO_ESPERA 30.0

// funciones auxiliares para la Cola
static void* no_copia_solicitud(void* dato) { return dato; }
static void destruir_solicitud(void* dato) { free(dato); }

// funciones auxiliares para la TablaHash de peticiones multi-recurso
static void* no_copia_peticion(void* dato) { return dato; }
static int peticion_comparar(void* dato1, void* dato2) {
    return ((PeticionMulti)dato1)->job_id - ((PeticionMulti)dato2)->job_id;
}
static unsigned peticion_hash(void* dato) {
    return (unsigned)((PeticionMulti)dato)->job_id;
}

//---------------------------------------------------------------------------

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
    e->peticiones_pendientes = tablahash_crear(100, no_copia_peticion, peticion_comparar, (FuncionDestructora)free, peticion_hash);
    pthread_mutex_init(&e->lock, NULL);
    return e;
}

void estado_destruir(EstadoGlobal estado) {
    recurso_destruir(estado->cpu);
    recurso_destruir(estado->gpu);
    recurso_destruir(estado->mem);
    pthread_mutex_destroy(&estado->lock);
    destruir_tabla_jobs(estado->libro_contable);
    destruir_tabla_nodos(estado->registro_nodos);
    tablahash_destruir(estado->peticiones_pendientes);
    free(estado);
}


int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id, int socket_origen, int cantidad) {
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

static void manejar_release_aux(EstadoGlobal estado, char* nombre_recurso, int job_id, int cantidad, void (*avisar_red)(int, int)) {
    RecursoLocal rec = obtener_recurso(estado, nombre_recurso);
    if (!rec || cantidad <= 0) return;

    // Liberar solo lo que realmente tiene asignado en el libro contable
    int liberado = registrar_liberacion(estado->libro_contable, job_id, nombre_recurso, cantidad);
    if (liberado == 0) return; // Nada que liberar

    // devuelvo los recursos
    rec->disponible += liberado;

    // Revisamos la cola de este recurso específico y repartimos al siguiente en espera
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
}

void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id, int cantidad, void (*avisar_red)(int, int)) {
    pthread_mutex_lock(&estado->lock);
    
    manejar_release_aux(estado, nombre_recurso, job_id, cantidad, avisar_red);
    
    pthread_mutex_unlock(&estado->lock);
}


void gestor_liberar_job(EstadoGlobal estado, int job_id, void (*avisar_red)(int, int)) {
    struct jobActivo_ busqueda;
    busqueda.job_id = job_id;

    pthread_mutex_lock(&estado->lock);

    // Buscamos el trabajo directamente bajo la seguridad del lock
    JobActivo job = tablahash_buscar(estado->libro_contable->tabla, &busqueda);
    if (!job) {
        pthread_mutex_unlock(&estado->lock);
        printf("[GESTOR] JOB_RELEASE: job %d no encontrado en el libro contable\n", job_id);
        return;
    }

    // Guardamos las deudas actuales en variables locales
    int cpu = job->cpu_usada;
    int gpu = job->gpu_usada;
    int mem = job->mem_usada;

    // Ejecutamos las tres liberaciones de manera consecutiva y atómica
    // Cada llamada interna buscará al job por su ID de manera segura, 
    // y la última llamada destruirá la ficha del trabajo al quedar sus deudas en 0.
    if (cpu > 0) manejar_release_aux(estado, "cpu", job_id, cpu, avisar_red);
    if (gpu > 0) manejar_release_aux(estado, "gpu", job_id, gpu, avisar_red);
    if (mem > 0) manejar_release_aux(estado, "mem", job_id, mem, avisar_red);

    pthread_mutex_unlock(&estado->lock);
}

void gestor_expirar_pedidos(EstadoGlobal estado, void (*avisar_timeout)(int, int)) {
    pthread_mutex_lock(&estado->lock);
    time_t ahora = time(NULL);
    // Un simple arreglo nos permite limpiar los 3 recursos
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
// PROTOCOLO ANTE DESCONEXIONES 
// -----------------------------------------------------------------------------
static void (*aviso_red_actual)(int, int) = NULL;
static EstadoGlobal estado_actual = NULL;

static void callback_tragedia(char* nombre_recurso, int cantidad) {
    // Aca nos saltamos registrar_liberacion porque liberar_recursos_socket 
    // ya destruyó la ficha entera del cliente desconectado. Solo devolvemos los recursos usados por algún job.
    RecursoLocal rec = obtener_recurso(estado_actual, nombre_recurso);
    if (!rec) return;
    
    rec->disponible += cantidad;
    
    while (!cola_es_vacia(rec->pendientes)) { //vaciamos la cola.
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
// GESTIÓN DE PETICIONES MULTI-RECURSO
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// OPERACIONES DEL RADAR DE NODOS (Protocolo Erlang / UDP)
// -----------------------------------------------------------------------------

void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu, int gpu, int mem) {
    pthread_mutex_lock(&estado->lock);
    procesar_anuncio(estado->registro_nodos, ip, puerto, cpu, gpu, mem);
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
    // desconecta tras 15 segundos.
    desconectar(estado->registro_nodos);
    pthread_mutex_unlock(&estado->lock);
}
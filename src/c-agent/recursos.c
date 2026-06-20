#include "recursos.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TIEMPO_ESPERA 30.0

static void* no_copia_solicitud(SolicitudPendiente dato) {
    return dato;
}



static void destruir_solicitud(void* dato){
    free(dato);
}



RecursoLocal recurso_crear(char* nombre, int capacidad) {
    RecursoLocal rec = malloc(sizeof(struct recursoLocal_));
    strncpy(rec->nombre, nombre, sizeof(rec->nombre) - 1);
    rec->nombre[sizeof(rec->nombre) - 1] = '\0'; 
    rec->capacidad_total = capacidad;
    rec->disponible = capacidad;
    rec->pendientes = cola_crear();
    return rec;
}
void recurso_destruir(RecursoLocal rec){
    cola_destruir(rec->pendientes, (FuncionDestructora)destruir_solicitud);
    free(rec);
}

int manejar_reserva(RecursoLocal rec, int job_id, int socket_origen, int cantidad) {
    if (rec->disponible >= cantidad && cola_es_vacia(rec->pendientes)) {
        
        rec->disponible -= cantidad;
        return 1; 
    } else {
        
        SolicitudPendiente nueva_solicitud = malloc(sizeof(struct solicitudPendiente_));
        nueva_solicitud->job_id = job_id;
        nueva_solicitud->socket_origen = socket_origen;
        nueva_solicitud->cantidad_pedida = cantidad;
        nueva_solicitud->instante_llegada = time(NULL);
        
        
        rec->pendientes = cola_encolar(rec->pendientes, nueva_solicitud, (FuncionCopia)no_copia_solicitud);
        return 0; 
    }
}





void manejar_release(RecursoLocal rec, int cantidad, FuncionAviso avisar_red) {
    if (rec == NULL) return;
    
    if (cantidad <= 0) return;

    rec->disponible += cantidad;
    

    while (!cola_es_vacia(rec->pendientes)) {
        SolicitudPendiente solicitud = (SolicitudPendiente) cola_inicio(rec->pendientes, (FuncionCopia)no_copia_solicitud);

        if (solicitud == NULL) {
            break; 
        }
        
        

        
        if (rec->disponible >= solicitud->cantidad_pedida) {
            rec->disponible -= solicitud->cantidad_pedida;
            if (avisar_red != NULL) avisar_red(solicitud->job_id, solicitud->socket_origen);
            rec->pendientes = cola_desencolar(rec->pendientes, (FuncionDestructora)destruir_solicitud);
        } else {
            break; 
        }
    }
}


void expirar_pedidos(RecursoLocal rec, FuncionAviso avisar_timeout){
    if (rec == NULL) return;
    
    int termine = 0;

    time_t ahora = time(NULL); 

    while(!cola_es_vacia(rec->pendientes) && !termine){
        SolicitudPendiente solicitud = (SolicitudPendiente) cola_inicio(rec->pendientes, (FuncionCopia)no_copia_solicitud);
        if (solicitud == NULL) {
            break; 
        }
        if (difftime(ahora, solicitud->instante_llegada) >= TIEMPO_ESPERA) {
            printf("El job_id %d se cansó de esperar y caducó.\n", solicitud->job_id);
            if (avisar_timeout != NULL) {
                avisar_timeout(solicitud->job_id, solicitud->socket_origen);
            }
            rec->pendientes = cola_desencolar(rec->pendientes, (FuncionDestructora)destruir_solicitud);
            continue; 
        }
        else{
            termine = 1;
        }
    }
}
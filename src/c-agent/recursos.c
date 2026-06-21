#include "recursos.h"
#include <stdlib.h>
#include <string.h>

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
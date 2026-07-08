#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "recursos.h"

static void destruir_solicitud(void* dato);

RecursoLocal recurso_crear(char* nombre, int capacidad) {
    RecursoLocal rec = malloc(sizeof(struct recursoLocal_));
    assert(rec);
    strncpy(rec->nombre, nombre, sizeof(rec->nombre) - 1);
    rec->nombre[sizeof(rec->nombre) - 1] = '\0'; 
    rec->capacidad_total = capacidad;
    rec->disponible = capacidad;
    rec->pendientes = cola_crear();
    return rec;
}

void recurso_destruir(RecursoLocal rec) {
    cola_destruir(rec->pendientes, (FuncionDestructora)destruir_solicitud);
    free(rec);
}

static void destruir_solicitud(void* dato) {
    free(dato);
}
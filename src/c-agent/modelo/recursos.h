#ifndef __RECURSOS_H__
#define __RECURSOS_H__

#include <time.h>

#include "estructuras/cola.h"
#include "config.h"

typedef struct solicitudPendiente_{
    int job_id;
    int socket_origen;
    int cantidad_pedida;
    time_t instante_llegada;
} *SolicitudPendiente;

typedef struct recursoLocal_{
    char nombre[TAM_NOMBRE_RECURSO];
    int capacidad_total;
    int disponible;
    Cola pendientes;
} *RecursoLocal;

/* Crea un recurso local */
RecursoLocal recurso_crear(char* nombre, int capacidad);

/* Destruye un recurso local */
void recurso_destruir(RecursoLocal rec);

#endif /* __RECURSOS_H__ */
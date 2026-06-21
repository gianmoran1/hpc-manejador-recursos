#ifndef __RECURSOS_H__
#define __RECURSOS_H__

#include "cola.h"
#include <time.h>

// 1. Movemos las estructuras acá para que el Gestor pueda verlas
typedef struct solicitudPendiente_{
    int job_id;
    int socket_origen;
    int cantidad_pedida;
    time_t instante_llegada;
} *SolicitudPendiente;

typedef struct recursoLocal_{
    char nombre[10];       
    int capacidad_total;   
    int disponible;        
    Cola pendientes;       
} *RecursoLocal;

// 2. Quitamos todos los callbacks y la lógica. Solo dejamos la gestión de memoria.
RecursoLocal recurso_crear(char* nombre, int capacidad);
void recurso_destruir(RecursoLocal rec);

#endif /* __RECURSOS_H__ */
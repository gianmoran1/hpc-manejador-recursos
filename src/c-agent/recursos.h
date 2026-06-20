#ifndef __RECURSOS_H__
#define __RECURSOS_H__

#include "cola.h"
#include <time.h>

typedef void (*FuncionAviso)(int job_id, int socket_origen);

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


RecursoLocal recurso_crear(char* nombre, int capacidad);
void recurso_destruir(RecursoLocal rec);
int manejar_reserva(RecursoLocal rec, int job_id, int socket_origen, int cantidad);
void manejar_release(RecursoLocal rec, int cantidad, FuncionAviso avisar_red);
void expirar_pedidos(RecursoLocal rec, FuncionAviso avisar_timeout);
#endif /* __RECURSOS_H__ */
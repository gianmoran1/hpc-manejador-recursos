#ifndef __GESTOR_ESTADO_H__
#define __GESTOR_ESTADO_H__

#include "recursos.h"
#include "jobs.h"
#include "nodos.h"
#include <pthread.h>

typedef struct estadoGlobal_ {
    RecursoLocal cpu;
    RecursoLocal gpu;
    RecursoLocal mem;
    TablaJobs libro_contable;
    TablaNodos registro_nodos;
    pthread_mutex_t lock;
} *EstadoGlobal;

EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem);
void estado_destruir(EstadoGlobal estado);


int gestor_manejar_reserva(EstadoGlobal estado, char* nombre_recurso, int job_id, int socket_origen, int cantidad);
void gestor_manejar_release(EstadoGlobal estado, char* nombre_recurso, int job_id, int cantidad, void (*avisar_red)(int, int));
void gestor_expirar_pedidos(EstadoGlobal estado, void (*avisar_timeout)(int, int));
void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, void (*avisar_red)(int, int));


void gestor_procesar_anuncio(EstadoGlobal estado, char* ip, int puerto, int cpu, int gpu, int mem);
char* gestor_get_nodes(EstadoGlobal estado);
void gestor_desconectar_nodos(EstadoGlobal estado);

#endif /* __GESTOR_ESTADO_H__ */
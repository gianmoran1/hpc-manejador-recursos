#ifndef __ESTADO_H__
#define __ESTADO_H__

#include "modelo/recursos.h"
#include "modelo/jobs.h"
#include "modelo/nodos.h"
#include "estructuras/tablahash.h"
#include <pthread.h>

typedef struct estadoGlobal_ {
  RecursoLocal cpu;
  RecursoLocal gpu;
  RecursoLocal mem;
  TablaJobs libro_contable;
  TablaNodos registro_nodos;
  TablaHash peticiones_pendientes;
  pthread_mutex_t lock;
} *EstadoGlobal;

/* 
 * Crea e inicializa el estado global del agente con las capacidades dadas de
 * cpu, gpu y mem. Inicializa el mutex interno. Devuelve el puntero al estado. 
 */
EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem);

/* 
 * Libera todo el estado global (recursos, jobs, nodos, peticiones y mutex).
 */
void estado_destruir(EstadoGlobal estado);

#endif /* __ESTADO_H__ */

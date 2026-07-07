#include "estado.h"
#include "config.h"
#include "peticiones.h"

#include <stdlib.h>
#include <assert.h>

// Callbacks para la TablaHash de peticiones multi-recurso.

static void* no_copia_peticion(void* dato) { return dato; }

static int peticion_comparar(void* dato1, void* dato2) {
  return ((PeticionMulti)dato1)->job_id - ((PeticionMulti)dato2)->job_id;
}

// Hash de una petición: su propio job_id.
static unsigned peticion_hash(void* dato) {
  return (unsigned)((PeticionMulti)dato)->job_id;
}

EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem) {
  EstadoGlobal e = malloc(sizeof(struct estadoGlobal_));
  assert(e);
  e->cpu = recurso_crear(RECURSO_CPU, cap_cpu);
  e->gpu = recurso_crear(RECURSO_GPU, cap_gpu);
  e->mem = recurso_crear(RECURSO_MEM, cap_mem);
  e->libro_contable = crear_tabla_jobs();
  e->registro_nodos = crear_tabla_nodos();
  e->peticiones_pendientes = tablahash_crear(TAM_INICIAL_TABLA_HASH,
    no_copia_peticion, peticion_comparar, (FuncionDestructora)free, peticion_hash);
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
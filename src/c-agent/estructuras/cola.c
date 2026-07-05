#include "cola.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Crea y devuelve una cola vacía.
 */
Cola cola_crear(){
  Cola cola = malloc(sizeof(_Cola));
  assert(cola);
  cola->primero = NULL;
  cola->ultimo = NULL;
  return cola;
}

/**
 * Destruye la cola completa: libera cada nodo y aplica destroy sobre su dato.
 */
void cola_destruir(Cola cola, FuncionDestructora destroy){
  if (cola != NULL){
    for(GNode* primero = cola->primero; primero != NULL;primero = cola->primero ){
      cola->primero = primero->next;
      destroy(primero->data);
      free(primero);
    }
    free(cola);
  }
}

/**
 * Determina si la cola es vacía.
 */
int cola_es_vacia(Cola cola){
  if (cola->primero == NULL) return 1;
  return 0;
}

/**
 * Devuelve copy(dato) del primer elemento sin desencolarlo, o NULL si la
 * cola es NULL o está vacía. Con una copia que no copia (no_copia), permite
 * inspeccionar y modificar el elemento del frente sin sacarlo de la cola.
 */
void* cola_inicio(Cola cola, FuncionCopia copy){
  if (cola == NULL) return NULL;
  if (cola->primero == NULL) return NULL;
  return copy(cola->primero->data);
}

/**
 * Inserta dato al final de la cola. Si cola es NULL, la crea.
 * Devuelve la cola (posiblemente recién creada): el resultado debe
 * reasignarse en el llamador, ej. `cola = cola_encolar(cola, dato, copy);`.
 */
Cola cola_encolar(Cola cola, void* dato, FuncionCopia copy){
  if (cola == NULL) cola = cola_crear();
  GNode* nodoNuevo = malloc(sizeof(GNode));
  nodoNuevo->data = copy(dato);
  nodoNuevo->next = NULL;
  if (cola->primero == NULL) {
    cola->primero = nodoNuevo;
  }
  else { //cola->ultimo != NULL
    cola->ultimo->next = nodoNuevo;
  }
  cola->ultimo = nodoNuevo;
  return cola;
}

/**
 * Elimina el primer elemento de la cola, liberando su dato con destroy.
 * Devuelve la cola (o NULL si cola ya era NULL).
 */
Cola cola_desencolar(Cola cola, FuncionDestructora destroy){
  if (cola == NULL) return NULL;
  if (cola->primero == NULL) return NULL;
  GNode *temp = cola->primero;
  cola->primero = cola->primero->next;
  if(cola->primero == NULL) cola->ultimo = NULL;
  destroy(temp->data);
  free(temp);
  return cola;
}

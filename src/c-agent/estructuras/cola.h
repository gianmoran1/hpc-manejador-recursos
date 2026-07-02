#ifndef __COLA_H__
#define __COLA_H__

#include "glist.h"

typedef struct {
  GNode *primero;
  GNode *ultimo;
}_Cola;

typedef _Cola *Cola;

/**
 * Crea y devuelve una cola vacía.
 */
Cola cola_crear();

/**
 * Destruye la cola completa: libera cada nodo y aplica destroy sobre su dato.
 */
void cola_destruir(Cola cola, FuncionDestructora destroy);

/**
 * Devuelve copy(dato) del primer elemento sin desencolarlo, o NULL si la
 * cola es NULL o está vacía. Con una copia que no copia (no_copia), permite
 * inspeccionar y modificar el elemento del frente sin sacarlo de la cola.
 */
void* cola_inicio(Cola cola, FuncionCopia copy);

/**
 * Determina si la cola es vacía.
 */
int cola_es_vacia(Cola cola);

/**
 * Inserta dato al final de la cola. Si cola es NULL, la crea.
 * Devuelve la cola (posiblemente recién creada): el resultado debe
 * reasignarse en el llamador, ej. `cola = cola_encolar(cola, dato, copy);`.
 */
Cola cola_encolar(Cola cola, void* dato, FuncionCopia copy);

/**
 * Elimina el primer elemento de la cola, liberando su dato con destroy.
 * Devuelve la cola (o NULL si cola ya era NULL).
 */
Cola cola_desencolar(Cola cola, FuncionDestructora destroy);

#endif /* __COLA_H__ */
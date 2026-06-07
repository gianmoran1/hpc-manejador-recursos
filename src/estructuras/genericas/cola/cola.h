#ifndef COLA_H
#define COLA_H

#include "../lista/lista.h"

typedef Lista Cola;

/**
 * cola_crear: Dada una funcion de copia y otra de destruccion, la funcion crea
 * una cola.
 */
Cola cola_crear(FuncionCopiadora copiar, FuncionDestructora destruir);

/**
 * cola_es_vacia: Dada una cola la funcion retorna 1 si es vacia o 0 si no.
 * Si la cola es NULL devuelve -1.
 */
int cola_es_vacia(Cola cola);

/**
 * cola_primero: Dada una cola la funcion retorna el elemento que se encuentre
 * primero en la cola. Es decir, el elemento a desencolar.
 */
void* cola_primero(Cola cola);

/**
 * cola_encolar: Dada una cola y un dato la funcion encola el dato en la cola
 * dada.
 */
void cola_encolar(Cola cola, void* dato);

/**
 * cola_desencolar: Dada una cola la funcion desencola, es decir saca el primer
 * elemento de la cola y retorna este elemento.
 */
void* cola_desencolar(Cola cola);

/**
 * cola_destruir: Dada una cola la funcion libera la memoria pedida por esta.
 */
Cola cola_destruir(Cola cola);

#endif // COLA_H
#ifndef FUNCIONES_PRIMITIVAS_H
#define FUNCIONES_PRIMITIVAS_H

#include "../../../genericas/lista/lista.h"

/**
 * primitiva_0i: Dada una lista, agrega un 0 como primer elemento de la lista.
 */
Lista primitiva_0i(Lista lista);

/**
 * primitiva_0d: Dada una lista agrega un 0 como ultimo elemento de la lista.
 */
Lista primitiva_0d(Lista lista);

/**
 * primitiva_Si: Dada una lista, aumenta en uno el primer elemento de la lista. 
 */
Lista primitiva_Si(Lista lista);

/**
 * primitiva_Sd: Dada una lista, aumenta en uno el ultimo elemento de la lista. 
 */
Lista primitiva_Sd(Lista lista);

/**
 * primitiva_Di: Dada una lista, elimina el primer elemento de la lista. Si la
 * lista es vacia devuelve NULL.
 */
Lista primitiva_Di(Lista lista);

/**
 * primitiva_Dd: Dada una lista, elimina el ultimo elemento de la lista. Si la
 * lista es vacia devuelve NULL.
 */
Lista primitiva_Dd(Lista lista);

#endif // FUNCIONES_PRIMITIVAS_H
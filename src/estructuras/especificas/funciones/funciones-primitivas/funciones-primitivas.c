#include <stdlib.h>

#include "funciones-primitivas.h"
#include "../../../../utils/utils-enteros.h"

/**
 * primitiva_0i: Dada una lista, agrega un 0 como primer elemento de la lista.
 */
Lista primitiva_0i(Lista lista) {
  int cero = 0;
  lista_agregar_inicio(lista, &cero);
  return lista;
}

/**
 * primitiva_0d: Dada una lista agrega un 0 como ultimo elemento de la lista.
 */
Lista primitiva_0d(Lista lista) {
  int cero = 0;
  lista_agregar_final(lista, &cero);
  return lista;
}

/**
 * primitiva_Si: Dada una lista, aumenta en uno el primer elemento de la lista. 
 */
Lista primitiva_Si(Lista lista) {
  if (lista_es_vacia(lista) == 1) {
    return NULL;
  }
  int* primero = lista_elimina_primero(lista);
  int sucesor = *primero + 1;
  entero_destruir(primero);
  lista_agregar_inicio(lista, &sucesor);
  return lista;
}

/**
 * primitiva_Sd: Dada una lista, aumenta en uno el ultimo elemento de la lista. 
 */
Lista primitiva_Sd(Lista lista) {
  if (lista_es_vacia(lista) == 1) {
    return NULL;
  }
  int* ultimo = lista_elimina_ultimo(lista);
  int sucesor = *ultimo + 1;
  entero_destruir(ultimo);
  lista_agregar_final(lista, &sucesor);
  return lista;
}

/**
 * primitiva_Di: Dada una lista, elimina el primer elemento de la lista. Si la
 * lista es vacia devuelve NULL.
 */
Lista primitiva_Di(Lista lista) {
  if (lista_es_vacia(lista) == 1) {
    return NULL;
  }
  entero_destruir((int*) lista_elimina_primero(lista));
  return lista;
}

/**
 * primitiva_Dd: Dada una lista, elimina el ultimo elemento de la lista. Si la
 * lista es vacia devuelve NULL.
 */
Lista primitiva_Dd(Lista lista) {
  if (lista_es_vacia(lista) == 1) {
    return NULL;
  }
  entero_destruir((int*) lista_elimina_ultimo(lista));
  return lista;
}

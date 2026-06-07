#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lista.h"
#include "../../../utils/utils-str.h"

/**
 * Estructura que representa un nodo de una lista doblemente enlazada.
 */
typedef struct _NodoLista{
  void* dato;
  struct _NodoLista* siguiente;
  struct _NodoLista* anterior;
}* NodoLista;

/**
 * Estructura que representa una lista doblemente enlazada. El campo iterador
 * es un puntero a un NodoLista que tiene el objetivo de iterar sobre los
 * elementos de la lista mediante las funciones de iteracion, hasta entonces 
 * este sera NULL.
 */
struct _Lista{
  char* idLista; // Si no corresponde se coloca NULL.
  NodoLista primero;
  NodoLista ultimo;
  NodoLista iterador;
  int cantElementos;
  FuncionCopiadora copiar;
  FuncionDestructora destruir;
};

/**
 * lista_crear: Dada una funcion de copia y otra de destruccion la funcion crea 
 * una lista doblemente enlazada.
 */
Lista lista_crear(FuncionCopiadora copiar, FuncionDestructora destruir) {
  Lista lista = malloc(sizeof(struct _Lista));
  assert(lista);
  lista->idLista = NULL;
  lista->primero = NULL;
  lista->ultimo = NULL;
  lista->iterador = NULL;
  lista->cantElementos = 0;
  lista->copiar = copiar;
  lista->destruir = destruir;
  return lista;
}

/**
 * lista_es_vacia: Dada una lista, la funcion retorna 1 si es vacia, o 0 en caso
 * contrario. Si la lista es NULL retorna -1.
 */
int lista_es_vacia(Lista lista) {
  int esVacia = -1;
  if (lista) {
    esVacia = lista->cantElementos == 0;
  }
  return esVacia;
}

/**
 * lista_asignar_id: Dada una lista y un string, la funcion asigna ese string
 * como id de la lista en caso de que esta no tenga un id. Se crea una copia
 * del string dado.
 */
void lista_asignar_id(Lista lista, char* id) {
  if (lista && !lista->idLista) {
    lista->idLista = string_copiar(id);
  }
}

/**
 * lista_iterador_inicializar: Dada una lista, la funcion inicializa el iterador
 * de esta, colocando el iterador al inicio de la lista y devolviendo el dato
 * que alli se halle. Devuelve NULL si no hay un primer elemento.
 */
void* lista_iterador_inicializar(Lista lista) {
  void* dato = NULL;
  if (lista && lista->primero) {
    lista->iterador = lista->primero;
    dato = lista->iterador->dato;
  }
  return dato;
}

/**
 * lista_iterador_actual: Dada una lista, la funcion devuelve el dato donde en
 * el cual se encuentra el iterador, devuelve NULL si el iterador no se
 * inicializo.
 */
void* lista_iterador_actual(Lista lista) {
  void* dato = NULL;
  if (lista && lista->iterador) {
    dato = lista->iterador->dato;
  }
  return dato;
}

/**
 * lista_iterador_siguiente: Dada una lista, la funcion mueve el iterador al
 * siguiente nodo y devuelve el dato que alli se halle. En caso de que no haya
 * siguiente devuelve NULL. Tambien devuelve NULL si el iterador nunca se 
 * inicializo.
 */
void* lista_iterador_siguiente(Lista lista) {
  void* dato = NULL;
  if (lista && lista->iterador && lista->iterador->siguiente) {
    lista->iterador = lista->iterador->siguiente;
    dato = lista->iterador->dato;
  }
  return dato;
}

/**
 * lista_iterador_destruir: Dada una lista, la funcion destruye el iterador,
 * esto es hace NULL.
 */
void lista_iterador_destruir(Lista lista) {
  if (lista) {
    lista->iterador = NULL;
  }
}

/**
 * lista_agregar_inicio: Dada una lista y un dato, la funcion lo agrega al
 * inicio de la lista.
 */
void lista_agregar_inicio(Lista lista, void* dato) {
  if (lista) {
    NodoLista nuevoNodo = malloc(sizeof(struct _NodoLista));
    assert(nuevoNodo);
    nuevoNodo->dato = lista->copiar(dato);
    nuevoNodo->siguiente = lista->primero;
    nuevoNodo->anterior = NULL;

    if (lista_es_vacia(lista) == 1){
      lista->ultimo = nuevoNodo;
    } else {
      lista->primero->anterior = nuevoNodo;
    }
    lista->primero = nuevoNodo;
    lista->cantElementos += 1;
  }
}

/**
 * lista_agregar_final: Dada una lista y un dato, la funcion lo agrega al
 * final de la lista.
 */
void lista_agregar_final(Lista lista, void* dato) {
  if (lista) {
    NodoLista nuevoNodo = malloc(sizeof(struct _NodoLista));
    assert(nuevoNodo);
    nuevoNodo->dato = lista->copiar(dato);
    nuevoNodo->siguiente = NULL;
    nuevoNodo->anterior = lista->ultimo;

    if (lista_es_vacia(lista) == 1) {
      lista->primero = nuevoNodo;
    } else {
      lista->ultimo->siguiente = nuevoNodo;
    }
    lista->ultimo = nuevoNodo;
    lista->cantElementos += 1;
  }
}

/**
 * lista_copiar: Dada una lista la funcion realiza una copia fisica de la misma.
 */
Lista lista_copiar(Lista lista) {
  Lista listaCopia = NULL;
  if (lista) {
    listaCopia = lista_crear(lista->copiar, lista->destruir);
    for (NodoLista nodoActual = lista->primero; nodoActual; 
                                          nodoActual = nodoActual->siguiente) {
      lista_agregar_final(listaCopia, nodoActual->dato);
    }
    listaCopia->idLista = string_copiar(lista->idLista);
  }
  return listaCopia;
}

/**
 * lista_cant_elementos: Dada una lista la funcion devuelve la cantidad de
 * elementos de la misma. Devuelve -1 si la lista es NULL.
 */
int lista_cant_elementos(Lista lista) {
  int cantElementos = -1;
  if (lista){
    cantElementos = lista->cantElementos;
  }
  return cantElementos;
}

/**
 * lista_primero: Dada una lista la funcion devuelve el primer elemento de la
 * misma.
 */
void* lista_primero(Lista lista) {
  void* primero = NULL;
  if (lista_es_vacia(lista) == 0) {
    primero = lista->primero->dato;
  }
  return primero;
}

/**
 * lista_ultimo: Dada una lista la funcion devuelve el ultimo elemento de la
 * misma.
 */
void* lista_ultimo(Lista lista) {
  void* ultimo = NULL;
  if (lista_es_vacia(lista) == 0) {
    ultimo = lista->ultimo->dato;
  }
  return ultimo;
}

/**
 * lista_elimina_primero: Dada una lista la funcion elimina el primer elemento.
 * Retorna este elemento, por ende no lo destruye.
 */
void* lista_elimina_primero(Lista lista) {
  void* primero = NULL;
  if (lista_es_vacia(lista) == 0) {
    primero = lista->primero->dato;
    NodoLista nuevoPrimero = lista->primero->siguiente;
    free(lista->primero);
    lista->cantElementos--;

    lista->primero = nuevoPrimero;
    if (lista->primero) { // Caso si habia mas elementos aparte del eliminado.
      lista->primero->anterior = NULL;
    } else { // Si elimine el unico elemento.
      lista->ultimo = NULL;
    }
  }
  return primero;
}

/**
 * lista_elimina_ultimo: Dada una lista la funcion elimina el ultimo elemento.
 * Retorna este elemento, por ende no lo destruye.
 */
void* lista_elimina_ultimo(Lista lista) {
  void* ultimo = NULL;
  if (lista_es_vacia(lista) == 0) {
    ultimo = lista->ultimo->dato;
    NodoLista nuevoUltimo = lista->ultimo->anterior;
    free(lista->ultimo);
    lista->cantElementos--;

    lista->ultimo = nuevoUltimo;
    if (lista->ultimo) { // Caso si habia mas elementos aparte del eliminado.
      lista->ultimo->siguiente = NULL;
    } else { // Si elimine el unico elemento.
      lista->primero = NULL;
    }
  }
  return ultimo;
}

/**
 * lista_comparar: Dadas dos listas, la funcion realiza la comparacion de sus ID
 * retornando un entero negativo si lista1 < lista2, 0 si son iguales y un 
 * entero positivo si lista1 > lista2.
 */
int lista_comparar(Lista lista1, Lista lista2) {
  assert(lista1 && lista2 && lista1->idLista && lista2->idLista);
  return strcmp(lista1->idLista, lista2->idLista);
}

/**
 * lista_hash: Dada una lista, la funcion devuelve un entero sin signo
 * correspondiente al indice de hash que se le asigna.
 */
unsigned lista_hash(Lista lista) {
  assert(lista && lista->idLista);
  unsigned hashval = 0;
  for (char* temp = lista->idLista; *temp; temp++) {
    hashval = *(temp) + 31 * hashval;
  }
  return hashval;
}

/**
 * lista_recorrer: Dada una lista y una funcion visitante, la funcion recorre
 * la lista y aplica la funcion visitante a cada dato de la lista.
 */
void lista_recorrer(Lista lista, FuncionVisitante visitar) {
  if (lista) {
    NodoLista nodoActual = lista->primero;
    while (nodoActual) {
      visitar(nodoActual->dato);
      nodoActual = nodoActual->siguiente;
    }
  }
}

/**
 * listas_iguales: Dadas dos listas y una funcion de comparacion, la funcion 
 * retorna 1 en caso de que las listas sean iguales o 0 si no lo son.
 */
int listas_iguales(Lista lista1, Lista lista2, FuncionComparadora comparar) {
  int listasIguales = -1;
  if (lista1 && lista2) {
    if (lista_cant_elementos(lista1) != lista_cant_elementos(lista2)) {
      return 0;
    }

    listasIguales = 1;
    int lecturaTerminada = 0;
    NodoLista nodoActual1 = lista1->primero;
    NodoLista nodoActual2 = lista2->primero;

    while (listasIguales && !lecturaTerminada) {
      if (nodoActual1 && nodoActual2 && 
            comparar(nodoActual1->dato, nodoActual2->dato) == 0) {
        nodoActual1 = nodoActual1->siguiente;
        nodoActual2 = nodoActual2->siguiente;
      } else if (!nodoActual1 && !nodoActual2) {
        lecturaTerminada = 1;
      } else {
        listasIguales = 0;
      }
    }
  }
  return listasIguales;
}

/**
 * lista_destruir: Dada una lista la funcion libera la memoria utilizada por la
 * lista y sus elementos.
 */
Lista lista_destruir(Lista lista) {
  if (lista) {
    NodoLista nodoActual = lista->primero;
    while (nodoActual) {
      NodoLista temp = nodoActual->siguiente;
      lista->destruir(nodoActual->dato);
      free(nodoActual);
      nodoActual = temp;
    }
    string_destruir(lista->idLista);
    free(lista);
    lista = NULL;
  }
  return lista;
}

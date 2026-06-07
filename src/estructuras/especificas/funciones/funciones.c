#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "funciones.h"
#include "../../../utils/utils-str.h"

/**
 * nodo_funcion_crear: La funcion crea una estructura NodoFuncion vacia.
 */
NodoFuncion nodo_funcion_crear() {
  NodoFuncion nodoFunc = malloc(sizeof(struct _NodoFuncion));
  assert(nodoFunc);
  nodoFunc->esRepeticion = -1;
  nodoFunc->func = NULL;
  return nodoFunc;
}

/**
 * nodo_funcion_copiar: Dado un NodoFuncion, la funcion retorna una copia fisica
 * del mismo.
 */
NodoFuncion nodo_funcion_copiar(NodoFuncion nodoFunc) {
  NodoFuncion nodoCopia = NULL;
  if (nodoFunc) {
    nodoCopia = nodo_funcion_crear();
    nodoCopia->esRepeticion = nodoFunc->esRepeticion;
    if (nodoFunc->esRepeticion != -1) {
      nodoCopia->func = funcion_copiar(nodoFunc->func);
    }
  }
  return nodoCopia;
}

/**
 * nodo_funcion_destruir: Dado un NodoFuncion, la funcion libera la memoria
 * pedida por este.
 */
NodoFuncion nodo_funcion_destruir(NodoFuncion nodoFunc) {
  if (nodoFunc) {
    if (nodoFunc->esRepeticion != -1) {
      funcion_destruir(nodoFunc->func);
    }
    free(nodoFunc);
    nodoFunc = NULL;
  }
  return nodoFunc;
}

/**
 * funcion_crear: La funcion crea una estructura Funcion vacia.
 */
Funcion funcion_crear() {
  Funcion func = malloc(sizeof(struct _Funcion));
  assert(func);
  func->esPrimitiva = -1;
  func->idFunc = NULL;
  return func;
}

/**
 * funcion_copiar: Dada un Funcion, la funcion retorna una copia fisica de la 
 * misma.
 */
Funcion funcion_copiar(Funcion func) {
  Funcion funcCopia = NULL;
  if (func) {
    funcCopia = funcion_crear();
    funcCopia->esPrimitiva = func->esPrimitiva;
    funcCopia->idFunc = string_copiar(func->idFunc);
    if (func->esPrimitiva == 1) {
      funcCopia->funcPrimitiva = func->funcPrimitiva;
    } else if (func->esPrimitiva == 0) {
      funcCopia->Composicion.cantFunciones = func->Composicion.cantFunciones;
      if (func->Composicion.cantFunciones > 0) {
        funcCopia->Composicion.arrFunc = 
            malloc(sizeof(NodoFuncion) * func->Composicion.cantFunciones);
        assert(funcCopia->Composicion.arrFunc);
        for (int i = 0; i < func->Composicion.cantFunciones; i++) {
          funcCopia->Composicion.arrFunc[i] = 
                              nodo_funcion_copiar(func->Composicion.arrFunc[i]);
        }
      }
    }
  }
  return funcCopia;
}

/**
 * funcion_compara: Dadas dos estructuras Funcion, la funcion realiza la 
 * comparacion de sus ID. Retorna un entero negativo si funcId1 < funcId2, 0 si 
 * son iguales y un entero positivo si funcId1 > funcId2.
 */
int funcion_compara(Funcion func1, Funcion func2) {
  assert(func1 && func2 && func1->idFunc && func2->idFunc);
  return strcmp(func1->idFunc, func2->idFunc);
}

/**
 * funcion_hash: Dada una Funcion, la funcion devuelve un entero sin signo
 * correspondiente al indice de hash que se le asigna.
 */
unsigned funcion_hash(Funcion func) {
  assert(func && func->idFunc);
  unsigned hashval = 0;
  for (char* p = func->idFunc; *p; p++) {
    hashval = *p + 31 * hashval;
  }
  return hashval;
}

/**
 * funcion_imprimir: Dada una estructura Funcion, la funcion la imprime por
 * pantalla.
 */
void funcion_imprimir(Funcion func) {
  if (func) {
    if (func->esPrimitiva == 1) {
      printf(" %s ", func->idFunc);
    } else {
      for (int i = 0 ; i < func->Composicion.cantFunciones; i++) {
        if (func->Composicion.arrFunc[i]->esRepeticion == 0) {
          funcion_imprimir(func->Composicion.arrFunc[i]->func);
        }
        if (func->Composicion.arrFunc[i]->esRepeticion == 1) {
          printf("<");
          funcion_imprimir(func->Composicion.arrFunc[i]->func);
          printf(">");
        }
      }
    }
  }
}

/**
 * funcion_destruir: Dada una Funcion, la funcion libera la memoria
 * pedida por esta.
 */
Funcion funcion_destruir(Funcion func) {
  if (func) {
    string_destruir(func->idFunc);
    if (func->esPrimitiva == 0 && func->Composicion.cantFunciones > 0) {
      for (int i = 0; i < func->Composicion.cantFunciones; i++) {
        nodo_funcion_destruir(func->Composicion.arrFunc[i]);
      }
      free(func->Composicion.arrFunc);
    }
    free(func);
    func = NULL;
  }
  return func;
}

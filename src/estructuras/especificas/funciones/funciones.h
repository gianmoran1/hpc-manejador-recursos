#ifndef FUNCIONES_H
#define FUNCIONES_H

#include "../../genericas/lista/lista.h"

typedef Lista (*FuncionPrimitiva) (Lista );

/**
 * Declaro las estructuras aqui ya que Funcion usa NodoFuncion y NodoFuncion
 * usa Funcion.
 */
typedef struct _Funcion* Funcion;
typedef struct _NodoFuncion* NodoFuncion;

/**
 * Estructura que representa un nodo de funcion compuesta, ya sea una primitiva, 
 * una funcion definida por el usuario o una repeticion.
 */
struct _NodoFuncion {
  int esRepeticion;
  Funcion func;
};

/**
 * Estructura que representa una funcion compuesta o una primitiva. En caso de 
 * que esPrimitiva sea 1 la union toma el valor de la primitiva correspondiente. 
 * En caso de que sea 0 la union toma el valor de la composicion. En caso de 
 * que no sea necesario el campo idFunc se deja NULL.
 */
struct _Funcion {
  int esPrimitiva;
  char* idFunc;
  union {
    FuncionPrimitiva funcPrimitiva;
    struct {
      NodoFuncion* arrFunc;
      int cantFunciones;
    } Composicion;
  };
};

/**
 * nodo_funcion_crear: La funcion crea una estructura NodoFuncion vacia.
 */
NodoFuncion nodo_funcion_crear();

/**
 * nodo_funcion_copiar: Dado un NodoFuncion, la funcion retorna una copia fisica
 * del mismo.
 */
NodoFuncion nodo_funcion_copiar(NodoFuncion nodoFunc);

/**
 * nodo_funcion_destruir: Dado un NodoFuncion, la funcion libera la memoria
 * pedida por este.
 */
NodoFuncion nodo_funcion_destruir(NodoFuncion nodoFunc);

/**
 * funcion_crear: La funcion crea una estructura Funcion vacia.
 */
Funcion funcion_crear();

/**
 * funcion_copiar: Dada un Funcion, la funcion retorna una copia fisica de la 
 * misma.
 */
Funcion funcion_copiar(Funcion func);

/**
 * funcion_compara: Dadas dos estructuras Funcion, la funcion realiza la 
 * comparacion de sus ID. Retorna un entero negativo si funcId1 < funcId2, 0 si 
 * son iguales y un entero positivo si funcId1 > funcId2.
 */
int funcion_compara(Funcion func1, Funcion func2);

/**
 * funcion_hash: Dada una Funcion, la funcion devuelve un entero sin signo
 * correspondiente al indice de hash que se le asigna.
 */
unsigned funcion_hash(Funcion func);

/**
 * funcion_imprimir: Dada una estructura Funcion, la funcion la imprime por
 * pantalla.
 */
void funcion_imprimir(Funcion func);

/**
 * funcion_destruir: Dada una Funcion, la funcion libera la memoria
 * pedida por esta.
 */
Funcion funcion_destruir(Funcion func);

#endif // FUNCIONES_H
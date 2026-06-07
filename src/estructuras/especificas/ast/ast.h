#ifndef AST_H
#define AST_H

#include "../../genericas/lista/lista.h"
#include "../../../utils/utils-tipo-sentencia.h"

/**
 * Las estructuras "Sintax" son las que guardan la informacion necesaria para 
 * ejecutar una sentencia del tipo indicado. 
 * En el caso de las funciones, es la estructura que guarda la informacion de 
 * una funcion, mas especificamente del "nodo" de una funcion. Por ejemplo, en 
 * la sentencia -- deff f = mi < si 0i pi > si ; -- este nodo seria tanto "mi", 
 * como "<si 0i pi>".
 */

typedef struct _SintaxFuncion {
  int esRepeticion;
  union {
    char* idFunc; // Si no es repeticion doy su id.
    Lista funcRepeticion; // Esta lista guardara estructuras SintaxFuncion
  };
}* SintaxFuncion;

typedef struct _SintaxDeff {
  char* nombreFuncion;
  Lista listaFunc; // Este lista guardara estructuras SintaxFuncion
}* SintaxDeff;

typedef struct _SintaxApply {
  char* idFuncAplicar;
  int esListaPorExtension;
  union {
    Lista lista; // La union toma este valor si esListaPorExtension es 1.
    char* idLista;
  };
}* SintaxApply;

typedef struct _SintaxSearch {
  int sonListasPorExt;
  union {
    struct {
      Lista listasOrigen; // Listas de listas de enteros.
      Lista listasDestino;
    } ListasPorExt;
    struct {
      Lista listasOrigen; // Listas de punteros a char.
      Lista listasDestino;
    } ListasPorId;
  };
}* SintaxSearch;

/**
 * Estructura que representa un arbol de sintaxis abstracto (AST) lineal. 
 * La union tomara los siguientes valores, segun el valor de tipoSentencia: 
 * - SENTENCIA_DEFF, a la union se le dara el valor del campo "deff".
 * - SENTENCIA_DEFL, a la union se le dara el valor del campo "defl".
 * - SENTENCIA_APPLY, a la union se le dara el valor del campo "apply".
 * - SENTENCIA_SEARCH, a la union se le dara el valor del campo "search".
 */
typedef struct _AST {
  TipoSentencia tipoSentencia;
  union {
    SintaxDeff deff;
    Lista defl;
    SintaxApply apply;
    SintaxSearch search;
  };
}* AST;

/**
 * ast_crear: La funcion crea un AST vacio.
 */
AST ast_crear();

/**
 * ast_destruir: Dado un AST la funcion libera la memoria pedida por este.
 */
AST ast_destruir(AST arbol);

/**
 * sintax_funcion_crear: La funcion crea una estructura funcion vacia.
 */
SintaxFuncion sintax_funcion_crear();

/**
 * sintax_funcion_destruir: La funcion libera la memoria pedida por una
 * estructura SintaxFuncion.
 */
SintaxFuncion sintax_funcion_destruir(SintaxFuncion func);

/**
 * sintax_deff_crear: La funcion crea una estructura de sintaxis de la sentencia
 * deff vacia.
 */
SintaxDeff sintax_deff_crear();

/**
 * sintax_deff_destruir: Dada una estructura sintaxis deff, la funcion libera
 * la memoria pedida por esta.
 */
SintaxDeff sintax_deff_destruir(SintaxDeff deff);

/**
 * sintax_apply_crear: La funcion crea una estructura de sintaxis de la 
 * sentencia apply "vacia".
 */
SintaxApply sintax_apply_crear();

/**
 * sintax_apply_destruir: Dada una estructura sintaxis apply, la funcion libera
 * la memoria pedida por esta.
 */
SintaxApply sintax_apply_destruir(SintaxApply apply);

/**
 * sintax_search_crear: La funcion crea una estructura de sintaxis de la 
 * sentencia search "vacia".
 */
SintaxSearch sintax_search_crear();

/**
 * sintax_search_destruir: Dada una estructura sintaxis search, la funcion 
 * libera la memoria pedida por esta.
 */
SintaxSearch sintax_search_destruir(SintaxSearch search);

#endif // AST_H
#ifndef VALOR_SENTENCIA_H
#define VALOR_SENTENCIA_H

#include "../funciones/funciones.h"
#include "../../genericas/lista/lista.h"
#include "../../../utils/utils-tipo-sentencia.h"

/**
 * Estructura para guardar la informacion de la sentencia que se esta
 * ejecutando con el fin de eventualmente imprimir por pantalla el resultado o
 * cargar a la estructura el dato definido segun corrresponda. La union tomara 
 * el valor segun tipoSentencia:
 * - SENTENCIA_DEFF, a la union se le dara el valor del campo funcDeff
 * - SENTENCIA_DEFL, a la union se le dara el valor del campo listaDefl.
 * - SENTENCIA_APPLY, a la union se le dara el valor del campo listaApply.
 * - SENTENCIA_SEARCH, a la union se le dara el valor del campo funcSearch.
 */
typedef struct _ValorSentencia {
  TipoSentencia tipoSentencia;
  union {
    Funcion funcDeff;
    Lista listaDefl;
    Lista listaApply;
    Funcion funcSearch;
  };
}* ValorSentencia;

/**
 * valor_sentencia_crear: La funcion crea una estructura ValorSentencia vacia.
 */
ValorSentencia valor_sentencia_crear();

/**
 * valor_sentencia_destruir: Dada una estructura ValorSentencia, la funcion 
 * libera la memoria pedida por esta.
 */
ValorSentencia valor_sentencia_destruir(ValorSentencia sentVal);

#endif // VALOR_SENTENCIA_H
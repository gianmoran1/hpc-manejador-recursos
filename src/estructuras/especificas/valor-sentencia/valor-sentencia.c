#include <stdlib.h>
#include <assert.h>

#include "valor-sentencia.h"

/**
 * valor_sentencia_crear: La funcion crea una estructura ValorSentencia vacia.
 */
ValorSentencia valor_sentencia_crear() {
  ValorSentencia sentVal = malloc(sizeof(struct _ValorSentencia));
  assert(sentVal);
  sentVal->tipoSentencia = SENTENCIA_NULA;
  return sentVal;
}

/**
 * valor_sentencia_destruir: Dada una estructura ValorSentencia, la funcion
 * libera la memoria pedida por esta.
 */
ValorSentencia valor_sentencia_destruir(ValorSentencia sentVal) {
  if (sentVal) {
    if (sentVal->tipoSentencia == SENTENCIA_DEFF) {
      funcion_destruir(sentVal->funcDeff);
    } else if (sentVal->tipoSentencia == SENTENCIA_DEFL) {
      lista_destruir(sentVal->listaDefl);
    } else if (sentVal->tipoSentencia == SENTENCIA_APPLY) {
      lista_destruir(sentVal->listaApply);
    } else if (sentVal->tipoSentencia == SENTENCIA_SEARCH) {
      funcion_destruir(sentVal->funcSearch);
    }
    free(sentVal);
    sentVal = NULL;
  }
  return sentVal;
}

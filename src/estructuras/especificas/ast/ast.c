#include <stdlib.h>
#include <assert.h>

#include "ast.h"
#include "../../../utils/utils-str.h"

/**
 * ast_crear: La funcion crea un AST vacio.
 */
AST ast_crear() {
  AST arbol = malloc(sizeof(struct _AST));
  assert(arbol);
  arbol->tipoSentencia = SENTENCIA_NULA;
  return arbol;
}

/**
 * ast_destruir: Dado un AST la funcion libera la memoria pedida por este.
 */
AST ast_destruir(AST arbol) {
  if (arbol) {
    if (arbol->tipoSentencia == SENTENCIA_DEFF) {
      arbol->deff = sintax_deff_destruir(arbol->deff);
    } else if (arbol->tipoSentencia == SENTENCIA_DEFL) {
      arbol->defl = lista_destruir(arbol->defl);
    } else if (arbol->tipoSentencia == SENTENCIA_APPLY) {
      arbol->apply = sintax_apply_destruir(arbol->apply);
    } else if (arbol->tipoSentencia == SENTENCIA_SEARCH) {
      arbol->search = sintax_search_destruir(arbol->search);
    }
    free(arbol);
    arbol = NULL;
  }
  return arbol;
}

/**
 * sintax_funcion_crear: La funcion crea una estructura funcion vacia.
 */
SintaxFuncion sintax_funcion_crear() {
  SintaxFuncion func = malloc(sizeof(struct _SintaxFuncion));
  assert(func);
  func->esRepeticion = -1;
  return func;
}

/**
 * sintax_funcion_destruir: La funcion libera la memoria pedida por una
 * estructura SintaxFuncion.
 */
SintaxFuncion sintax_funcion_destruir(SintaxFuncion func) {
  if (func) { 
    if (func->esRepeticion == 0) {
      string_destruir(func->idFunc);
    } else if (func->esRepeticion == 1) {
      lista_destruir(func->funcRepeticion);
    }
    free(func);
    func = NULL;
  }
  return func;
}

/**
 * sintax_deff_crear: La funcion crea una estructura de sintaxis de la sentencia
 * deff vacia.
 */
SintaxDeff sintax_deff_crear() {
  SintaxDeff deff = malloc(sizeof(struct _SintaxDeff));
  assert(deff);
  deff->nombreFuncion = NULL;
  deff->listaFunc = NULL;
  return deff;
}

/**
 * sintax_deff_destruir: Dada una estructura sintaxis deff, la funcion libera
 * la memoria pedida por esta.
 */
SintaxDeff sintax_deff_destruir(SintaxDeff deff) {
  if (deff) {
    string_destruir(deff->nombreFuncion);
    lista_destruir(deff->listaFunc);
    free(deff);
    deff = NULL;
  }
  return deff;
}

/**
 * sintax_apply_crear: La funcion crea una estructura de sintaxis de la 
 * sentencia apply vacia.
 */
SintaxApply sintax_apply_crear() {
  SintaxApply apply = malloc(sizeof(struct _SintaxApply));
  assert(apply);
  apply->idFuncAplicar = NULL;
  apply->esListaPorExtension = -1;
  return apply;
}

/**
 * sintax_apply_destruir: Dada una estructura sintaxis apply, la funcion libera
 * la memoria pedida por esta.
 */
SintaxApply sintax_apply_destruir(SintaxApply apply) {
  if (apply) {
    string_destruir(apply->idFuncAplicar);
    if (apply->esListaPorExtension == 0) {
      string_destruir(apply->idLista);
    } else if (apply->esListaPorExtension == 1) {
      lista_destruir(apply->lista);
    }
    free(apply);
    apply = NULL;
  }
  return apply;
}

/**
 * sintax_search_crear: La funcion crea una estructura de sintaxis de la 
 * sentencia search vacia.
 */
SintaxSearch sintax_search_crear() {
  SintaxSearch search = malloc(sizeof(struct _SintaxSearch));
  assert(search);
  search->sonListasPorExt = -1;
  return search;
}

/**
 * sintax_search_destruir: Dada una estructura sintaxis search, la funcion 
 * libera la memoria pedida por esta.
 */
SintaxSearch sintax_search_destruir(SintaxSearch search) {
  if (search) {
    if (search->sonListasPorExt == 1) {
      lista_destruir(search->ListasPorExt.listasOrigen);
      lista_destruir(search->ListasPorExt.listasDestino);
    } else if (search->sonListasPorExt == 0) {
      lista_destruir(search->ListasPorId.listasOrigen);
      lista_destruir(search->ListasPorId.listasDestino);
    }
    free(search);
    search = NULL;
  }
  return search;
}

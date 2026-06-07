#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "../../include/parser.h"
#include "../utils/utils-str.h"
#include "../utils/utils-enteros.h"
#include "../estructuras/especificas/token/token.h"

/**
 * sintax_funcion_no_copiar: Dada una estructura SintaxFuncion, la funcion la
 * retorna.
 */
static SintaxFuncion sintax_funcion_no_copiar(SintaxFuncion func) {
  return func;
}

// Parser Deff------------------------------------------------------------------

/**
 * parser_funcion: Funcion interna de parser_deff que dada una cola de tokens
 * y un entero que indica si estoy en una repeticion, la funcion crea una lista
 * de estructuras SintaxFuncion y la devuelve si se trata de una sentencia
 * valida o NULL si no es una sentencia valida.
 */
static Lista parser_funcion(Cola colaTokens, int repeticion) {
  Lista listaFunc = lista_crear((FuncionCopiadora) sintax_funcion_no_copiar,
                            (FuncionDestructora) sintax_funcion_destruir);

  Token tokenActual;
  int funcValida = 1;
  int funcTerminada = 0;

  while (!funcTerminada && funcValida) {
    tokenActual = cola_desencolar(colaTokens);
    SintaxFuncion funcActual = NULL;

    if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
      funcActual = sintax_funcion_crear();
      funcActual->esRepeticion = 0;
      funcActual->idFunc = string_copiar(tokenActual->valorCadena);
      lista_agregar_final(listaFunc, funcActual);
      token_destruir(tokenActual);
    } else if (tokenActual && tokenActual->tipoToken == TOKEN_MENOR) {
      token_destruir(tokenActual);
      funcActual = sintax_funcion_crear();
      funcActual->esRepeticion = 1;
      // Llamo recursivamente a la funcion para ver si lo de dentro de la 
      // repeticion es una funcion valida.
      funcActual->funcRepeticion = parser_funcion(colaTokens, 1);

      if (!funcActual->funcRepeticion || 
          lista_es_vacia(funcActual->funcRepeticion) == 1) {
        funcValida = 0;
        funcActual = sintax_funcion_destruir(funcActual);
      } else {
        lista_agregar_final(listaFunc, funcActual);
      }
    } else if (tokenActual && tokenActual->tipoToken == TOKEN_MAYOR) {
      token_destruir(tokenActual);
      if (repeticion == 1) { // Debo estar una repeticion.
        funcTerminada = 1;
      } else {
        funcValida = 0;
      }
    } else if (tokenActual && tokenActual->tipoToken == TOKEN_PUNTO_Y_COMA && 
              cola_es_vacia(colaTokens) == 1) {
      token_destruir(tokenActual);
      if (repeticion == 1) { // No debo estar en una repeticion
        funcValida = 0;
      } else {
        funcTerminada = 1;
      }
    } else {
      token_destruir(tokenActual);
      funcValida = 0;
    }
  }

  if (!funcValida || !funcTerminada) {
    listaFunc = lista_destruir(listaFunc);
  }
  return listaFunc;
}

/**
 * parser_deff: Dada una cola de tokens, la funcion retorna una estructura
 * SintaxDeff si la setencia es valida o NULL en caso contrario.
 */
static SintaxDeff parser_deff(Cola colaTokens) {
  SintaxDeff deff = sintax_deff_crear();
  Token tokenActual = cola_desencolar(colaTokens);

  if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
    deff->nombreFuncion = string_copiar(tokenActual->valorCadena);
    token_destruir(tokenActual);
    tokenActual = cola_desencolar(colaTokens);

    if (tokenActual && tokenActual->tipoToken == TOKEN_IGUAL) {
      token_destruir(tokenActual);
      deff->listaFunc = parser_funcion(colaTokens, 0);
      if (!deff->listaFunc || lista_es_vacia(deff->listaFunc) == 1) {
        deff = sintax_deff_destruir(deff);
      }
    } else { // No hay igual
      token_destruir(tokenActual);
      deff = sintax_deff_destruir(deff);
    }
  } else { // No hay ID
    token_destruir(tokenActual);
    deff = sintax_deff_destruir(deff);
  }
  return deff;
}

// Parser Defl------------------------------------------------------------------

/**
 * parser_lista: Dada una cola de tokens y una lista de enteros inicializada 
 * previamente, la funcion carga los datos a la lista en caso de que la 
 * sentencia posea una lista valida, y en dicho caso devuelve la lista o en caso
 * de sentencia no valida retorna NULL y libera la memoria pedida por la lista. 
 */
static Lista parser_lista(Cola colaTokens, Lista listaEnteros) {
  Token tokenActual = cola_desencolar(colaTokens);

  if (tokenActual && tokenActual->tipoToken == TOKEN_CORCHETE_IZQUIERDO) {
    token_destruir(tokenActual);
    int listaVacia = 1;
    int listaValida = 1;
    int listaTerminada = 0;

    while (listaValida && !listaTerminada) {
      tokenActual = cola_desencolar(colaTokens);

      if (tokenActual && tokenActual->tipoToken == TOKEN_NUMERO) {
        lista_agregar_final(listaEnteros, tokenActual->valorNumero);
        token_destruir(tokenActual);
        tokenActual = cola_desencolar(colaTokens);
        listaVacia = 0;

        if (tokenActual && tokenActual->tipoToken == TOKEN_CORCHETE_DERECHO) {
          listaTerminada = 1;
        } else if (tokenActual && tokenActual->tipoToken != TOKEN_COMA) {
          listaValida = 0;
        }
      } else if (tokenActual && tokenActual->tipoToken == TOKEN_CORCHETE_DERECHO 
                && listaVacia) {
        listaTerminada = 1;
      } else { // La lista no es vacia y no contiene numeros.
        listaValida = 0;
      }
      token_destruir(tokenActual);
    }

    if (!listaValida || !listaTerminada){
      listaEnteros = lista_destruir(listaEnteros);
    }

  } else { // No hay corchete izquierdo.
    token_destruir(tokenActual);
    listaEnteros = lista_destruir(listaEnteros);
  }

  return listaEnteros;
}

/**
 * parser_defl: Dada una cola de tokens, la funcion retorna una lista
 * si la setencia es valida o NULL en caso contrario.
 */
static Lista parser_defl(Cola colaTokens) {
  Lista defl = lista_crear((FuncionCopiadora) entero_copiar, 
                      (FuncionDestructora) entero_destruir);
  Token tokenActual = cola_desencolar(colaTokens);

  if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
    lista_asignar_id(defl, tokenActual->valorCadena);
    token_destruir(tokenActual);
    tokenActual = cola_desencolar(colaTokens);

    if (tokenActual && tokenActual->tipoToken == TOKEN_IGUAL) {
      token_destruir(tokenActual);

      if ((defl = parser_lista(colaTokens, defl))) {
        tokenActual = cola_desencolar(colaTokens);

        // No termina con punto y coma o tiene mas palabras.
        if (!tokenActual || tokenActual->tipoToken != TOKEN_PUNTO_Y_COMA ||
            !cola_es_vacia(colaTokens)) {
          defl = lista_destruir(defl);
        }
        token_destruir(tokenActual);
      } else { // No hay una lista.
        defl = lista_destruir(defl);
      }
    } else { // No hay un igual.
      token_destruir(tokenActual);
      defl = lista_destruir(defl);
    }
  } else { // No hay identificador.
    token_destruir(tokenActual);
    defl = lista_destruir(defl);
  }
  return defl;
}

// Parser Apply-----------------------------------------------------------------

/**
 * parser_apply: Dada una cola de tokens, la funcion retorna una estructura
 * SintaxApply si la sentencia es valida o NULL en caso contrario.
 */
static SintaxApply parser_apply(Cola colaTokens) {
  SintaxApply apply = sintax_apply_crear();
  Token tokenActual = cola_desencolar(colaTokens);

  // Identificador de funcion.
  if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
    apply->idFuncAplicar = string_copiar(tokenActual->valorCadena);
    token_destruir(tokenActual);

    tokenActual = cola_primero(colaTokens);
    Lista listaPorExtension = lista_crear((FuncionCopiadora) entero_copiar, 
                                        (FuncionDestructora) entero_destruir);

    // Identificador de lista.
    if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
      tokenActual = cola_desencolar(colaTokens);
      apply->esListaPorExtension = 0;
      apply->idLista = string_copiar(tokenActual->valorCadena);
      token_destruir(tokenActual);
      lista_destruir(listaPorExtension);
    } else if ((listaPorExtension = 
                                parser_lista(colaTokens, listaPorExtension))) {
      apply->esListaPorExtension = 1;
      apply->lista = listaPorExtension;
    } else {
      lista_destruir(listaPorExtension);
      apply = sintax_apply_destruir(apply);
      return apply;
    }

    tokenActual = cola_desencolar(colaTokens);
    // No termina con punto y coma o tiene mas palabras.
    if (!tokenActual || tokenActual->tipoToken != TOKEN_PUNTO_Y_COMA ||
        !cola_es_vacia(colaTokens)) {
      apply = sintax_apply_destruir(apply);
    }
    token_destruir(tokenActual);
  } else { // No hay identificador de funcion
    token_destruir(tokenActual);
    apply = sintax_apply_destruir(apply);
  }
  return apply;
}

// Parser Search----------------------------------------------------------------

static Lista lista_no_copia(Lista lista) {
  return lista;
}

/**
 * parser_search_listas_extension: Funcion interna de parser_search, que dada 
 * una cola de tokens y la estructura SintaxSearch a cargar, analiza si en 
 * la sentencia search los pares de listas dados son por extension. En dicho 
 * caso carga los valores correspondientes. Caso contrario libera la memoria de 
 * la estructura SintaxSearch y devuelve NULL.
 */
static SintaxSearch parser_search_listas_extension(Cola colaTokens,
                                                    SintaxSearch search) {
  search->ListasPorExt.listasOrigen = 
                                  lista_crear((FuncionCopiadora) lista_no_copia, 
                                          (FuncionDestructora) lista_destruir);
  search->ListasPorExt.listasDestino = 
                                  lista_crear((FuncionCopiadora) lista_no_copia,
                                          (FuncionDestructora) lista_destruir);

  Token tokenActual;
  int paresValidos = 1;
  int lecturaTerminada = 0;

  while (paresValidos && !lecturaTerminada) {
    // Lista origen.
    Lista listaOrigen = lista_crear((FuncionCopiadora) entero_copiar,
                                    (FuncionDestructora) entero_destruir);
    if ((listaOrigen = parser_lista(colaTokens, listaOrigen))) {
      lista_agregar_final(search->ListasPorExt.listasOrigen, listaOrigen);
      tokenActual = cola_desencolar(colaTokens);

      if (tokenActual && tokenActual->tipoToken == TOKEN_COMA) {
        token_destruir(tokenActual);

        Lista listaDestino = lista_crear((FuncionCopiadora) entero_copiar,
                                        (FuncionDestructora) entero_destruir);
        if ((listaDestino = parser_lista(colaTokens, listaDestino))) {
          lista_agregar_final(search->ListasPorExt.listasDestino, listaDestino);
          // Me fijo si hay mas pares o si la lista termino.
          tokenActual = cola_desencolar(colaTokens);
          if (tokenActual && tokenActual->tipoToken == TOKEN_LLAVE_DERECHA) {
            lecturaTerminada = 1;
          } else if (!tokenActual || 
                    tokenActual->tipoToken != TOKEN_PUNTO_Y_COMA) {
            paresValidos = 0;
          }
          token_destruir(tokenActual);
        } else { // No hay lista destino
          paresValidos = 0;
        }
      } else { // No hay una coma
        token_destruir(tokenActual);
        paresValidos = 0;
      }
    } else { // No lista origen.
      paresValidos = 0;
    }
  }

  if (!lecturaTerminada || !paresValidos) {
    search = sintax_search_destruir(search);
  }
  return search;
}

/**
 * parser_search_listas_id: Funcion interna de parser_search, que dada una cola
 * de tokens y la estructura SintaxSearch a cargar, analiza si en la sentencia 
 * search los pares de listas dados son por id. En dicho caso carga los valores 
 * correspondientes. Caso contrario libera la memoria de la estructura
 * SintaxSearch y devuelve NULL.
 */
static SintaxSearch parser_search_listas_id(Cola colaTokens, 
                                            SintaxSearch search) {
  search->ListasPorId.listasOrigen = 
                                  lista_crear((FuncionCopiadora) string_copiar,
                                          (FuncionDestructora) string_destruir);
  search->ListasPorId.listasDestino = 
                                  lista_crear((FuncionCopiadora) string_copiar,
                                          (FuncionDestructora) string_destruir);

  Token tokenActual;
  int paresValidos = 1;
  int lecturaTerminada = 0;

  while (paresValidos && !lecturaTerminada) {
    tokenActual = cola_desencolar(colaTokens);

    // Identificador de lista origen.
    if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {

      lista_agregar_final(search->ListasPorId.listasOrigen, 
                          tokenActual->valorCadena);
      token_destruir(tokenActual);
      tokenActual = cola_desencolar(colaTokens);

      if (tokenActual && tokenActual->tipoToken == TOKEN_COMA) {
        token_destruir(tokenActual);
        tokenActual = cola_desencolar(colaTokens);

        if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
          lista_agregar_final(search->ListasPorId.listasDestino, 
                              tokenActual->valorCadena);

          token_destruir(tokenActual);

          // Me fijo si hay mas pares o si la lista termino.
          tokenActual = cola_desencolar(colaTokens);
          if (tokenActual && tokenActual->tipoToken == TOKEN_LLAVE_DERECHA) {
            lecturaTerminada = 1;
          } else if (!tokenActual || 
                      tokenActual->tipoToken != TOKEN_PUNTO_Y_COMA) {
            paresValidos = 0;
          }
          token_destruir(tokenActual);
        } else { // No hay un segundo identificador
          token_destruir(tokenActual);
          paresValidos = 0;
        }
      } else { // No hay una coma
        token_destruir(tokenActual);
        paresValidos = 0;
      }
    } else { // No hay ID de lista origen.
      token_destruir(tokenActual);
      paresValidos = 0;
    }
  }

  if (!lecturaTerminada || !paresValidos) {
    search = sintax_search_destruir(search);
  }
  return search;
}

/**
 * parser_search: Dada una cola de tokens, la funcion retorna una estructura
 * SintaxSearch si la sentencia es valida o NULL en caso contrario.
 */
static SintaxSearch parser_search(Cola colaTokens) {
  SintaxSearch search = sintax_search_crear();
  Token tokenActual = cola_desencolar(colaTokens);

  if (tokenActual && tokenActual->tipoToken == TOKEN_LLAVE_IZQUIERDA) {
    token_destruir(tokenActual);
    tokenActual = cola_primero(colaTokens);

    if (tokenActual && tokenActual->tipoToken == TOKEN_IDENTIFICADOR) {
      search->sonListasPorExt = 0;
      search = parser_search_listas_id(colaTokens, search);
    } else if (tokenActual && 
                tokenActual->tipoToken == TOKEN_CORCHETE_IZQUIERDO) {
      search->sonListasPorExt = 1;
      search = parser_search_listas_extension(colaTokens, search);
    } else {
      search = sintax_search_destruir(search);
    }

    if (search) {
      tokenActual = cola_desencolar(colaTokens);
      if (!tokenActual || tokenActual->tipoToken != TOKEN_PUNTO_Y_COMA || 
          !cola_es_vacia(colaTokens)) {
        search = sintax_search_destruir(search);
      }
      token_destruir(tokenActual);
    }
  } else { // No hay llave izquierda
    token_destruir(tokenActual);
    search = sintax_search_destruir(search);
  }
  return search;
}

/**
 * parser: Dada una cola de tokens, la funcion devuelve un AST con la
 * informacion de esos tokens. Si la sentencia no es valida devuelve NULL, 
 * liberando la memoria pedida por el AST, ademas en todos los casos libera
 * la memoria pedida por la cola.
 */
AST parser(Cola colaTokens) {
  AST arbol = ast_crear();

  if (cola_es_vacia(colaTokens) == 0) {
    Token tokenActual = cola_desencolar(colaTokens);
    if (tokenActual->tipoToken == TOKEN_DEFF) {
      arbol->tipoSentencia = SENTENCIA_DEFF;
      arbol->deff = parser_deff(colaTokens);
      if (!arbol->deff) {
        arbol = ast_destruir(arbol);
      }
    } else if (tokenActual->tipoToken == TOKEN_DEFL) {
      arbol->tipoSentencia = SENTENCIA_DEFL;
      arbol->defl = parser_defl(colaTokens);
      if (!arbol->defl) {
        arbol = ast_destruir(arbol);
      }
    } else if (tokenActual->tipoToken == TOKEN_APPLY) {
      arbol->tipoSentencia = SENTENCIA_APPLY;
      arbol->apply = parser_apply(colaTokens);
      if (!arbol->apply) {
        arbol = ast_destruir(arbol);
      }
    } else if (tokenActual->tipoToken == TOKEN_SEARCH) {
      arbol->tipoSentencia = SENTENCIA_SEARCH;
      arbol->search = parser_search(colaTokens);
      if (!arbol->search) {
        arbol = ast_destruir(arbol);
      }
    } else {
      arbol = ast_destruir(arbol);
    }
    token_destruir(tokenActual);
  }

  cola_destruir(colaTokens);
  return arbol;
}

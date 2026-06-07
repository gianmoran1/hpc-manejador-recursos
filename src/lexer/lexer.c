#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "../../include/lexer.h"
#include "../utils/utils-str.h"
#include "../estructuras/especificas/token/token.h"

/**
 * token_no_copia: Dado un token, la funcion lo retorna.
 */
static Token token_no_copia(Token token) {
  return token;
}

/**
 * lexer: Dado un string (linea), la funcion retorna una cola de tokens 
 * correspondiente a la linea dada. El lexer libera la memoria pedida por
 * la linea. Ademas si no es una linea valida retorna NULL.
 */
Cola lexer(char* linea) {
  Cola colaTokens = cola_crear((FuncionCopiadora) token_no_copia, 
                                (FuncionDestructora) token_destruir);

  char* cadenaToken = string_tokenizer(linea);
  int tokenValido = (cadenaToken) ? 1 : 0;
  Token tokenActual;

  while (cadenaToken && tokenValido) {
    tokenActual =  token_crear(cadenaToken);
    if (tokenActual) {
      cola_encolar(colaTokens, tokenActual);
      cadenaToken = string_destruir(cadenaToken);
      cadenaToken = string_tokenizer(linea);
    } else {
      tokenValido = 0;
    }
  }

  if (!tokenValido) {
    cadenaToken = string_destruir(cadenaToken);
    colaTokens = cola_destruir(colaTokens);
  }

  string_destruir(linea);

  return colaTokens;
}

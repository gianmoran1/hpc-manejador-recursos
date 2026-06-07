#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "token.h"
#include "../../../utils/utils-str.h"

/**
 * token_clasifica: Dado un string, la funcion lo clasifica con el criterio 
 * de los tokens.
 */
static TiposDeToken token_clasifica(char* str) {
  if (str) {
    // Palabras reservadas.
    if (strcmp(str, "deff") == 0) return TOKEN_DEFF;
    if (strcmp(str, "defl") == 0) return TOKEN_DEFL;
    if (strcmp(str, "apply") == 0) return TOKEN_APPLY;
    if (strcmp(str, "search") == 0) return TOKEN_SEARCH;

    // Signos validos.
    if (strcmp(str, "=") == 0) return TOKEN_IGUAL;
    if (strcmp(str, ",") == 0) return TOKEN_COMA;
    if (strcmp(str, ";") == 0) return TOKEN_PUNTO_Y_COMA;
    if (strcmp(str, "<") == 0) return TOKEN_MENOR;
    if (strcmp(str, ">") == 0) return TOKEN_MAYOR;
    if (strcmp(str, "[") == 0) return TOKEN_CORCHETE_IZQUIERDO;
    if (strcmp(str, "]") == 0) return TOKEN_CORCHETE_DERECHO;
    if (strcmp(str, "{") == 0) return TOKEN_LLAVE_IZQUIERDA;
    if (strcmp(str, "}") == 0) return TOKEN_LLAVE_DERECHA;

    if (string_evalua_caracteres(str, isdigit) == 1) return TOKEN_NUMERO;
    if (string_evalua_caracteres(str, isalnum) == 1) return TOKEN_IDENTIFICADOR;
  }
  return TOKEN_ERROR; // Token no valido.
}

/**
 * token_crear: Dado un string, la funcion crea un token con ese string. Si el
 * string no corresponde a un tipo de token valido devuelve NULL.
 */
Token token_crear(char* str) {
  Token token = NULL;
  TiposDeToken tipoToken = token_clasifica(str);
  if (tipoToken != TOKEN_ERROR) {
    token = malloc(sizeof(struct _Token));
    assert(token);
    token->tipoToken = tipoToken;

    if (tipoToken == TOKEN_NUMERO) {
      token->valorNumero = malloc(sizeof(int));
      assert(token->valorNumero);
      *(token->valorNumero) = atoi(str); 
    } else if (tipoToken == TOKEN_IDENTIFICADOR) {
      token->valorCadena = string_copiar(str);
    }
  }
  return token;
}

/**
 * token_destruir: Dado un token, la funcion libera la memoria pedida por este.
 */
Token token_destruir(Token token) {
  if (token) {
    if (token->tipoToken == TOKEN_IDENTIFICADOR) {
      string_destruir(token->valorCadena);
    } else if (token->tipoToken == TOKEN_NUMERO) {
      free(token->valorNumero);
    }
    free(token);
    token = NULL;
  }
  return token;
}

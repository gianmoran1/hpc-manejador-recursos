#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
  // Palabras reservadas.
  TOKEN_DEFF,
  TOKEN_DEFL,
  TOKEN_APPLY,
  TOKEN_SEARCH,

  // Simbolos validos.
  TOKEN_IGUAL,
  TOKEN_COMA,
  TOKEN_PUNTO_Y_COMA,
  TOKEN_MENOR,
  TOKEN_MAYOR,
  TOKEN_CORCHETE_IZQUIERDO,
  TOKEN_CORCHETE_DERECHO,
  TOKEN_LLAVE_IZQUIERDA,
  TOKEN_LLAVE_DERECHA,

  TOKEN_IDENTIFICADOR,
  TOKEN_NUMERO,

  TOKEN_ERROR
} TiposDeToken;

/**
 * Estructura que representa un token, donde se almacena el tipo del token y 
 * el valor que este tenga, si corresponde.
 */
typedef struct _Token {
  TiposDeToken tipoToken;
  union {
    char* valorCadena;
    int* valorNumero;
  };
}* Token;

/**
 * token_crear: Dado un string, la funcion crea un token con ese string. Si el
 * string no corresponde a un token valido devuelve NULL.
 */
Token token_crear(char* str);

/**
 * token_destruir: Dado un token, la funcion libera la memoria pedida por este.
 */
Token token_destruir(Token token);

#endif // TOKEN_H
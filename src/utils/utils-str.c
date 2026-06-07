#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "utils-str.h"

#define MAX_LONG_STR 1024

/**
 * string_evalua_caracteres: Dado un string y una funcion para evaluar si los
 * caracteres del string dado cumplen cierta condicion, la funcion retorna 1 
 * si el string cumple la condicion o 0 en caso contrario. Si el string es nulo
 * devuelve -1.
 */
int string_evalua_caracteres(char* str, int (*CondicionCaracteres) (int )) {
  int cumpleCondicion = -1;
  if (str) {
    cumpleCondicion = 1;
    while (*str && cumpleCondicion) {
      if (!CondicionCaracteres(*str)) {
        cumpleCondicion = 0;
      }
      str++;
    }
  }
  return cumpleCondicion;
}

/**
 * string_copiar: Dado un string, la funcion retorna una copia del mismo.
 */
char* string_copiar(char* str) {
  char* strCopia = NULL;
  if (str) {
    strCopia = malloc(strlen(str) + 1);
    assert(strCopia);
    strcpy(strCopia, str);
  }
  return strCopia;
}

/**
 * string_destruir: Dado un string, la funcion libera la memoria pedida por 
 * este.
 */
char* string_destruir(char* str) {
  if (str) {
    free(str);
    str = NULL;
  }
  return str;
}

/**
 * string_tokenizer: Dado un string, la funcion lo separa en tokens, esto es,
 * saca los espacios, tabulaciones, retornos de carro, etc. y me devuelve la 
 * primer subcadena del string dado que contenga cualquier caracter, dejando de
 * lado estos espacios. La funcion va modificando el string dado colocando 
 * espacios en los caracteres leidos para futuros usos del mismo.
 * Ejemplo: Dado "   soy un    string", en una primer llamada me devuelve "soy".
 */
char* string_tokenizer(char* linea) {
  char* str = NULL;
  if (linea) {
    while (*linea == ' ' || *linea == '\r' || *linea == '\t') {
      linea++;
    }

    if (*linea != '\0') {
      int tamStr = MAX_LONG_STR;
      str = malloc(tamStr);
      assert(str);
      int i = 0;

      while (*(linea + i) != ' '  && *(linea + i) != '\r' && 
            *(linea + i) != '\t' && *(linea + i) != '\0') {

        *(str + i) = *(linea + i);
        *(linea + i) = ' ';
        i++;

        if (i == tamStr) {
          tamStr *= 2;
          str = realloc(str, tamStr);
          assert(str);
        }
      }
      *(str + i) = '\0';
    }
  }
  return str;
}

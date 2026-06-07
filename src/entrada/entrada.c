#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "../../include/entrada.h"

#define MAXIMA_LONGITUD_PALABRAS 1024

/**
 * obtener_linea_de_consola: La funcion lee por consola hasta que encuentra un 
 * salto de linea. Devuelve un puntero a esa linea.
 */
char* obtener_linea_de_consola(){
  int i = 0;
  int tamStr = MAXIMA_LONGITUD_PALABRAS;
  char* str = malloc(tamStr + 1);
  assert(str);
  char c;

  while ((c = getchar()) != '\n' && c != EOF){
    if (i == tamStr) {
      tamStr *= 2;
      str = realloc(str, tamStr + 1);
      assert(str);
    }
    *(str + i) = c;
    i++;
  }

  *(str + i) = '\0';
  return str;
}

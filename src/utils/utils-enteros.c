#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "utils-enteros.h"

/**
 * entero_copiar: Dado un puntero entero, la funcion retorna una copia fisica 
 * del mismo.
 */
int* entero_copiar(int* entero) {
  int* enteroCopia = NULL;
  if (entero) {
    enteroCopia = malloc(sizeof(int));
    assert(enteroCopia);
    *enteroCopia = *entero;
  }
  return enteroCopia;
}

/**
 * entero_imprimir: Dado un puntero entero, la funcion imprime su valor por 
 * pantalla.
 */
void entero_imprimir(int* entero) {
  if (entero) {
    printf("%d ", *entero);
  }
}

/**
 * entero_comparar: Dados dos punteros enteros, retorna un entero negativo si el
 * primer entero es menor que el segundo, 0 si son iguales y un entero
 * positivo el primer entero es mayor que el segundo.
 */
int entero_comparar(int* entero1, int* entero2) {
  assert(entero1 && entero2);
  return *entero1 - *entero2;
}

/**
 * entero_destruir: Dado un puntero entero, la funcion libera la memoria pedida 
 * por este.
 */
int* entero_destruir(int* entero) {
  if (entero) {
    free(entero);
    entero = NULL;
  }
  return entero;
}

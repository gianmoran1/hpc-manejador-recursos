#ifndef UTILS_ENTEROS_H
#define UTILS_ENTEROS_H

/**
 * entero_copiar: Dado un puntero entero, la funcion retorna una copia fisica 
 * del mismo.
 */
int* entero_copiar(int* entero);

/**
 * entero_imprimir: Dado un puntero entero, la funcion imprime su valor por 
 * pantalla.
 */
void entero_imprimir(int* entero);

/**
 * entero_comparar: Dados dos punteros enteros, retorna un entero negativo si el
 * primer entero es menor que el segundo, 0 si son iguales y un entero
 * positivo el primer entero es mayor que el segundo.
 */
int entero_comparar(int* entero1, int* entero2);

/**
 * entero_destruir: Dado un puntero entero, la funcion libera la memoria pedida 
 * por este.
 */
int* entero_destruir(int* entero);

#endif // UTILS_ENTEROS_H
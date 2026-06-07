#ifndef SALIDA_H
#define SALIDA_H

#include "../src/estructuras/especificas/valor-sentencia/valor-sentencia.h"
#include "../src/estructuras/genericas/tabla-hash/tablahash.h"
#include "../src/estructuras/genericas/lista/lista.h"

/**
 * manejador_de_salida: Dada una estructura ValorSentencia, la tabla hash de 
 * listas, la de funciones y la lista de funciones, la funcion carga los datos 
 * a las estructuras o imprime la salida de la sentencia. Si la sentencia no es 
 * valida devuelve 0. Si la sentencia es valida devuelve 1.
 */
int manejador_de_salida(ValorSentencia sentVal, TablaHash listas, 
                                  TablaHash funcTab, Lista funcLista);

#endif // SALIDA_H
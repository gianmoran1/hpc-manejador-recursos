#ifndef INICIALIZADOR_H
#define INICIALIZADOR_H

#include "../src/estructuras/genericas/tabla-hash/tablahash.h"
#include "../src/estructuras/genericas/lista/lista.h"

typedef struct AlmacenamientoFunciones {
  TablaHash funcTab;
  Lista funcLista;
} AlmacenamientoFunciones;

/**
 * inicializador_funciones: La funcion crea la tabla hash y la lista donde se 
 * guardaran las funciones, ademas agrega las primitivas.
 */
AlmacenamientoFunciones inicializador_funciones();

/**
 * inicializador_listas: La funcion crea la tabla hash donde se guardaran las
 * listas.
 */
TablaHash inicializador_listas();

#endif // INICIALIZADOR_H
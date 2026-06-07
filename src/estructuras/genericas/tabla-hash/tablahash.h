#ifndef TABLAHASH_H
#define TABLAHASH_H

#include "../../../utils/utils-funciones.h"

typedef struct _TablaHash* TablaHash;

/**
 * tablahash_crear: Dada una capacidad, una funcion de copia, otra de
 * comparacion, una de destruccion y una funcion hash, la funcion crea una
 * tabla hash y la devuelve.
 */
TablaHash tablahash_crear(unsigned capacidad, FuncionCopiadora copia,
                          FuncionComparadora comp, FuncionDestructora destr,
                          FuncionHash hash);

/**
 * tablahash_nelems: Retorna el numero de elementos de la tabla dada. Si la
 * tabla es NULL retorna -1.
 */
int tablahash_nelems(TablaHash tabla);

/**
 * tablahash_capacidad: Retorna la capacidad de la tabla dada. Si la tabla es 
 * NULL retorna -1.
 */
int tablahash_capacidad(TablaHash tabla);

/**
 * tablahash_destruir: Dada una tabla hash, la funcion libera la memoria pedida
 * por esta y devuelve NULL.
 */
TablaHash tablahash_destruir(TablaHash tabla);

/**
 * tablahash_insertar: Dada una tabla hash y un dato, la funcion inserta el dato
 * dado en la tabla. De ser necesario, la funcion redimensiona la tabla
 * original.
 */
void tablahash_insertar(TablaHash tabla, void* dato);

/**
 * tablahash_buscar: Dada una tabla hash y un dato a buscar, la funcion retorna 
 * el dato de la tabla que coincida con el dato dado, o NULL si el dato
 * buscado no se encuentra en la tabla.
 */
void* tablahash_buscar(TablaHash tabla, void* dato);

/**
 * tablahash_eliminar: Dada una tabla hash y un dato, la funcion elimina el dato
 * de la tabla en caso de que este se encuentre.
 */
void tablahash_eliminar(TablaHash tabla, void *dato);

#endif // TABLAHASH_H
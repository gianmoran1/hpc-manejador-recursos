#ifndef UTILS_FUNCIONES_H
#define UTILS_FUNCIONES_H

/**
 * Retorna una copia fisica del dato.
 */
typedef void* (*FuncionCopiadora)(void* dato);

/**
 * Libera la memoria pedida por el dato. Devuelve NULL.
 */
typedef void* (*FuncionDestructora)(void* dato);

/**
 * Retorna un entero negativo si dato1 < dato2, 0 si son iguales y un entero
 * positivo si dato1 > dato2.
 */
typedef int (*FuncionComparadora)(void* dato1, void* dato2);

/**
 * Retorna un entero sin signo correspondiente al indice que le corresponde al
 * dato en una tabla hash.
 */
typedef unsigned (*FuncionHash)(void* dato);

/**
 * La funcion visita el dato dado.
 */
typedef void (*FuncionVisitante)(void* dato);

#endif // UTILS_FUNCIONES_H
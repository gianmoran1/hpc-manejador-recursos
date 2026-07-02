#ifndef __UTILS_H__
#define __UTILS_H__

/** Retorna una copia fisica del dato */
typedef void *(*FuncionCopia)(void *dato);

/** Retorna un entero negativo si dato1 < dato2, 0 si son iguales y un entero
 * positivo si dato1 > dato2 */
typedef int (*FuncionComparadora)(void *dato1, void *dato2);

/** Libera la memoria alocada para el dato */
typedef void (*FuncionDestructora)(void *dato);

#endif /* __UTILS_H__ */

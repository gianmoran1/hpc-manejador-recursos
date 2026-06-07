#ifndef LISTA_H
#define LISTA_H

#include "../../../utils/utils-funciones.h"

typedef struct _Lista* Lista;

/**
 * lista_crear: Dada una funcion de copia y otra de destruccion la funcion crea 
 * una lista doblemente enlazada.
 */
Lista lista_crear(FuncionCopiadora copiar, FuncionDestructora destruir);

/**
 * lista_es_vacia: Dada una lista, la funcion retorna 1 si es vacia, o 0 en caso
 * contrario. Si la lista es NULL retorna -1.
 */
int lista_es_vacia(Lista lista);

/**
 * lista_asignar_id: Dada una lista y un string, la funcion asigna ese string
 * como id de la lista en caso de que esta no tenga un id. Se crea una copia
 * del string dado.
 */
void lista_asignar_id(Lista lista, char* id);

/**
 * lista_iterador_inicializar: Dada una lista, la funcion inicializa el iterador
 * de esta, colocando el iterador al inicio de la lista y devolviendo el dato
 * que alli se halle. Devuelve NULL si no hay un primer elemento.
 */
void* lista_iterador_inicializar(Lista lista);

/**
 * lista_iterador_actual: Dada una lista, la funcion devuelve el dato donde en
 * el cual se encuentra el iterador, devuelve NULL si el iterador no se
 * inicializo.
 */
void* lista_iterador_actual(Lista lista);

/**
 * lista_iterador_siguiente: Dada una lista, la funcion mueve el iterador al
 * siguiente nodo y devuelve el dato que alli se halle. En caso de que no haya
 * siguiente devuelve NULL. Tambien devuelve NULL si el iterador nunca se 
 * inicializo.
 */
void* lista_iterador_siguiente(Lista lista);

/**
 * lista_iterador_destruir: Dada una lista, la funcion destruye el iterador,
 * esto es hace NULL.
 */
void lista_iterador_destruir(Lista lista);

/**
 * lista_agregar_inicio: Dada una lista y un dato, la funcion lo agrega al
 * inicio de la lista.
 */
void lista_agregar_inicio(Lista lista, void* dato);

/**
 * lista_agregar_final: Dada una lista y un dato, la funcion lo agrega al
 * final de la lista.
 */
void lista_agregar_final(Lista lista, void* dato);

/**
 * lista_copiar: Dada una lista la funcion realiza una copia fisica de la misma.
 */
Lista lista_copiar(Lista lista);

/**
 * lista_cant_elementos: Dada una lista la funcion devuelve la cantidad de
 * elementos de la misma. Devuelve -1 si la lista es NULL.
 */
int lista_cant_elementos(Lista lista);

/**
 * lista_primero: Dada una lista la funcion devuelve el primer elemento de la
 * misma.
 */
void* lista_primero(Lista lista);

/**
 * lista_ultimo: Dada una lista la funcion devuelve el ultimo elemento de la
 * misma.
 */
void* lista_ultimo(Lista lista);

/**
 * lista_elimina_primero: Dada una lista la funcion elimina el primer elemento.
 * Retorna este elemento.
 */
void* lista_elimina_primero(Lista lista);

/**
 * lista_elimina_ultimo: Dada una lista la funcion elimina el ultimo elemento.
 * Retorna este elemento.
 */
void* lista_elimina_ultimo(Lista lista);

/**
 * lista_comparar: Dadas dos listas, la funcion realiza la comparacion de sus ID
 * retornando un entero negativo si lista1 < lista2, 0 si son iguales y un 
 * entero positivo si lista1 > lista2.
 */
int lista_comparar(Lista lista1, Lista lista2);

/**
 * lista_hash: Dada una lista, la funcion devuelve un entero sin signo
 * correspondiente al indice de hash que se le asigna.
 */
unsigned lista_hash(Lista lista);

/**
 * lista_recorrer: Dada una lista y una funcion visitante, la funcion recorre
 * la lista y aplica la funcion visitante a cada dato de la lista.
 */
void lista_recorrer(Lista lista, FuncionVisitante visitar);

/**
 * listas_iguales: Dadas dos listas y una funcion de comparacion, la funcion 
 * retorna 1 en caso de que las listas sean iguales o 0 si no lo son.
 */
int listas_iguales(Lista lista1, Lista lista2, FuncionComparadora comparar);

/**
 * lista_destruir: Dada una lista la funcion libera la memoria utilizada por la
 * lista y sus elementos.
 */
Lista lista_destruir(Lista lista);

#endif // LISTA_H
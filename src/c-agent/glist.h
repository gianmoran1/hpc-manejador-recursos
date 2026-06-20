#ifndef __GLIST_H__
#define __GLIST_H__

typedef void (*FuncionDestructora)(void *dato);//puntero a funcion, no retorna
typedef void *(*FuncionCopia)(void *dato); //puntero a funcion que retorna void* (puntero)
typedef void (*FuncionVisitante)(void *dato);
typedef int (*Predicado) (void *dato); //puntero a funcion, retorna int
typedef int (*FuncionComparadora)(void *dato1, void *dato2);

typedef struct _GNode {
  void *data; //void puntero
  struct _GNode *next; //puntero a gnode 
} GNode;

typedef GNode *GList;

/**
 * Devuelve una lista vacía.
 */
GList glist_crear();

/**
 * Destruccion de la lista.
 */
void glist_destruir(GList lista, FuncionDestructora destruir);

/**
 * Determina si la lista es vacía.
 */
int glist_vacia(GList lista);

/**
 * Agrega un elemento al inicio de la lista.
 */
GList glist_agregar_inicio(GList lista, void *dato, FuncionCopia copiar);

/**
 * Recorrido de la lista, utilizando la funcion pasada.
 */
void glist_recorrer(GList lista, FuncionVisitante visitar);


#endif /* __GLIST_H__ */
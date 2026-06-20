#include "tablahash.h"
#include <assert.h>
#include <stdlib.h>



/**
 * Casillas en la que almacenaremos los datos de la tabla hash.
 */
typedef struct {
  void *dato;
  int eliminado;
} CasillaHash;

/**
 * Estructura principal que representa la tabla hash.
 */
struct _TablaHash {
  CasillaHash *elems;
  unsigned numElems;
  unsigned capacidad;
  FuncionCopiadora copia;
  FuncionComparadora comp;
  FuncionDestructora destr;
  FuncionHash hash;
};


/**
 * Crea una nueva tabla hash vacia, con la capacidad dada.
 */
TablaHash tablahash_crear(unsigned capacidad, FuncionCopiadora copia,
                          FuncionComparadora comp, FuncionDestructora destr,
                          FuncionHash hash) {

  // Pedimos memoria para la estructura principal y las casillas.
  TablaHash tabla = malloc(sizeof(struct _TablaHash));
  assert(tabla != NULL);
  tabla->elems = malloc(sizeof(CasillaHash) * capacidad);
  assert(tabla->elems != NULL);
  tabla->numElems = 0;
  tabla->capacidad = capacidad;
  tabla->copia = copia;
  tabla->comp = comp;
  tabla->destr = destr;
  tabla->hash = hash;

  // Inicializamos las casillas con datos nulos.
  for (unsigned idx = 0; idx < capacidad; ++idx) {
    tabla->elems[idx].dato = NULL;
    tabla->elems[idx].eliminado = 0;
  }

  return tabla;
}

/**
 * Retorna el numero de elementos de la tabla.
 */
int tablahash_nelems(TablaHash tabla) { return tabla->numElems; }

/**
 * Retorna la capacidad de la tabla.
 */
int tablahash_capacidad(TablaHash tabla) { return tabla->capacidad; }

/**
 * Destruye la tabla.
 */
void tablahash_destruir(TablaHash tabla) {

  // Destruir cada uno de los datos.
  for (unsigned idx = 0; idx < tabla->capacidad; ++idx)
    if (tabla->elems[idx].dato != NULL)
      tabla->destr(tabla->elems[idx].dato);

  // Liberar el arreglo de casillas y la tabla.
  free(tabla->elems);
  free(tabla);
  return;
}

static void* no_copia(void* dato){
  return dato;
}

static void tablahash_redimensionar(TablaHash tabla){
  CasillaHash* viejoArr = tabla->elems;
  unsigned viejaCap = tabla->capacidad;
  FuncionCopiadora copia = tabla->copia;
  tabla->capacidad *=2; //duplico la capacidad
  tabla->elems = malloc(sizeof(CasillaHash)*tabla->capacidad);
  assert(tabla->elems != NULL);

  //inicializo los datos:
  for (unsigned idx = 0; idx < tabla->capacidad; ++idx) {
    tabla->elems[idx].dato = NULL;
    tabla->elems[idx].eliminado = 0;
  }

  //cambio la funcion copia:
  tabla->copia = no_copia;

  //reinicio el contador de elementos:
  tabla->numElems = 0;

  //vuelvo a cargar los elentos:
  for (unsigned idx = 0; idx < viejaCap; ++idx){
    if (viejoArr[idx].dato != NULL)
      tablahash_insertar(tabla, viejoArr[idx].dato);
  }

  //restauro la copia:
  tabla->copia = copia;

  //libero el viejo array:
  free(viejoArr);
} 

/**
 * Inserta un dato en la tabla, o lo reemplaza si ya se encontraba.
 * IMPORTANTE: La implementacion maneja colisiones con linear probing.
 */
void tablahash_insertar(TablaHash tabla, void *dato) {
  // Calculamos la posicion del dato dado, de acuerdo a la funcion hash.
  unsigned idx = tabla->hash(dato) % tabla->capacidad;
  int insertado = 0;
  int lugar = -1;

  for(unsigned i=0; i< tabla->capacidad && !insertado; i++){

    if(tabla->elems[idx].dato == NULL){ //eliminado o casilla virgen
      if (lugar == -1) //la primera "vacia" que encontre
        lugar = idx;
      if (tabla->elems[idx].eliminado == 0){
        insertado = 1; //termine de recorrer el cluster
      }
    }

    // Sobrescribir el dato si el mismo ya se encontraba en la tabla.
    else if  (tabla->comp(tabla->elems[idx].dato, dato) == 0){
      tabla->destr(tabla->elems[idx].dato);
      tabla->elems[idx].dato = tabla->copia(dato);
      insertado = 1;
      lugar = -1;
    }
    idx = (idx +1) % tabla->capacidad; //itero
  } 

  if (lugar >= 0){
    tabla->numElems++;
    tabla->elems[lugar].dato = tabla->copia(dato);
    tabla->elems[lugar].eliminado = 0;
  }

  if (((float)tabla->numElems / (float)tabla->capacidad) > FACTOR_CARGA) 
    tablahash_redimensionar(tabla);
  return;
}


/**
 * Retorna el dato de la tabla que coincida con el dato dado, o NULL si el dato
 * buscado no se encuentra en la tabla.
 */
void *tablahash_buscar(TablaHash tabla, void *dato) {

  // Calculamos la posicion del dato dado, de acuerdo a la funcion hash.
  unsigned idx = tabla->hash(dato) % tabla->capacidad;

  for(unsigned i=0; i < tabla->capacidad; i++){

    // Retornar NULL si la casilla estaba vacia.
    if (tabla->elems[idx].dato == NULL && tabla->elems[idx].eliminado == 0)
        return NULL;

    if (tabla->elems[idx].dato != NULL &&  tabla->comp(tabla->elems[idx].dato, dato) == 0)
      return tabla->elems[idx].dato;

    //rompo el for pero no me importa.

    idx = (idx+1) % tabla->capacidad;
  }
  return NULL;
}

/**
 * Elimina el dato de la tabla que coincida con el dato dado.
 */
void tablahash_eliminar(TablaHash tabla, void *dato) {

  // Calculamos la posicion del dato dado, de acuerdo a la funcion hash.
  unsigned idx = tabla->hash(dato) % tabla->capacidad;

  int eliminado = 0;

  for(unsigned i=0; i< tabla->capacidad && !eliminado; i++){

    if (tabla->elems[idx].dato == NULL && tabla->elems[idx].eliminado == 0)
      eliminado = 1;
    else if (tabla->elems[idx].dato != NULL && tabla->comp(tabla->elems[idx].dato, dato) == 0){
      eliminado = 1;
      tabla->numElems--;
      tabla->destr(tabla->elems[idx].dato);
      tabla->elems[idx].dato = NULL; //fundamental
      CasillaHash siguiente = tabla->elems[(idx +1) % tabla->capacidad];
      if (siguiente.dato == NULL && siguiente.eliminado == 0)
        tabla->elems[idx].eliminado = 0;
      else
        tabla->elems[idx].eliminado = 1;
    }
    idx = (idx +1) % tabla->capacidad;
  }
}


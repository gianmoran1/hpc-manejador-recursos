#include "tablahash.h"
#include <assert.h>
#include <stdlib.h>

/**
 * Casillas en la que almacenaremos los datos de la tabla hash.
 */
typedef struct _CasillaHash {
  void* dato;
  int eliminado;
} CasillaHash;

/**
 * Estructura principal que representa la tabla hash.
 */
struct _TablaHash {
  CasillaHash* elems;
  unsigned numElems;
  unsigned capacidad;
  FuncionCopiadora copia;
  FuncionComparadora comp;
  FuncionDestructora destr;
  FuncionHash hash;
};

/**
 * tablahash_crear: Dada una capacidad, una funcion de copia, otra de
 * comparacion, una de destruccion y una funcion hash, la funcion crea una
 * tabla hash y la devuelve.
 */
TablaHash tablahash_crear(unsigned capacidad, FuncionCopiadora copia,
                          FuncionComparadora comp, FuncionDestructora destr,
                          FuncionHash hash) {

  TablaHash tabla = malloc(sizeof(struct _TablaHash));
  assert(tabla);
  tabla->elems = malloc(sizeof(CasillaHash) * capacidad);
  assert(tabla->elems);
  tabla->numElems = 0;
  tabla->capacidad = capacidad;
  tabla->copia = copia;
  tabla->comp = comp;
  tabla->destr = destr;
  tabla->hash = hash;

  // Inicializamos las casillas con datos nulos y marcamos que ninguno esta
  // eliminado.
  for (unsigned i = 0; i < capacidad; i++) {
    tabla->elems[i].dato = NULL;
    tabla->elems[i].eliminado = 0;
  }

  return tabla;
}

/**
 * tablahash_nelems: Retorna el numero de elementos de la tabla dada. Si la
 * tabla es NULL retorna -1.
 */
int tablahash_nelems(TablaHash tabla) {
  int cantElem = -1 ;
  if (tabla) {
    cantElem = tabla->numElems; 
  }
  return cantElem;
}

/**
 * tablahash_capacidad: Retorna la capacidad de la tabla dada. Si la tabla es 
 * NULL retorna -1.
 */
int tablahash_capacidad(TablaHash tabla) {
  int capacidad = -1;
  if (tabla) {
    capacidad = tabla->capacidad;
  }
  return capacidad;
}

/**
 * tablahash_destruir: Dada una tabla hash, la funcion libera la memoria pedida
 * por esta y devuelve NULL.
 */
TablaHash tablahash_destruir(TablaHash tabla) {
  if (tabla) {
    for (unsigned i = 0; i < tabla->capacidad; i++){
      if (tabla->elems[i].dato) {
        tabla->destr(tabla->elems[i].dato);
      }
    }
    free(tabla->elems);
    free(tabla);
    tabla = NULL;
  }
  return tabla;
}

/**
 * no_copia: Dado un puntero void, la funcion lo retorna.
 */
static void* no_copia(void* dato){
  return dato;
}

/**
 * tablahash_redimensionar: La funcion redimensiona la tabla hash dada.
 */
static void tablahash_redimensionar(TablaHash tabla) {
  unsigned nuevaCapacidad = tabla->capacidad * 2;
  CasillaHash* nuevoArrElementos = malloc(sizeof(CasillaHash) * nuevaCapacidad);
  assert(nuevoArrElementos);

  for (unsigned i = 0; i < nuevaCapacidad; i++) {
    nuevoArrElementos[i].dato = NULL;
    nuevoArrElementos[i].eliminado = 0;
  }

  // Guardo los elementos originales de la tabla.
  unsigned capacidadOriginal = tabla->capacidad;
  FuncionCopiadora funcCopiaOriginal = tabla->copia;
  CasillaHash* elemOrignales = tabla->elems;

  tabla->capacidad = nuevaCapacidad;
  tabla->elems = nuevoArrElementos;
  tabla->numElems = 0;
  tabla->copia = no_copia;

  for (unsigned i = 0; i < capacidadOriginal; i++) {
    if (elemOrignales[i].dato && !elemOrignales[i].eliminado) {
      tablahash_insertar(tabla, elemOrignales[i].dato);
    }
  }
  free(elemOrignales);
  tabla->copia = funcCopiaOriginal;
}

/**
 * tablahash_insertar_aux: Funcion interna a tablahash_insertar, el parametro
 * idxAColocar es un indice el cual fue eliminado y puedo colocar ahi el dato.
 */
static TablaHash tablahash_insertar_aux(TablaHash tabla, void* dato, 
                                        int idxAVisitar, int idxAColocar) {

  // Inserto el dato si la casilla estaba libre y ademas no encontre 
  // una casilla eliminada para colocarlo.
  if (!tabla->elems[idxAVisitar].dato && idxAColocar == -1) {
    tabla->numElems++;
    tabla->elems[idxAVisitar].dato = tabla->copia(dato);
  } else if (!tabla->elems[idxAVisitar].dato && idxAColocar != -1) {
    // Si llegue a un NULL y el indice a colocarlo es distinto de -1
    // entonces el dato no se halla en la tabla y tengo una posicion eliminada
    // para colocarlo.
    tabla->numElems++;
    tabla->destr(tabla->elems[idxAColocar].dato);
    tabla->elems[idxAColocar].dato = tabla->copia(dato);
    tabla->elems[idxAColocar].eliminado = 0;
  } else if (tabla->comp(tabla->elems[idxAVisitar].dato, dato) == 0 && 
              tabla->elems[idxAVisitar].eliminado == 0) {
    // Sobrescribir el dato si el mismo ya se encontraba en la tabla.
    tabla->destr(tabla->elems[idxAVisitar].dato);
    tabla->elems[idxAVisitar].dato = tabla->copia(dato);
  } else { // Caso colision lo coloco en el siguiente libre
    if (tabla->elems[idxAVisitar].eliminado == 1 && idxAColocar == -1) {
      // Si encuentro una casilla eliminada voy a poner el dato aqui, pero
      // debo seguir buscando para ver si no hay coincidencia del dato.
      idxAColocar = idxAVisitar;
    } 
    tabla = tablahash_insertar_aux(tabla, dato, 
                            (idxAVisitar + 1) % tabla->capacidad, idxAColocar);
  }
  return tabla;
}

/**
 * tablahash_insertar: Dada una tabla hash y un dato, la funcion inserta el dato
 * dado en la tabla. De ser necesario, la funcion redimensiona la tabla
 * original.
 */
void tablahash_insertar(TablaHash tabla, void* dato) {
  if (tabla) {
    float factorCarga = (float) tabla->numElems / (float) tabla->capacidad; 
    if (factorCarga > 0.7) {
      tablahash_redimensionar(tabla);
    }
    unsigned idx = tabla->hash(dato) % tabla->capacidad;
    tabla = tablahash_insertar_aux(tabla, dato, idx, -1);
  }
}

/**
 * tablahash_buscar_aux: Funcion interna a tablahash_buscar.
 */
static void* tablahash_buscar_aux(TablaHash tabla, void* dato, int idx) {
  void* datoBuscado = NULL;
  if (tabla->elems[idx].dato) {
    // Retornar el dato de la casilla si hay concidencia y no elimine el dato.
    if (tabla->comp(tabla->elems[idx].dato, dato) == 0 &&
                    tabla->elems[idx].eliminado == 0) {
      datoBuscado = tabla->elems[idx].dato;
     } else { // Lo sigo buscando en el indice siguiente.
      datoBuscado =  tablahash_buscar_aux(tabla, dato, 
                                          (idx + 1) % tabla->capacidad);
    }
  }
  return datoBuscado;
}

/**
 * tablahash_buscar: Dada una tabla hash y un dato a buscar, la funcion retorna 
 * el dato de la tabla que coincida con el dato dado, o NULL si el dato
 * buscado no se encuentra en la tabla.
 */
void* tablahash_buscar(TablaHash tabla, void* dato) {
  void* datoBuscado = NULL;
  if (tabla) {
    unsigned idx = tabla->hash(dato) % tabla->capacidad;
    datoBuscado = tablahash_buscar_aux(tabla, dato, idx);
  }
  return datoBuscado;
}

/**
 * tablahash_eliminar_aux: Funcion interna a tablahash_eliminar.
 */
static TablaHash tablahash_eliminar_aux(TablaHash tabla, void* dato, int idx) {
  if (tabla->elems[idx].dato){
    // Si hay coincidencia lo marco como eliminado.
    if (tabla->comp(tabla->elems[idx].dato, dato) == 0 && 
                    tabla->elems[idx].eliminado == 0) {
      tabla->numElems--;
      tabla->elems[idx].eliminado = 1;
    } else {
      tabla = tablahash_eliminar_aux(tabla, dato, (idx + 1) % tabla->capacidad);
    }
  }
  return tabla;
}

/**
 * tablahash_eliminar: Dada una tabla hash y un dato, la funcion elimina el dato
 * de la tabla en caso de que este se encuentre.
 */
void tablahash_eliminar(TablaHash tabla, void* dato) {
  if (tabla) {
    unsigned idx = tabla->hash(dato) % tabla->capacidad;
    tabla = tablahash_eliminar_aux(tabla, dato, idx);
  }
}

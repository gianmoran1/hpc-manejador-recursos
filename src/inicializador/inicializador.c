#include "../../include/inicializador.h"
#include "../estructuras/especificas/funciones/funciones.h"
#include "../estructuras/especificas/funciones/funciones-primitivas/funciones-primitivas.h"
#include "../estructuras/genericas/lista/lista.h"
#include "../utils/utils-str.h"

#define CAPACIDAD_INICIAL_TABLAS 1024

/**
 * inicializador_funciones: La funcion crea la tabla hash y la lista donde se 
 * guardaran las funciones, ademas agrega las primitivas.
 */
AlmacenamientoFunciones inicializador_funciones() {
  TablaHash tablaFunciones = tablahash_crear(CAPACIDAD_INICIAL_TABLAS, 
                                      (FuncionCopiadora) funcion_copiar, 
                                      (FuncionComparadora) funcion_compara, 
                                      (FuncionDestructora) funcion_destruir, 
                                      (FuncionHash) funcion_hash);

  Lista listaFunciones = lista_crear((FuncionCopiadora) funcion_copiar, 
                                      (FuncionDestructora) funcion_destruir);

  Funcion primitiva0i = funcion_crear();
  primitiva0i->idFunc = string_copiar("0i");
  primitiva0i->esPrimitiva = 1;
  primitiva0i->funcPrimitiva = primitiva_0i;
  tablahash_insertar(tablaFunciones, primitiva0i);
  lista_agregar_final(listaFunciones, primitiva0i);
  primitiva0i = funcion_destruir(primitiva0i);

  Funcion primitiva0d = funcion_crear();
  primitiva0d->idFunc = string_copiar("0d");
  primitiva0d->esPrimitiva = 1;
  primitiva0d->funcPrimitiva = primitiva_0d;
  tablahash_insertar(tablaFunciones, primitiva0d);
  lista_agregar_final(listaFunciones, primitiva0d);
  primitiva0d = funcion_destruir(primitiva0d);

  Funcion primitivaSi = funcion_crear();
  primitivaSi->idFunc = string_copiar("Si");
  primitivaSi->esPrimitiva = 1;
  primitivaSi->funcPrimitiva = primitiva_Si;
  tablahash_insertar(tablaFunciones, primitivaSi);
  lista_agregar_final(listaFunciones, primitivaSi);
  primitivaSi = funcion_destruir(primitivaSi);

  Funcion primitivaSd = funcion_crear();
  primitivaSd->idFunc = string_copiar("Sd");
  primitivaSd->esPrimitiva = 1;
  primitivaSd->funcPrimitiva = primitiva_Sd;
  tablahash_insertar(tablaFunciones, primitivaSd);
  lista_agregar_final(listaFunciones, primitivaSd);
  primitivaSd = funcion_destruir(primitivaSd);

  Funcion primitivaDi = funcion_crear();
  primitivaDi->idFunc = string_copiar("Di");
  primitivaDi->esPrimitiva = 1;
  primitivaDi->funcPrimitiva = primitiva_Di;
  tablahash_insertar(tablaFunciones, primitivaDi);
  lista_agregar_final(listaFunciones, primitivaDi);
  primitivaDi= funcion_destruir(primitivaDi);

  Funcion primitivaDd = funcion_crear();
  primitivaDd->idFunc = string_copiar("Dd");
  primitivaDd->esPrimitiva = 1;
  primitivaDd->funcPrimitiva = primitiva_Dd;
  tablahash_insertar(tablaFunciones, primitivaDd);
  lista_agregar_final(listaFunciones, primitivaDd);
  primitivaDd= funcion_destruir(primitivaDd);

  AlmacenamientoFunciones almFunc;
  almFunc.funcLista = listaFunciones;
  almFunc.funcTab = tablaFunciones;
  return almFunc;
}

/**
 * inicializador_listas: La funcion crea la tabla hash donde se guardaran las
 * listas.
 */
TablaHash inicializador_listas() {
  TablaHash tablaListas = tablahash_crear(CAPACIDAD_INICIAL_TABLAS, 
                                      (FuncionCopiadora) lista_copiar, 
                                      (FuncionComparadora) lista_comparar, 
                                      (FuncionDestructora) lista_destruir, 
                                      (FuncionHash) lista_hash);
  return tablaListas;
}

#include <stdlib.h>
#include <assert.h>

#include "../../include/interprete.h"
#include "../utils/utils-str.h"
#include "../utils/utils-enteros.h"
#include "../estructuras/genericas/cola/cola.h"

#define LIMITE_ITERACIONES_REPETICION 506
#define PROFUNDIDAD_MAXIMA_SEARCH 6

// Utilidades-------------------------------------------------------------------

/**
 * buscar_lista_enteros_por_id: Dado un string y una tabla hash con listas de 
 * enteros, la funcion busca si hay una lista con ese id. Si hay la retorna sin 
 * crear una copia del mismo. Si no existe tal lista retorna NULL.
 */
static void* buscar_lista_enteros_por_id(char* idLista, TablaHash listas) {
  Lista listaSimulada = lista_crear((FuncionCopiadora) entero_copiar,
                                    (FuncionDestructora) entero_destruir);
  lista_asignar_id(listaSimulada, idLista);
  Lista listaReal = tablahash_buscar(listas, listaSimulada);
  lista_destruir(listaSimulada);
  return listaReal;
}

/**
 * buscar_funcion_por_id: Dado un string y una tabla hash con funciones 
 * la funcion busca si hay una funcion con ese id. Si hay la retorna sin 
 * crear una copia del mismo. Si no existe tal lista retorna NULL.
 */
static void* buscar_funcion_por_id(char* idFunc, TablaHash funciones) {
  Funcion funcSimulada = funcion_crear();
  funcSimulada->idFunc = string_copiar(idFunc);
  Funcion funcReal = tablahash_buscar(funciones, funcSimulada);
  funcion_destruir(funcSimulada);
  return funcReal;
}

// Interprete de sentencias Deff------------------------------------------------

/**
 * interprete_deff_aux: Dada una estructura Funcion (func) sin inicializar, 
 * salvo posiblemente su id, una lista de estructuras SintaxFuncion y una tabla
 * hash de estructuras Funcion, la funcion carga a func los datos 
 * correspondientes a la funcion indicada por el usuario. Si es una sentencia 
 * valida retorna func. Si no es valida retorna NULL.
 */
static Funcion interprete_deff_aux(Funcion func, Lista listaFunc, 
                                    TablaHash funciones) {
  func->esPrimitiva = 0;
  func->Composicion.cantFunciones = lista_cant_elementos(listaFunc);
  func->Composicion.arrFunc = malloc(sizeof(NodoFuncion) * 
                                              func->Composicion.cantFunciones);
  assert(func->Composicion.arrFunc);

  SintaxFuncion nodoActual = lista_iterador_inicializar(listaFunc);

  int funcValida = 1;
  for (int i = 0; i < func->Composicion.cantFunciones; i++) {
    // Si la funcion no es valida lleno el array de NULL para facilitar la 
    // liberacion de memoria.
    func->Composicion.arrFunc[i] = NULL;

    if (funcValida) {
      if (nodoActual->esRepeticion == 0) {
        Funcion funcReal = buscar_funcion_por_id(nodoActual->idFunc, funciones);

        if (funcReal) {
          func->Composicion.arrFunc[i] = nodo_funcion_crear();
          func->Composicion.arrFunc[i]->esRepeticion = 0;
          func->Composicion.arrFunc[i]->func = funcion_copiar(funcReal);
        } else {
          funcValida = 0;
        }
      } else if (nodoActual->esRepeticion == 1) {
        func->Composicion.arrFunc[i] = nodo_funcion_crear();
        func->Composicion.arrFunc[i]->esRepeticion = 1;
        // Analizo que la funcion sea valida dentro de la repeticion llamando
        // recursivamente a la funcion.
        func->Composicion.arrFunc[i]->func = funcion_crear();
        func->Composicion.arrFunc[i]->func = 
        interprete_deff_aux(func->Composicion.arrFunc[i]->func, 
                            nodoActual->funcRepeticion, funciones);

        if (!func->Composicion.arrFunc[i]->func) {
          funcValida = 0;
        }
      }
    }
    nodoActual = lista_iterador_siguiente(listaFunc);
  }

  lista_iterador_destruir(listaFunc);

  if (!funcValida) {
    func = funcion_destruir(func);
  }
  return func;
} 

/**
 * interprete_deff: Dada una estructura SintaxDeff y una tabla hash con 
 * funciones, la funcion crea una estructura de funcion y la devuelve 
 * en caso de que la setencia deff sea valida. Si no es una sentencia valida
 * devuelve NULL.
 */
static Funcion interprete_deff(SintaxDeff deff, TablaHash funciones) {
  Funcion func = funcion_crear();
  func->idFunc = string_copiar(deff->nombreFuncion);
  // Tengo que verificar que no haya una funcion con ese id.
  if (!tablahash_buscar(funciones, func)) {
    func = interprete_deff_aux(func, deff->listaFunc, funciones);
  } else {
    func = funcion_destruir(func);
  }
  return func;
}

// Interprete de sentencias Apply-----------------------------------------------

// Doy el prototipo para usarla en la funcion repeticion_algoritmo.
static Lista apply_algoritmo(Funcion func, Lista lista);

/**
 * repeticion_algoritmo: Dada una Funcion, una lista y un entero, la funcion 
 * aplica el operador de repeticion de la funcion dada a la lista. Si la lista
 * tiene menos de dos elementos retorna NULL, ademas como tiene potencial
 * de iteracion "infinita", si se superan un maximo de llamadas recursivas
 * la funcion tambien retorna NULL. Para esto ultimo sirve el entero pedido por
 * parametro. La funcion modifica la lista dada.
 */
static Lista repeticion_algoritmo(Funcion func, Lista lista, int i) {
  Lista listaResultado = NULL;
  if (i < LIMITE_ITERACIONES_REPETICION && lista_cant_elementos(lista) >= 2) {
    if (*((int*) lista_primero(lista)) != *((int*) lista_ultimo(lista))) {
      listaResultado = repeticion_algoritmo(func, apply_algoritmo(func, lista),
                                            i + 1);
    } else {
      listaResultado = lista;
    }
  }
  return listaResultado;
} 

/**
 * apply_algoritmo: Dada una Funcion func y una lista, la funcion aplica
 * func a la lista dada. La funcion modifica la lista original. Ademas si hay
 * un error, la funcion devuelve NULL.
 */
static Lista apply_algoritmo(Funcion func, Lista lista) {
  Lista listaResultado = NULL;
  if (func->esPrimitiva == 1) {
    // Las primitivas modifican la lista.
    listaResultado = func->funcPrimitiva(lista);
  } else if (func->esPrimitiva == 0) {
    int composicionValida = 1;
    for (int i = 0; i < func->Composicion.cantFunciones && composicionValida; 
                                                                          i++) {
      if (func->Composicion.arrFunc[i]->esRepeticion == 0) {
        listaResultado = 
            apply_algoritmo(func->Composicion.arrFunc[i]->func, lista);
        if (!listaResultado) {
          composicionValida = 0;
        }
      } else if (func->Composicion.arrFunc[i]->esRepeticion == 1) {
        listaResultado = 
              repeticion_algoritmo(func->Composicion.arrFunc[i]->func,
                                  lista, 0);
        if (!listaResultado) {
          composicionValida = 0;
        }
      }
    }
  }
  return listaResultado;
}

/**
 * interprete_apply: Dada una estructura SintaxApply, una tabla hash con listas
 * y otra con funciones, la funcion devuelve una lista que es el resultado
 * de ejecutar la sentencia apply. Si no es una sentencia valida devuelve NULL.
 */
static Lista interprete_apply(SintaxApply apply, TablaHash listas, 
                              TablaHash funciones) {
  Lista listaResultado = NULL;
  Funcion funcAplicar = buscar_funcion_por_id(apply->idFuncAplicar, funciones);

  if (funcAplicar) {
    Lista listaAplicar = NULL;
    if (apply->esListaPorExtension == 1) {
      listaAplicar = lista_copiar(apply->lista);
    } else {
      listaAplicar = lista_copiar(buscar_lista_enteros_por_id(apply->idLista, 
                                                              listas));
    }

    if (listaAplicar) {
      listaResultado = apply_algoritmo(funcAplicar, listaAplicar);
      if (!listaResultado) {
        lista_destruir(listaAplicar);
      }
    }
  }

  return listaResultado;
}

// Interprete de sentencias Search----------------------------------------------

/**
 * crear_funcion_compuesta: Dadas dos estructuras Funcion, la funcion crea una
 * composicion con esas dos funciones.
 */
static Funcion crear_funcion_compuesta(Funcion func1, Funcion func2) {
  Funcion funcCompuesta = funcion_crear();
  funcCompuesta->esPrimitiva = 0;
  funcCompuesta->Composicion.arrFunc = malloc(sizeof(NodoFuncion) * 2);
  assert(funcCompuesta->Composicion.arrFunc);
  funcCompuesta->Composicion.cantFunciones = 2;
  funcCompuesta->Composicion.arrFunc[0] = nodo_funcion_crear();
  funcCompuesta->Composicion.arrFunc[0]->esRepeticion = 0;
  funcCompuesta->Composicion.arrFunc[0]->func = funcion_copiar(func1);
  funcCompuesta->Composicion.arrFunc[1] = nodo_funcion_crear();
  funcCompuesta->Composicion.arrFunc[1]->esRepeticion = 0;
  funcCompuesta->Composicion.arrFunc[1]->func = funcion_copiar(func2);
  return funcCompuesta;
}

/**
 * funcion_no_copiar: Dada una estructura Funcion, la funcion la retorna.
 */
static Funcion funcion_no_copiar(Funcion func) {
  return func;
}

/**
 * search_algoritmo: Dadas las listas de listas de origen y destino de la
 * sentencia search, y la lista de funciones, la funcion realiza la busqueda de
 * la funcion que satisfaga la sentencia search.
 */
static Funcion search_algoritmo(Lista listasOrigen, Lista listasDestino, 
                                Lista funciones) {
  // La lista origen se modificara por eso trabajamos con la copia.
  Lista listaOrigenAnalizar = 
                        lista_copiar(lista_iterador_inicializar(listasOrigen)); 
  Lista listaDestinoAnalizar = lista_iterador_inicializar(listasDestino);

  Cola colaBfs = cola_crear((FuncionCopiadora) funcion_no_copiar,
                            (FuncionDestructora) funcion_destruir);
  for (Funcion nodoActual = lista_iterador_inicializar(funciones); nodoActual; 
        nodoActual = lista_iterador_siguiente(funciones)) {
    // Cargo a la cola todas las funciones definidas.
    Funcion funcCopiada = funcion_copiar(nodoActual);
    cola_encolar(colaBfs, funcCopiada); 
  }
  lista_iterador_destruir(funciones);

  int restantesNivel = lista_cant_elementos(funciones);
  int profundidad = 0;
  int sigNivel = 0;
  Funcion funcResultado = NULL;
  int primerPar = 1;

  while (!funcResultado && profundidad < PROFUNDIDAD_MAXIMA_SEARCH) {
    Funcion funcAplicar = cola_primero(colaBfs);
    Lista listaActual = apply_algoritmo(funcAplicar, listaOrigenAnalizar);

    if (listaActual && listas_iguales(listaActual, listaDestinoAnalizar,
                                  (FuncionComparadora) entero_comparar) == 1) {
      lista_destruir(listaOrigenAnalizar);
      listaOrigenAnalizar = 
                          lista_copiar(lista_iterador_siguiente(listasOrigen)); 
      listaDestinoAnalizar = lista_iterador_siguiente(listasDestino);

      // Si hay mas listas sigo buscando.
      if (listaOrigenAnalizar && listaDestinoAnalizar) { 
        primerPar = 0;
      } else {
        funcResultado = funcion_copiar(funcAplicar);
      }
    } else {
      for (Funcion nodoActual = lista_iterador_inicializar(funciones);
            nodoActual; 
            nodoActual = lista_iterador_siguiente(funciones)) {
        Funcion funcEncolar = crear_funcion_compuesta(funcAplicar, 
                                                      nodoActual);
        cola_encolar(colaBfs, funcEncolar);
        sigNivel++;
      }
      lista_iterador_destruir(funciones);
      cola_desencolar(colaBfs);
      funcion_destruir(funcAplicar);

      // Elimino la lista con la que trabaje ya que esta modificada.
      listaOrigenAnalizar = lista_destruir(listaOrigenAnalizar);
      if (primerPar) {
        listaOrigenAnalizar = 
                              lista_copiar(lista_iterador_actual(listasOrigen)); 
      } else {
        // Vuelvo a la primera lista si no encuentro una funcion valida y ya
        // estaba en otra lista.
        listaOrigenAnalizar = 
                        lista_copiar(lista_iterador_inicializar(listasOrigen)); 
        listaDestinoAnalizar = lista_iterador_inicializar(listasDestino);
      }
    }
    restantesNivel--;
    if (restantesNivel == 0) {
      profundidad++;
      restantesNivel = sigNivel;
      sigNivel = 0;
    }
  }

  if (!funcResultado) {
    listaOrigenAnalizar = lista_destruir(listaOrigenAnalizar);
  }

  lista_iterador_destruir(listasOrigen);
  lista_iterador_destruir(listasDestino);
  cola_destruir(colaBfs);

  return funcResultado;
}

/**
 * interprete_search: Dada una estructura SintaxSearch, la tabla hash de listas
 * y la lista de funciones, la funcion realiza la busqueda de la funcion que
 * satisfaga la sentencia search, en caso de que esta sea valida. En caso de 
 * encontrarla devuelve la funcion, si no la encuentra, o la sentencia
 * no es valida devuelve NULL.
 */
static Funcion interprete_search(SintaxSearch search, TablaHash listas, 
                                  Lista funciones) {
  Lista listasOrigen = NULL;
  Lista listasDestino = NULL;

  if (search->sonListasPorExt == 0) {
    // Tengo una lista con ids, debo ver si los ids dados son validos.
    listasOrigen = lista_crear((FuncionCopiadora) lista_copiar,
                                (FuncionDestructora) lista_destruir);
    listasDestino = lista_crear((FuncionCopiadora) lista_copiar,
                                (FuncionDestructora) lista_destruir);
    int lecturaTerminada = 0;
    int paresValidos = 1;

    // Del parseo se que hay al menos un par de listas.
    char* idListaOrigen = 
                  lista_iterador_inicializar(search->ListasPorId.listasOrigen);
    char* idListaDestino = 
                  lista_iterador_inicializar(search->ListasPorId.listasDestino);

    while (!lecturaTerminada && paresValidos) {
      if (!idListaOrigen) {
        lecturaTerminada = 1;
      } else {
        Lista listaOrigenActual = buscar_lista_enteros_por_id(idListaOrigen, 
                                                            listas);
        idListaOrigen = 
                    lista_iterador_siguiente(search->ListasPorId.listasOrigen);

        if (listaOrigenActual) { // Si el id corresponde a una lista.
          lista_agregar_final(listasOrigen, listaOrigenActual);
        } else {
          paresValidos = 0;
        }
      }

      if (!idListaDestino) {
        lecturaTerminada = 1;
      } else {
        Lista listaDestinoActual = buscar_lista_enteros_por_id(idListaDestino, 
                                                            listas);
        idListaDestino = 
                    lista_iterador_siguiente(search->ListasPorId.listasDestino);

        if (listaDestinoActual) { // Si el id corresponde a una lista.
          lista_agregar_final(listasDestino, listaDestinoActual);
        } else {
          paresValidos = 0;
        }
      }
    }

    if (!lecturaTerminada || !paresValidos) {
      listasOrigen = lista_destruir(listasOrigen);
      listasDestino = lista_destruir(listasDestino);
    }

    lista_iterador_destruir(search->ListasPorId.listasOrigen);
    lista_iterador_destruir(search->ListasPorId.listasDestino);

  } else if (search->sonListasPorExt == 1) {
    listasOrigen = search->ListasPorExt.listasOrigen;
    listasDestino = search->ListasPorExt.listasDestino;
  }

  Funcion funcResultado = NULL;
  if (listasOrigen && listasDestino) {
    funcResultado = search_algoritmo(listasOrigen, listasDestino, 
                                    funciones);
    if (search->sonListasPorExt == 0) {
      lista_destruir(listasOrigen);
      lista_destruir(listasDestino);
    }
  }

  return funcResultado;
}

// Interprete-------------------------------------------------------------------

/**
 * interprete: Dado un AST, una tabla hash con listas, otra con funciones y
 * una lista con funciones, la funcion retorna una estructura ValorSentencia
 * si la sentencia es una sentencia correcta. Devuelve NULL en caso contrario.
 * Ademas la funcion libera le memoria pedida por el AST.
 */
ValorSentencia interprete(AST arbol, TablaHash listas, TablaHash funcTab, 
                          Lista funcLista) {

  ValorSentencia sentVal = NULL;
  if (arbol) {
    sentVal = valor_sentencia_crear();
    if (arbol->tipoSentencia == SENTENCIA_DEFF) {
      sentVal->tipoSentencia = SENTENCIA_DEFF;
      sentVal->funcDeff = interprete_deff(arbol->deff, funcTab);
      if (!sentVal->funcDeff) {
        sentVal = valor_sentencia_destruir(sentVal);
      }
    } else if (arbol->tipoSentencia == SENTENCIA_DEFL) {
      // Si una sentencia defl pasa el parseo ya es valida.
      sentVal->tipoSentencia = SENTENCIA_DEFL;
      sentVal->listaDefl = lista_copiar(arbol->defl);
    } else if (arbol->tipoSentencia == SENTENCIA_APPLY) {
      sentVal->tipoSentencia = SENTENCIA_APPLY;
      sentVal->listaApply = interprete_apply(arbol->apply, listas, funcTab);
      if (!sentVal->listaApply) {
        sentVal = valor_sentencia_destruir(sentVal);
      }
    } else if (arbol->tipoSentencia == SENTENCIA_SEARCH) {
      sentVal->tipoSentencia = SENTENCIA_SEARCH;
      sentVal->funcSearch = interprete_search(arbol->search, listas, funcLista);
      if (!sentVal->funcSearch) {
        sentVal = valor_sentencia_destruir(sentVal);
      }
    } else {
      sentVal = valor_sentencia_destruir(sentVal);
    }
    ast_destruir(arbol);
  }
  return sentVal;
}

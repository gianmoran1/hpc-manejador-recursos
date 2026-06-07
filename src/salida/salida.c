#include <stdio.h>

#include "../../include/salida.h"
#include "../utils/utils-enteros.h"
#include "../utils/utils-funciones.h"

/**
 * manejador_de_salida: Dada una estructura ValorSentencia, la tabla hash de 
 * listas, la de funciones y la lista de funciones, la funcion carga los datos 
 * a las tablas o imprime la salida de la sentencia. Si la sentencia no es 
 * valida devuelve 0. Si la sentencia es valida devuelve 1.
 */
int manejador_de_salida(ValorSentencia sentVal, TablaHash listas, 
                                  TablaHash funcTab, Lista funcLista) { 
  int codigoSalida = 0;
  if (sentVal) {
    if (sentVal->tipoSentencia == SENTENCIA_DEFF) {
      tablahash_insertar(funcTab, sentVal->funcDeff);
      lista_agregar_final(funcLista, sentVal->funcDeff);
    } else if (sentVal->tipoSentencia == SENTENCIA_DEFL) {
      tablahash_insertar(listas, sentVal->listaDefl);
    } else if (sentVal->tipoSentencia == SENTENCIA_APPLY) {
      printf("[ ");
      lista_recorrer(sentVal->listaApply, (FuncionVisitante) entero_imprimir);
      printf("]\n");
    } else if (sentVal->tipoSentencia == SENTENCIA_SEARCH) {
      funcion_imprimir(sentVal->funcSearch);
      printf("\n");
    }
    valor_sentencia_destruir(sentVal);
    codigoSalida = 1;
  }
  return codigoSalida;
}

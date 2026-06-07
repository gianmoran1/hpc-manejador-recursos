#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../include/inicializador.h"
#include "../include/entrada.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/interprete.h"
#include "../include/salida.h"

int main() {
  AlmacenamientoFunciones almFunc = inicializador_funciones();
  TablaHash tablaFunciones = almFunc.funcTab;
  Lista listaFunciones = almFunc.funcLista;
  TablaHash listas = inicializador_listas();
  printf("Bienvenido al interprete de funciones de lista, ejecute una sentencia\
 a continuacion o ingrese Q para salir del programa.\n");
  char* cadena = obtener_linea_de_consola();

  while (strcmp(cadena, "Q") != 0) {
    Cola colaTokens = lexer(cadena);
    AST arbol = parser(colaTokens);
    ValorSentencia sentVal = interprete(arbol, listas, tablaFunciones, 
                                        listaFunciones);
    int codigoSalida = manejador_de_salida(sentVal, listas, tablaFunciones, 
                                            listaFunciones);
    if (codigoSalida) {
      printf("Sentencia ejecutada con exito.\n");
    } else {
      printf("La sentencia tiene un error.\n");
    }
    cadena = obtener_linea_de_consola();
  }

  free(cadena); // Libero la ultima Q.
  tablahash_destruir(tablaFunciones);
  lista_destruir(listaFunciones);
  tablahash_destruir(listas);
}

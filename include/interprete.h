#ifndef INTERPRETE_H
#define INTERPRETE_H

#include "../src/estructuras/especificas/ast/ast.h"
#include "../src/estructuras/especificas/valor-sentencia/valor-sentencia.h"
#include "../src/estructuras/genericas/tabla-hash/tablahash.h"
#include "../src/estructuras/genericas/lista/lista.h"

/**
 * interprete: Dado un AST, una tabla hash con listas, otra con funciones y
 * una lista con funciones, la funcion retorna una estructura ValorSentencia
 * si la sentencia es una sentencia correcta. Devuelve NULL en caso contrario.
 * Ademas la funcion libera le memoria pedida por el AST.
 */
ValorSentencia interprete(AST arbol, TablaHash listas, TablaHash funcTab, 
                          Lista funcLista);

#endif // INTERPRETE_H
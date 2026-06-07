#ifndef PARSER_H
#define PARSER_H

#include "../src/estructuras/genericas/cola/cola.h"
#include "../src/estructuras/especificas/ast/ast.h"

/**
 * parser: Dada una cola de tokens, la funcion devuelve un AST con la
 * informacion de esos tokens. Si la sentencia no es valida devuelve NULL, 
 * liberando la memoria pedida por el AST, ademas en todos los casos libera
 * la memoria pedida por la cola.
 */
AST parser(Cola colaDeTokens);

#endif // PARSER_H
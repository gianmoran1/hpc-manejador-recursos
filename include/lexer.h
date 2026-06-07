#ifndef LEXER_H
#define LEXER_H

#include "../src/estructuras/genericas/cola/cola.h"

/**
 * lexer: Dado un string (linea), la funcion retorna una cola de tokens 
 * correspondiente a la linea dada. El lexer libera la memoria pedida por
 * la linea. Ademas si no es una linea valida retorna NULL.
 */
Cola lexer(char* linea);

#endif // LEXER_H
#ifndef UTILS_STR_H
#define UTILS_STR_H

/**
 * string_evalua_caracteres: Dado un string y una funcion para evaluar si los
 * caracteres del string dado cumplen cierta condicion, la funcion retorna 1 
 * si el string cumple la condicion o 0 en caso contrario.
 */
int string_evalua_caracteres(char* str, int (*CondicionCaracteres) (int ));

/**
 * string_copiar: Dado un string la funcion retorna una copia del mismo.
 */
char* string_copiar(char* str);

/**
 * string_destruir: Dado un string la funcion libera la memoria pedida por este.
 */
char* string_destruir(char* str);

/**
 * string_tokenizer: Dado un string, la funcion lo separa en tokens, esto es,
 * saca los espacios, tabulaciones, retornos de carro, etc. y me devuelve la 
 * primer subcadena del string dado que contenga cualquier caracter, dejando de
 * lado estos espacios. La funcion va modificando el string dado colocando 
 * espacios en los caracteres leidos para futuros usos del mismo.
 * Ejemplo: Dado "   soy un    string", en una primer llamada me devuelve "soy".
 */
char* string_tokenizer(char* linea);

#endif // UTILS_STR_H
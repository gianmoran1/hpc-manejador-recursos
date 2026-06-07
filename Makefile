# Makefile para la compilacion del interprete de funciones de lista.

FLAGS = -Wall -Wextra -Werror -g -pedantic

main: ./build/main.o ./build/inicializador.o ./build/tablahash.o ./build/funciones.o ./build/funciones-primitivas.o\
./build/lista.o ./build/utils-str.o ./build/entrada.o ./build/lexer.o ./build/cola.o ./build/token.o ./build/parser.o ./build/ast.o\
./build/interprete.o ./build/valor-sentencia.o ./build/salida.o ./build/utils-enteros.o
	gcc -o $@ $(FLAGS) $^ -o ./bin/$@

./build/main.o: ./src/main.c
	gcc -c $< $(FLAGS) -o $@

./build/inicializador.o: ./src/inicializador/inicializador.c
	gcc -c $< $(FLAGS) -o $@

./build/tablahash.o: ./src/estructuras/genericas/tabla-hash/tablahash.c
	gcc -c $< $(FLAGS) -o $@

./build/funciones.o: ./src/estructuras/especificas/funciones/funciones.c
	gcc -c $< $(FLAGS) -o $@

./build/funciones-primitivas.o: ./src/estructuras/especificas/funciones/funciones-primitivas/funciones-primitivas.c
	gcc -c $< $(FLAGS) -o $@

./build/lista.o: ./src/estructuras/genericas/lista/lista.c
	gcc -c $< $(FLAGS) -o $@

./build/utils-str.o: ./src/utils/utils-str.c
	gcc -c $< $(FLAGS) -o $@

./build/entrada.o: ./src/entrada/entrada.c
	gcc -c $< $(FLAGS) -o $@

./build/lexer.o: ./src/lexer/lexer.c
	gcc -c $< $(FLAGS) -o $@

./build/cola.o: ./src/estructuras/genericas/cola/cola.c
	gcc -c $< $(FLAGS) -o $@

./build/token.o: ./src/estructuras/especificas/token/token.c
	gcc -c $< $(FLAGS) -o $@

./build/parser.o: ./src/parser/parser.c
	gcc -c $< $(FLAGS) -o $@

./build/ast.o: ./src/estructuras/especificas/ast/ast.c
	gcc -c $< $(FLAGS) -o $@

./build/interprete.o: ./src/interprete/interprete.c
	gcc -c $< $(FLAGS) -o $@

./build/valor-sentencia.o: ./src/estructuras/especificas/valor-sentencia/valor-sentencia.c
	gcc -c $< $(FLAGS) -o $@

./build/salida.o: ./src/salida/salida.c
	gcc -c $< $(FLAGS) -o $@

./build/utils-enteros.o: ./src/utils/utils-enteros.c
	gcc -c $< $(FLAGS) -o $@

clean:
	rm ./build/*.o
	rm ./bin/main

.PHONY = clean
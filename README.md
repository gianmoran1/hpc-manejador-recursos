# hpc-manejador-recursos
  Este es un middleware distribuido que gestiona los recursos de un cluster HPC 
simulado. Este programa es un nodo autonomo.

  Compilación en C: Para compilar el programa basta con colocar << make >> en la 
terminal.

  Ejecución en C: Para correr el intérprete colocamos << ./bin/main >> en la
terminal.
Para ejecutarlo con Valgrind colocamos << valgrind -s --leak-check=full 
--track-origins=yes --show-leak-kinds=all ./bin/main >> 

  Compilación en Erlang: Desde una terminal Erlang se coloca:
c(loggerScheduler), c(tcpClient), c(manejoRecursos), c(jobWorker), c(scheduler), c(main).
  
  Ejecución en Erlang: Desde la terminal habiendo compilado todo, el programa
se inicia con: main:main().

  Test: FALTA.

  En la carpeta "bin" se guarda el binario resultado de la compilación del
programa. En "build" se guardan los archivos objeto (.o). En "docs" se halla la
documentación del programa. La carpeta "include" es la que tiene los archivos de
cabecera "principales". En "src" se guardan los archivos .c y .erl de los 
diferentes módulos del programa y en algunos casos se guardan
junto a sus archivos de cabecera debido a que la implementación es "interna".
Por último, en la carpeta "test" se halla un archivo para probar el
programa. 

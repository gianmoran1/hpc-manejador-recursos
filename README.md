# hpc-manejador-recursos

Middleware distribuido que gestiona los recursos de un cluster HPC simulado. Cada instancia es un nodo autónomo compuesto por un agente C (gestión de recursos y comunicación de red) y un scheduler Erlang (asignación de trabajos).

## Compilación

**Agente C:**
```
make
```
El binario queda en `bin/main`. Los archivos objeto intermedios se guardan en `build/`.

**Scheduler Erlang:**
```
make erlang
```
Los `.beam` compilados quedan en `src/erlang-scheduler/`.

## Ejecución

**Agente C:**
```
make run
```
O directamente:
```
./bin/main
```

**Agente C con Valgrind:**
```
make valgrind
```

**Scheduler Erlang** (desde una terminal Erlang, con los `.beam` ya compilados):
```erlang
main:main().
```

## Tests
```
make test
```

## Limpieza
```
make clean
```
Elimina los objetos de `build/`, el binario de `bin/` y los `.beam` de Erlang.

## Estructura del proyecto

```
bin/        Binario compilado del agente C
build/      Archivos objeto intermedios (.o)
docs/       Documentación del proyecto
include/    Cabeceras públicas del agente C (config.h, servidor.h, gestor_estado.h, red/…) + config.hrl de Erlang
src/
  c-agent/            Agente C (fuentes .c y cabeceras internas)
  erlang-scheduler/   Scheduler Erlang (.erl y .beam)
test/       Scripts de prueba
```

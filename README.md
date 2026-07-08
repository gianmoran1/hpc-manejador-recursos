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

**Scheduler Erlang** (desde `src/erlang-scheduler/`, con los `.beam` ya compilados):
```
cd src/erlang-scheduler
erl
```
```erlang
main:main().
```

## Cómo correr el sistema completo

Cada nodo se compone de un agente C (el servidor de recursos) y un scheduler Erlang (el planificador). El orden importa: **primero se levanta el agente C, después el scheduler**, porque el scheduler se conecta a él.

1. En una terminal, levantar el agente: `make run` (o `./bin/main`). Queda escuchando en el puerto TCP **4040** (tanto para la conexión local de Erlang como para otros agentes de la red) y usa el puerto UDP **12529** para el descubrimiento.
2. En otra terminal, compilar Erlang (`make erlang`) y arrancar el scheduler con `main:main()`. Este se conecta al agente local en `127.0.0.1:4040`, pide la lista de nodos y empieza a generar trabajo.

Si el agente todavía no está corriendo, el scheduler reintenta la conexión con backoff hasta que aparezca, así que también se lo puede arrancar primero sin problema.

**Interoperabilidad entre máquinas:** para probar el clúster distribuido se corre un nodo (agente C + scheduler) en cada máquina de la misma red local. Los agentes se descubren entre sí automáticamente por broadcast UDP (mensaje `ANNOUNCE`), mantienen una tabla de nodos actualizada y pueden reservar recursos de forma cruzada. No hace falta configurar direcciones a mano: cada agente extrae la IP de los anuncios que recibe.

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

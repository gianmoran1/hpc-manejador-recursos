# Bugs conocidos — revisión de `estructuras/` y `modelo/`

Este documento junta los bugs encontrados durante la revisión módulo por módulo
de las estructuras genéricas (`src/c-agent/estructuras/`) y del modelo de
dominio (`src/c-agent/modelo/`), hecha como parte de la refactorización del
agente C. Todos están además marcados con un comentario `BUG:` en el lugar
exacto del código donde ocurren.

Ninguno de estos bugs impide compilar ni rompe los tests actuales
(`test/test_deadlock_simple.sh`, 28/28 OK) — son casos borde, condiciones de
carrera o límites no validados que no se disparan con el flujo feliz ni con
los escenarios que cubre el test.

---

## `modelo/nodos.c` / `nodos.h`

### 1. `nodo_limpiar_conexion_por_fd` borra el nodo entero en vez de solo desvincular la conexión

**Dónde:** `nodos.c`, dentro de `nodo_limpiar_conexion_por_fd` (línea ~238 en
adelante); documentado también en `nodos.h` junto a la declaración de la
función.

**Qué pasa:** el nombre de la función y su propio comentario en `nodos.h`
prometen *"pone a NULL el campo conexion... no libera memoria (el caller lo
hace)"*. La implementación real, en cambio, llama a `tablahash_eliminar`
sobre el nodo completo — es decir, **borra el `Nodo` entero del registro**
(ip, puerto, cpu/gpu/mem conocidos), no solo la conexión TCP cacheada.

**Por qué importa:** `docs/arquitectura.md` documenta que un nodo remoto solo
se elimina del registro tras 15 segundos sin `ANNOUNCE`. Con este bug, un
nodo perfectamente vivo desaparece de `GET_NODES` apenas se cae **una
conexión TCP saliente cacheada** hacia él (por ejemplo, por un hipo de red
momentáneo), mucho antes de que se cumplan esos 15 segundos. El scheduler de
Erlang puede perder de vista temporalmente un nodo con recursos disponibles.

**Fix sugerido:** no llamar a `tablahash_eliminar` acá; solo dejar
`conexion = NULL` en el `Nodo` y dejar que sea `desconectar()` (por timeout
de 15s) o un futuro `ANNOUNCE` los que decidan si el nodo sigue vivo.

---

### 2. Race de concurrencia: `nodo_destruir` puede cerrar/liberar una conexión que otro hilo está usando

**Dónde:** `nodos.c`, función estática `nodo_destruir` (línea ~12), llamada
desde `desconectar()` cada vez que expira el timer de 15s.

**Qué pasa:** `nodo_destruir` hace `close(nodo->conexion->fd)` y
`free(nodo->conexion)`. Ese mismo puntero `ClienteConectado*` es el que
`Agente.c` guarda en `eventos[n].data.ptr` dentro del epoll compartido por
los 4 hilos worker, y el despacho de eventos (`bucle_principal`) lee
`entidad->fd` **sin tomar `estado->lock`**.

**Por qué importa:** si `desconectar()` corre (con el lock tomado) justo
cuando otro hilo despierta con un evento pendiente para ese mismo fd (por
ejemplo, un `GRANTED`/`DENIED` que llega justo antes de que el nodo expire
por silencio), ese otro hilo termina leyendo memoria ya liberada
(use-after-free). Peor: el fd puede haber sido reciclado por el kernel para
un socket totalmente distinto entre el `close()` de acá y el `recv()` del
otro hilo, haciendo que el mensaje se procese sobre la conexión equivocada.

**Nota:** este bug y el anterior comparten la misma raíz — nada coordina
"tirar abajo una conexión cacheada en `TablaNodos`" con "el epoll la está
usando ahora mismo desde otro hilo". Conviene pensarlos y resolverlos juntos.

**Relacionado (en otro archivo, no tocado en esta revisión):**
`controlador.c:121-122` hace
`Nodo nodo = gestor_buscar_nodo_por_ip(ip_destino, estado); int puerto_destino = nodo->puerto;`
sin chequear que `nodo` no sea `NULL`. Si el bug #1 de arriba (u otra causa)
hace que un nodo esperado no esté en el registro cuando llega un
`JOB_REQUEST` para su IP, este `NULL` deref tira abajo el agente entero.

### Código sin uso

`reiniciar_timestamp` y `buscar_nodo` están declaradas e implementadas pero
ningún otro archivo las llama (`procesar_anuncio` hace su propio upsert, y
todo el código usa `buscar_nodo_por_ip` en vez de `buscar_nodo`). No son
bugs, pero quedaron marcadas como candidatas a eliminar en `nodos.h`.

---

## `modelo/transacciones.c` / `transacciones.h`

### 3. `PeticionMulti.nodos[]` es de tamaño fijo y nada valida el límite antes de escribir

**Dónde:** `transacciones.h`, campo `nodos[MAX_NODOS_PETICION]` del struct
`peticionMulti_` (antes `nodos[16]`, ahora con la constante nombrada).

**Qué pasa:** `total` (cuántos recursos remotos tiene el `JOB_REQUEST`) se
calcula en `controlador.c` contando los tokens `@IP:recurso:cant` del mensaje
que manda Erlang, y **nada valida `total <= MAX_NODOS_PETICION`** antes de
que `controlador.c` escriba en `peticion->nodos[idx]` para `idx` creciente.

**Por qué importa:** un `JOB_REQUEST` con más de 16 recursos desborda este
arreglo fijo — es un heap buffer overflow sobre el resto del `malloc` de la
`PeticionMulti`, con la corrupción de memoria que eso implica.

**Fix sugerido:** el arreglo en sí ya tiene su tope nombrado
(`MAX_NODOS_PETICION`, agregado en esta revisión), pero el fix real va en
`controlador.c`: cortar o rechazar el `JOB_REQUEST` con `JOB_DENIED` si
`total > MAX_NODOS_PETICION`, antes de llamar a `peticion_crear`.
`controlador.c` además tiene tres arreglos propios hardcodeados en `16`
(`fds[16]`, `recursos_rb[16][32]`, etc. en las funciones de rollback) que
deberían pasar a usar esta misma constante cuando se toque ese archivo.

---

## `estructuras/tablahash.c` / `tablahash.h`

### 4. Las tombstones nunca se limpian si el churn de inserts/deletes no hace crecer `numElems`

**Dónde:** `tablahash.c`, la condición de resize dentro de
`tablahash_insertar` (línea ~153: `if (((float)tabla->numElems / (float)tabla->capacidad) > FACTOR_CARGA) tablahash_redimensionar(tabla);`).

**Qué pasa:** el único lugar que limpia las tombstones (casillas marcadas
`eliminado = 1`) es `tablahash_redimensionar`, porque al reconstruir la tabla
desde cero solo reinserta las celdas con dato vivo. Pero el resize se dispara
únicamente mirando `numElems / capacidad`, y `numElems` **no cuenta
tombstones**, solo elementos vivos.

**Por qué importa:** en tablas con mucho churn (insertar + eliminar
repetidamente) pero pocos elementos vivos en un momento dado — que es
exactamente el patrón de `libro_contable` (jobs que se crean y liberan todo
el tiempo) o `registro_nodos` (nodos que se conectan/desconectan) — las
celdas se van llenando de tombstones sin que el resize se dispare nunca.
Con el tiempo, cada `insertar`/`buscar`/`eliminar` degrada hacia un escaneo
lineal de toda la capacidad, y no hay forma de que se recupere solo.

**Estado:** no rompe nada hoy con `capacidad` inicial de 100 y la carga de
este proyecto; se decidió dejarlo así por ahora. Fix sugerido si se retoma:
que el load factor cuente celdas ocupadas (`dato != NULL || eliminado`) en
vez de solo `numElems`, para que el resize también limpie tombstones por
acumulación y no solo por crecimiento real.

### Código sin uso

`tablahash_nelems` y `tablahash_capacidad` no tienen ningún llamador en el
proyecto.

---

## `estructuras/cola.c` / `cola.h` — ya resuelto

`cola_desencolar_void` tenía un `NULL` dereference si se la llamaba con una
cola vacía sin crear (`*cola == NULL`): el chequeo `cola != NULL` solo
validaba el puntero-a-puntero, no el contenido, y la versión sin `_void`
(`cola_desencolar`) sí lo validaba bien. La función (junto con
`cola_encolar_void` y `cola_recorrer`, que tampoco se usaban) se eliminó del
proyecto, así que el bug quedó resuelto por eliminación del código muerto.

---

## `estructuras/glist.c` / `glist.h` — sin bugs

Revisado a fondo, sin bugs. `glist_vacia`, `glist_recorrer` y el typedef
`Predicado` quedaron sin uso en el proyecto (no son bugs, solo código
muerto).

## `modelo/recursos.c` / `recursos.h` — sin bugs

Revisado a fondo, sin bugs.

## `modelo/jobs.c` / `jobs.h` — sin bugs

Revisado a fondo, sin bugs. El módulo mantiene una lista y una tabla hash en
paralelo apuntando a los mismos `JobActivo`, con un orden de liberación
específico para no hacer doble free (documentado con comentarios en
`destruir_tabla_jobs` y `no_destruye_job`), pero el orden actual es correcto.

---

# Bugs conocidos — revisión de archivos núcleo

Esta segunda tanda sale de revisar los archivos núcleo del agente (todavía sin
refactorizar por completo): `Sockets.c`, `cliente.c/.h`, `gestor_estado.c` y la
función `main` de `Agente.c`. Al igual que los anteriores, ninguno rompe la
compilación ni los tests: `test/test_deadlock_simple.sh` compila **solo** el
gestor y el modelo (sin sockets), así que nada de lo de abajo se ejercita ahí.

---

## `Sockets.c` / `Sockets.h`

### 5. `mk_udp_lsock` usa `SO_REUSEADDR` donde el comentario pide `SO_REUSEPORT`

**Dónde:** `Sockets.c`, dentro de `mk_udp_lsock` (el `setsockopt` del socket UDP);
marcado con un comentario `// BUG:` en el lugar exacto.

**Qué pasa:** el comentario del propio código declara la intención —"que varios
nodos en la misma PC de pruebas escuchen el mismo broadcast"— que es
exactamente el caso de uso de `SO_REUSEPORT`. Pero el código activa
`SO_REUSEADDR`.

**Por qué importa:** en Linux, para tener **varios sockets bindeados al mismo
puerto UDP con `INADDR_ANY`**, `SO_REUSEADDR` no alcanza: el segundo `bind()`
falla con `EADDRINUSE`. Como el bind está envuelto en `quit()`, el **segundo
agente lanzado en la misma máquina aborta al arrancar**. Es decir, correr dos o
más agentes en un mismo host (el escenario típico de prueba de
autodescubrimiento por UDP) no funciona. No se dispara en
`test_deadlock_simple.sh` porque ese test nunca crea sockets.

**Fix sugerido:** usar `SO_REUSEPORT` (o `SO_REUSEADDR | SO_REUSEPORT`) en ese
`setsockopt`. No se aplicó todavía por consigna (solo documentar).

### Robustez menor (no rompen el flujo feliz)

- **`obtener_mi_ip_local`**: no chequea el retorno de `connect()` ni
  `getsockname()`. Si `getsockname` fallara, `name` queda sin inicializar y el
  `inet_ntoa(name.sin_addr)` copiaría basura o `0.0.0.0` a `mi_ip_publica`.
  Impacto bajo (el fallback inicial ya cubre el caso de socket no creado), pero
  conviene validar y caer a `"127.0.0.1"` también acá.
- **`mk_tcp_lsock` / `mk_udp_lsock`**: `struct sockaddr_in sa` no se hace
  `memset` a 0 antes del `bind` (a diferencia de `conectar_a_nodo` y
  `obtener_mi_ip_local`, que sí lo hacen). El padding `sin_zero` queda sin
  inicializar. En la práctica `bind` con `AF_INET` lo ignora, pero es
  inconsistente y es buena higiene ponerlo en cero.
- **`inet_addr(ip)`** no se valida: ante una IP mal formada devuelve
  `INADDR_NONE` (0xFFFFFFFF) sin avisar. Hoy la IP viene de `getsockname`, así
  que no se dispara, pero es un `bind` a una dirección basura latente.

### Código muerto / limpieza (ya aplicada en esta revisión)

- Se eliminó el `extern char mi_ip_publica[16];` que estaba al tope de
  `Sockets.c`: ninguna función del archivo lo usaba (todas reciben la IP por
  parámetro). La variable sigue viviendo en `Agente.c` y su `extern` está en
  `Agente.h`.
- El backlog hardcodeado `10` de `listen()` pasó a la constante `BACKLOG_TCP`.

---

## `cliente.c` / `cliente.h`

Sin bugs de comportamiento. Observaciones:

- **`crear_cliente_conectado` no chequea el `malloc`** → NULL-deref si el
  sistema se queda sin memoria. Es el mismo patrón que el resto del proyecto
  (`recurso_crear`, `crear_nodo`, etc. tampoco chequean), así que se deja como
  está por consistencia; se anota como deuda de robustez global.
- **No hay `destruir_cliente_conectado`**: los `ClienteConectado` se liberan con
  `free()` crudo desde `Agente.c` (`bucle_principal`) y el `close(fd)+free` está
  duplicado en `nodo_destruir`. Un destructor propio (`close(fd)` + `free`)
  centralizaría eso y ayudaría a coordinar el fix del bug de concurrencia #2
  (use-after-free de la conexión cacheada). Sugerencia de refactor, no bug.

**Ubicación del módulo:** `ClienteConectado` no es modelo de dominio (no es un
recurso/job/nodo); es infraestructura de transporte —un fd + su buffer de
acumulación—, fuertemente acoplado a `Sockets.c`. Recomendación: **no** moverlo
a `modelo/`. Lo natural es agruparlo con la capa de red (junto a `Sockets`),
idealmente en una carpeta `red/` que contenga `Sockets.*` + `cliente.*`. Si no
se quiere tocar el `Makefile`/includes ahora, dejarlo en la raíz junto a
`Sockets` es aceptable.

---

## `gestor_estado.c` (hasta `estado_destruir`)

Sin bugs en el tramo revisado (callbacks de Cola/TablaHash, `obtener_recurso`,
`estado_crear`, `estado_destruir`). Observaciones:

- **`estado_crear` no chequea el `malloc`** (mismo criterio que arriba).
- **`estado_destruir` no tiene ningún llamador**: `main` corre un servidor
  infinito y nunca lo invoca. Es código muerto de facto (no es un bug; se anota
  para eventual limpieza o para cuando se agregue un apagado ordenado).

---

## `Agente.c` — `main` / `agregar_cliente_en_epoll`

Sin bugs de comportamiento. Observaciones:

- **`agregar_fd_en_epoll`** (declarada en `Agente.h`, definida en `Agente.c`) no
  tiene ningún llamador → código muerto candidato a eliminar. (No se tocó: está
  fuera del alcance de esta pasada, que fue `main` + `agregar_cliente_en_epoll`.)
- **Leak "still-reachable"**: los 5 `ClienteConectado` que envuelven los sockets
  de escucha y los timers se crean con `crear_cliente_conectado` y nunca se
  liberan. Como viven toda la vida del proceso no crecen, pero `valgrind`
  (`make valgrind`) los reporta como bloques aún alcanzables al salir.
- **Pregunta respondida (`estado` == NULL al llegar un TCP?)**: no. `estado` se
  inicializa en `main` *antes* de crear el epoll, registrar sockets y lanzar los
  hilos, así que cuando cualquier worker procesa un mensaje el estado ya existe.

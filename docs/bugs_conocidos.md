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

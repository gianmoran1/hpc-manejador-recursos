# Bugs conocidos — agente C

Bugs y observaciones encontrados en la revisión módulo por módulo del agente C.
Los que siguen **abiertos** están además marcados con un comentario `// BUG:` en
el lugar exacto del código. Ninguno rompe la compilación ni los tests
(`test/test_deadlock_simple.sh`) — son casos borde, condiciones de carrera o
límites que no se disparan con el flujo feliz.

> Los bugs ya resueltos (#3 tope de `nodos[]`, #5 `SO_REUSEPORT`, #6 dangling-else
> UDP, #7 data race al llenar `peticion->nodos[]`, #8 atribución de `GRANTED`, el
> `NULL`-deref de `gestor_buscar_nodo_por_ip`, y el de `cola`) se removieron de
> este documento.

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
`servidor.c` guarda en `eventos[n].data.ptr` dentro del epoll compartido por
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

**Relacionado — misma raíz, en `controlador.c` `erlang_job_request`:** el
chequeo de `nodo == NULL` ya se agregó, pero **el acceso al registro de nodos en
ese camino no está protegido por `estado->lock`**:

- `gestor_buscar_nodo_por_ip` toma el lock, busca y lo **suelta antes de
  devolver** el puntero `Nodo`. Después `nodo->puerto` se derefencia sin lock: si
  el timer de mantenimiento (`gestor_desconectar_nodos`) libera ese nodo en el
  medio → **use-after-free**.
- `nodo_obtener_conexion` y `nodo_registrar_conexion` (en `nodos.c`) **no toman
  ningún lock** (no hay un solo `pthread_mutex` en ese archivo) y tocan
  `registro_nodos` directamente, mientras el hilo del `ANNOUNCE`
  (`gestor_procesar_anuncio`) y el timer lo modifican bajo `estado->lock` →
  **data race sobre la `TablaNodos`** (corrupción del hash, lecturas rotas).
- El `ClienteConectado*` que devuelve `nodo_obtener_conexion` puede quedar
  liberado por otro hilo antes de usar su `fd` (use-after-free, familia de #2).

**Fix (junto con #1/#2):** que toda la interacción con `registro_nodos` en
`erlang_job_request` pase por la fachada del gestor bajo `estado->lock`, y no
retener punteros a `Nodo`/`ClienteConectado` a través de fronteras de lock.

### Código sin uso

`reiniciar_timestamp` y `buscar_nodo` están declaradas e implementadas pero
ningún otro archivo las llama (`procesar_anuncio` hace su propio upsert, y
todo el código usa `buscar_nodo_por_ip` en vez de `buscar_nodo`). No son
bugs, pero quedaron marcadas como candidatas a eliminar en `nodos.h`.

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

Segunda tanda: archivos núcleo del agente. Al igual que los anteriores, ninguno
rompe la compilación ni los tests (`test/test_deadlock_simple.sh` compila **solo**
el gestor y el modelo, sin sockets).

---

## `Sockets.c` / `Sockets.h`

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

---

## `cliente.c` / `cliente.h`

Sin bugs de comportamiento. Observación:

- **`crear_cliente_conectado` no chequea el `malloc`** → NULL-deref si el
  sistema se queda sin memoria. Es el mismo patrón que el resto del proyecto
  (`recurso_crear`, `crear_nodo`, etc. tampoco chequean), así que se deja como
  está por consistencia; se anota como deuda de robustez global.

---

## `gestor_estado.c`

Sin bugs en el tramo revisado (callbacks de Cola/TablaHash, `obtener_recurso`,
`estado_crear`, `estado_destruir`). Observaciones:

- **`estado_crear` no chequea el `malloc`** (mismo criterio que arriba).
- **`estado_destruir` no tiene ningún llamador**: `main` corre un servidor
  infinito y nunca lo invoca. Es código muerto de facto (no es un bug; se anota
  para eventual limpieza o para cuando se agregue un apagado ordenado).

---

## `main.c` / `servidor.c`

Sin bugs de comportamiento. Observaciones:

- **Leak "still-reachable"**: los 5 `ClienteConectado` que envuelven los sockets
  de escucha y los timers se crean con `crear_cliente_conectado` y nunca se
  liberan. Como viven toda la vida del proceso no crecen, pero `valgrind`
  (`make valgrind`) los reporta como bloques aún alcanzables al salir.

---

## `controlador.c` — `procesar_mensaje_red_c`

### Observación (a verificar, no marcada inline): RELEASE a un nodo que solo encoló

En el rollback por `DENIED` se manda `RELEASE` a **todos** los nodos, incluidos
los que solo encolaron el `RESERVE` (no lo concedieron). `gestor_manejar_release`
opera sobre lo **asignado** en el libro contable; un `RESERVE` que quedó en la
cola de pendientes (sin asignar) no tiene asignación que liberar, así que el
`RELEASE` no lo saca de la cola. Ese pedido encolado podría concederse más tarde
(vía la redistribución greedy) y disparar un `GRANTED` a un coordinador que ya
abandonó el job → recurso asignado a un job muerto (leak en el nodo dueño).
Conviene confirmarlo leyendo la lógica de colas de `gestor_manejar_release`.

---

## Pendiente transversal: timeout del coordinador

La `PeticionMulti` que crea `erlang_job_request` **no expira nunca** si un peer
queda mudo (nunca responde `GRANTED`/`DENIED`, o el `RESERVE` no llegó). El timer
de mantenimiento (`timer_deadlock_nodos` → `gestor_expirar_pedidos`) solo expira
los `RESERVE` **encolados localmente**, no las `peticiones_pendientes` salientes.
Resultado: el job se cuelga (Erlang nunca recibe `JOB_GRANTED` ni `JOB_DENIED`) y
la petición queda leakeada. El fix vive en el timer/gestor, pero la petición nace
en esta rama. Requiere primero definir el contrato con Erlang (qué mensaje espera
ante un timeout: la doc menciona `JOB_TIMEOUT` pero el código reusa `JOB_DENIED`).

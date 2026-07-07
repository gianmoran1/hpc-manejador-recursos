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

### Concurrencia del registro de nodos — resuelto

Los #1 (borrado indebido del nodo al limpiar la conexión), #2 (use-after-free de
la conexión cacheada desde el timer) y la data race del registro en
`erlang_job_request` se resolvieron con un **modelo de ownership**: el
`ClienteConectado` es propiedad del loop de epoll; el registro guarda solo una
referencia débil (`nodo->conexion`) que nunca cierra ni libera.
`nodo_limpiar_conexion_por_fd` solo pone `conexion = NULL`; `nodo_destruir` ya no
toca la conexión. Todo acceso a la `TablaNodos` desde el controlador pasa por
facades del gestor bajo `estado->lock` (`gestor_obtener_destino`,
`gestor_registrar_conexion`, `gestor_limpiar_conexion_por_fd`), sin retener
punteros a `Nodo`/`ClienteConectado` fuera del lock.

**Residuales aceptables (a tener presente):**

- Si el timer saca un nodo que todavía tenía una conexión viva, esa conexión
  queda **huérfana** (sigue en el epoll y funciona; se libera cuando se cierra).
  Un `ANNOUNCE` posterior de ese nodo abre una conexión nueva → puede haber dos
  conexiones al mismo nodo hasta que la vieja se cierre. Leak acotado.
- En `erlang_job_request`, el `fd` de la conexión cacheada se lee bajo lock pero
  el `send` del `RESERVE` se hace fuera del lock. Si otro hilo cierra esa
  conexión en esa ventana mínima, el `send` falla (manejable) o —muy improbable—
  va a un fd reciclado. El fix robusto sería refcounting de la conexión; se dejó
  así por costo/beneficio.

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
- **`atender_cliente_udp` no filtra el eco propio**: el agente recibe y procesa
  su propio `ANNOUNCE` (se observó en la corrida: `ANNOUNCE recibido: <mi_ip>`).
  Impacto nulo (se re-registra a sí mismo, cosa que ya hace al arrancar), pero
  `docs/arquitectura.md` supone que se descarta por IP propia y no es así.

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

## Timeout del coordinador — resuelto

La `PeticionMulti` ahora expira: se le agregó `instante_creacion`, y el timer
(`controlador_timer` → `gestor_expirar_peticiones`) recorre `peticiones_pendientes`
(vía el nuevo `tablahash_recorrer`) y, por cada una que superó
`TIEMPO_ESPERA_RESERVA` sin completarse (`respondidos < total`), manda `RELEASE` a
todos sus nodos + `JOB_TIMEOUT` a su Erlang (que reintenta con backoff) y la
destruye.

Contrato con Erlang (confirmado leyendo `jobWorker.erl`/`tcpClient.erl`):
`JOB_DENIED` = fallo permanente (el worker no reintenta); `JOB_TIMEOUT` = reintenta.
Por eso el timeout se manda como `JOB_TIMEOUT`, no `JOB_DENIED`. **No se agregó
ningún mensaje de timeout entre agentes**: el coordinador detecta el vencimiento
por sí mismo. El timeout del dueño (`gestor_expirar_pedidos`) pasó a **desencolar
en silencio** (higiene de cola); ya no le manda nada al coordinador.

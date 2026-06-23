# Agente C — Documentación de arquitectura y funciones

## Visión general

El agente C es un proceso por nodo que cumple tres roles simultáneos:

1. **Servidor de recursos local**: administra CPU, GPU y RAM de la máquina donde corre y responde pedidos de reserva de otros nodos.
2. **Cliente de recursos remoto**: cuando Erlang pide un trabajo que requiere recursos en otro nodo, el agente conecta a ese nodo, le envía `RESERVE` y rastrea la respuesta.
3. **Puente de protocolo**: traduce entre el protocolo Erlang (`JOB_REQUEST / JOB_RELEASE / GET_NODES`) y el protocolo C-a-C (`RESERVE / GRANTED / DENIED / RELEASE`).

El proceso arranca un epoll con cuatro hilos worker que comparten el mismo descriptor. Los sockets de escucha se registran con `EPOLLEXCLUSIVE` (para evitar thundering herd) y los clientes conectados con `EPOLLONESHOT` para que a lo sumo un hilo procese cada evento.

---

## Módulos

```
Agente.c / Agente.h       — bucle de eventos principal, sockets de escucha, main
controlador.c / .h        — parseo de mensajes y lógica de protocolo
gestor_estado.c / .h      — fachada thread-safe sobre recursos, jobs y nodos
recursos.c / .h           — RecursoLocal con cola de pendientes
jobs.c / .h               — TablaJobs: libro contable de recursos asignados
nodos.c / .h              — TablaNodos: directorio de agentes conocidos
transacciones.c / .h      — PeticionMulti: transacción de JOB_REQUEST multi-nodo
Sockets.c / .h            — primitivas de red (TCP/UDP/timers)
cliente.c / .h            — ClienteConectado: buffer por socket
cola.c / .h               — cola FIFO genérica
glist.c / .h              — lista enlazada genérica
tablahash.c / .h          — tabla hash genérica
```

---

## Protocolos de mensajes

### Erlang → Agente (puerto local 127.0.0.1:4040)

| Mensaje | Descripción |
|---------|-------------|
| `GET_NODES` | Pide la tabla de nodos conocidos. Respuesta: `NODES IP:puerto:cpu:X:mem:Y:gpu:Z[;...]` |
| `JOB_REQUEST <id> @IP:recurso:cant [...]` | Solicita reservas en uno o varios nodos remotos. Respuesta eventual: `JOB_GRANTED <id>` o `JOB_DENIED <id>` |
| `JOB_RELEASE <id>` | El trabajo terminó; liberar todos los recursos reservados en nodos remotos y locales |

### Agente C ↔ Agente C (puerto público IP:4040)

| Mensaje | Dirección | Descripción |
|---------|-----------|-------------|
| `RESERVE <id> <recurso> <cant>` | A → B | Pide que B reserve `cant` unidades del recurso `recurso` para el job `id` |
| `GRANTED <id>` | B → A | Reserva concedida |
| `DENIED <id>` | B → A | Reserva rechazada (recurso lleno y cola llena, o cantidad inválida) |
| `RELEASE <id> <recurso> <cant>` | A → B | Cancela / libera `cant` unidades del recurso `recurso` del job `id` en B (rollback o fin de trabajo) |

### UDP broadcast (puerto 12529)

| Mensaje | Descripción |
|---------|-------------|
| `ANNOUNCE <puerto> cpu:<n> gpu:<g> mem:<m>` | Autodescubrimiento periódico cada 5 s. La IP del remitente la aporta el propio datagrama UDP. Los nodos que no se anuncian en 15 s son eliminados del registro |

---

## Flujo de un `JOB_REQUEST`

```
Erlang                Agente A                Agente B (remoto)
  |                      |                         |
  |-- JOB_REQUEST 42 --> |                         |
  |   @IP_B:gpu:1        |                         |
  |                      |-- RESERVE 42 gpu 1 ----> |
  |                      |                    gestor_manejar_reserva()
  |                      |                    ├ disponible → GRANTED
  |                      |                    └ ocupado   → encolado (espera)
  |                      | <-- GRANTED 42 --------- |
  |                      | marca nodo grantado       |
  |                      | (si todos grantaron)      |
  | <-- JOB_GRANTED 42 --|                         |
  |                      |                         |
  |-- JOB_RELEASE 42 --> |                         |
  |                      |-- RELEASE 42 ---------->|
  |                      |                    gestor_liberar_job()
```

Si algún nodo responde `DENIED`, Agente A envía `RELEASE` a **todos** los nodos del request (grantados o pendientes) y notifica `JOB_DENIED` a Erlang.

---

## Gestión de deadlock

Un deadlock puede ocurrir cuando dos agentes se esperan mutuamente para recursos ocupados. La estrategia implementada es **timeout + backoff en Erlang**:

1. Cuando un RESERVE no puede cumplirse de inmediato, la solicitud queda en la cola del recurso con su `instante_llegada`.
2. El timer de expiración (cada 15 s) llama a `gestor_expirar_pedidos`. Si un pedido supera `TIEMPO_ESPERA` (30 s por defecto), se lo saca de la cola y se dispara el callback que envía `TIMEOUT <id>` al agente coordinador que originó el RESERVE.
3. El agente coordinador que recibe `TIMEOUT` ejecuta rollback (envía `RELEASE` a todos sus nodos) y notifica `JOB_TIMEOUT <id>` a Erlang.
4. Erlang aplica backoff aleatorio y reintenta con `JOB_REQUEST`.

---

## Módulo: `gestor_estado`

Fachada principal. Todas sus funciones que acceden al estado global toman `estado->lock` internamente (excepto `gestor_buscar_peticion` y `gestor_eliminar_peticion`, que deben llamarse con el lock ya tomado por el caller).

### `EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem)`
Aloja e inicializa la estructura global: tres recursos locales (CPU, GPU, MEM), el libro contable de jobs activos, el registro de nodos conocidos y la lista de peticiones multi-recurso en vuelo. Retorna el puntero al estado.

### `void estado_destruir(EstadoGlobal estado)`
Libera toda la memoria del estado: recursos, libro contable, tabla de nodos y peticiones pendientes.

### `int gestor_manejar_reserva(EstadoGlobal, char *nombre, int job_id, int socket, int cantidad)`
Intenta reservar `cantidad` unidades del recurso `nombre` para el job `job_id`. Retorna:
- `1` — **GRANTED**: había recursos disponibles y la cola estaba vacía.
- `0` — **encolado**: el recurso estaba ocupado o había pedidos anteriores esperando; la solicitud queda en la cola FIFO del recurso.
- `-1` — **DENIED**: nombre de recurso desconocido, cantidad ≤ 0, o cantidad > capacidad total.

La decisión se toma bajo `lock`. El socket se registra en el libro contable junto a la cantidad asignada.

**Caso borde — FIFO estricto**: si hay pedidos en cola, un nuevo pedido que podría satisfacerse con los recursos disponibles igual queda encolado detrás de los anteriores. Esto evita inanición pero puede dejar recursos ociosos transitoriamente.

### `void gestor_manejar_release(EstadoGlobal, char *nombre, int job_id, int cantidad, void (*cb)(int,int))`
Libera `cantidad` unidades del recurso `nombre` para el job `job_id`. Después de liberar, recorre la cola del recurso de forma greedy: desencola y asigna a todos los pedidos que quepan en el disponible (en orden FIFO). Por cada pedido satisfecho llama `cb(job_id_pendiente, socket_pendiente)` para notificar al solicitante original.

**Caso borde — release excesivo**: si `cantidad` supera lo que el job tenía asignado, solo se libera lo que realmente estaba registrado (nunca queda disponible > capacidad).

### `void gestor_liberar_job(EstadoGlobal, int job_id, void (*cb)(int,int))`
Versión atómica de `gestor_manejar_release` para los tres recursos a la vez. Busca el job en el libro contable bajo lock, guarda las cantidades usadas (cpu, gpu, mem) y llama `manejar_release_aux` por cada recurso que tenga deuda. Al terminar, el job queda eliminado del libro contable.

**Caso borde — job inexistente**: si `job_id` no está en el libro, imprime un aviso y retorna sin modificar ningún recurso.

### `void gestor_expirar_pedidos(EstadoGlobal, void (*cb_timeout)(int,int))`
Recorre las colas de los tres recursos buscando pedidos cuyo `instante_llegada` sea anterior en más de `TIEMPO_ESPERA` segundos al momento actual. Por cada pedido vencido lo desencola y llama `cb_timeout(job_id, socket_origen)`. Se detiene al encontrar el primer pedido no vencido (la cola es FIFO y los pedidos más viejos están al frente).

### `void manejar_desconexion_socket(EstadoGlobal, int socket_caido, void (*cb)(int,int))`
Cuando un socket se cierra inesperadamente, libera todos los recursos que pertenecían a los jobs de ese socket. A diferencia de `gestor_liberar_job`, opera directamente sobre el libro contable sin buscarlo por job_id: elimina todas las entradas cuyo `socket_origen == socket_caido` y devuelve los recursos acumulados a las colas de pendientes.

### `char* gestor_get_nodes(EstadoGlobal)`
Retorna un string con el formato `NODES IP:puerto:cpu:X:mem:Y:gpu:Z[;...]`. El caller debe liberar el puntero con `free()`.

### `void gestor_procesar_anuncio(EstadoGlobal, char *ip, int puerto, int cpu, int gpu, int mem)`
Inserta o actualiza un nodo en el registro. Si ya existe (misma IP+puerto), actualiza sus recursos y renueva el timestamp. Si no existe, lo crea.

### `void gestor_desconectar_nodos(EstadoGlobal)`
Elimina del registro todos los nodos que no han enviado un ANNOUNCE en los últimos 15 segundos.

### Funciones de peticiones multi-recurso

#### `void gestor_registrar_peticion(EstadoGlobal, PeticionMulti)`
Inserta la petición en la tabla hash `peticiones_pendientes` (indexada por `job_id`). Toma el lock internamente.

#### `PeticionMulti gestor_buscar_peticion(EstadoGlobal, int job_id)`
Recorre la lista buscando por `job_id`. **Debe llamarse con `estado->lock` ya tomado.** Retorna `NULL` si no existe.

#### `void gestor_eliminar_peticion(EstadoGlobal, int job_id)`
Elimina y libera la petición con ese `job_id`. **Debe llamarse con `estado->lock` ya tomado.** No-op si no existe.

---

## Módulo: `transacciones`

Tipos y helpers sin estado para la transacción de un `JOB_REQUEST` multi-nodo.

### Estructura `PeticionMulti`

```c
typedef struct peticionMulti_ {
    int         job_id;
    int         socket_erlang;   // fd donde devolver JOB_GRANTED / JOB_DENIED
    int         total;           // cuántos RESERVE se enviaron
    int         respondidos;     // cuántos respondieron hasta ahora
    NodoReserva nodos[16];       // máx. 16 recursos por job
} *PeticionMulti;
```

### Estructura `NodoReserva`

```c
typedef struct nodoReserva_ {
    int  fd_remoto;
    char recurso[32];
    int  cantidad;
    int  grantado;   // 0 = pendiente, 1 = GRANTED recibido
} NodoReserva;
```

### `PeticionMulti peticion_crear(int job_id, int socket_erlang, int total)`
Aloja e inicializa la estructura con `respondidos = 0` y todos los nodos en cero.

### `NodoReserva* peticion_buscar_nodo_por_fd(PeticionMulti, int fd)`
Búsqueda lineal en `nodos[]` por `fd_remoto`. Retorna el puntero al nodo o `NULL`. El array es pequeño (≤ 16) por lo que la búsqueda lineal es apropiada.

### `void peticion_destruir(PeticionMulti)`
Libera la estructura.

---

## Módulo: `recursos`

### Estructura `RecursoLocal`

```c
typedef struct recursoLocal_ {
    char nombre[10];
    int  capacidad_total;
    int  disponible;
    Cola pendientes;    // cola FIFO de SolicitudPendiente
} *RecursoLocal;
```

### Estructura `SolicitudPendiente`

```c
typedef struct solicitudPendiente_ {
    int    job_id;
    int    socket_origen;
    int    cantidad_pedida;
    time_t instante_llegada;   // para detectar timeout
} *SolicitudPendiente;
```

---

## Módulo: `jobs`

Mantiene el libro contable de recursos actualmente asignados.

### `void registrar_asignacion(TablaJobs, int job_id, int socket, char *recurso, int cantidad)`
Si el job ya existía, suma la cantidad al campo correspondiente. Si no existía, crea la entrada. Esto permite que un mismo job acumule reservas de distintos recursos (y del mismo recurso en múltiples llamadas).

### `int registrar_liberacion(TablaJobs, int job_id, char *recurso, int cantidad)`
Descuenta la cantidad del recurso. Retorna la cantidad efectivamente liberada (≤ lo que había registrado). Si el job queda con todos sus campos en 0, lo elimina del libro y de la lista.

### `void liberar_recursos_socket(TablaJobs, int socket_caido, void (*cb)(char*, int))`
Recorre la lista de jobs activos y elimina todos los que pertenecen a `socket_caido`, acumulando los totales liberados por recurso. Al final dispara el callback una vez por recurso para que el caller pueda redistribuir esos recursos.

---

## Módulo: `nodos`

Directorio de agentes C conocidos. La clave es `(ip, puerto)`.

### `void procesar_anuncio(TablaNodos, char *ip, int puerto, int cpu, int gpu, int mem)`
Upsert: actualiza si existe, inserta si no. Renueva `ultimo_anuncio = time(NULL)`.

### `void desconectar(TablaNodos)`
Elimina nodos con `difftime(ahora, ultimo_anuncio) >= 15.0`. Recorre la lista en un solo pase.

### `char* get_nodes(TablaNodos)`
Genera el string de respuesta para `GET_NODES`. Usa `realloc` dinámico si el buffer crece. **El caller debe liberar el puntero.**

---

## Módulo: `Sockets`

Primitivas de red. Todos los sockets creados son no-bloqueantes (`O_NONBLOCK`).

### `int mk_tcp_lsock(int port, const char *ip)`
Crea un socket TCP de escucha en `ip:port` con `SO_REUSEADDR`. El agente llama a esta función dos veces: una con la IP pública (para agentes remotos) y otra con `"127.0.0.1"` (para Erlang).

### `int mk_udp_lsock(int port)`
Crea un socket UDP con `SO_BROADCAST` en `INADDR_ANY:port` para recibir anuncios de cualquier interfaz.

### `int mk_timer(int segundos)`
Crea un `timerfd` que dispara cada `segundos` segundos. Se registra en epoll igual que un socket.

### `int conectar_a_nodo(const char *ip, int puerto)`
Conexión TCP no-bloqueante con timeout de 2 s. Usa `select` para esperar el `EINPROGRESS`. Retorna el fd o `-1` en error.

### `int atender_cliente_tcp(ClienteConectado *cliente)`
Lee en loop hasta `EAGAIN` acumulando bytes en `cliente->buffer`. Retorna `1` si ok, `0` si el socket se cerró o si el mensaje supera 1023 bytes sin `\n` (protección anti-DoS).

### `int atender_cliente_udp(int usock, char *dest, size_t max)`
Lee un datagrama UDP. Descarta los mensajes provenientes de la propia IP del agente (eco del broadcast). Escribe `"IP_REMITENTE mensaje"` en `dest`. Retorna `1` si hay mensaje útil, `0` si no.

### `int enviar_mensaje_tcp(int fd, const char *mensaje)`
`send()` simple. Retorna `1` en éxito, `-1` si el socket está caído.

### `int enviar_mensaje_udp(int usock, const char *ip, int puerto, const char *mensaje)`
`sendto()` a la dirección indicada. Para el broadcast de ANNOUNCE se usa `"255.255.255.255"`.

---

## Módulo: `controlador`

### `void procesar_mensaje_erlang(ClienteConectado *cliente, char *msg)`

Parsea y despacha mensajes del socket Erlang local:

- **`GET_NODES`**: llama `gestor_get_nodes` y envía la respuesta por TCP.
- **`JOB_REQUEST <id> @IP:res:cant [...]`**:
  1. Cuenta los tokens `@IP:...` para conocer el `total`.
  2. Crea y registra una `PeticionMulti`.
  3. Para cada token: conecta a `IP:PUERTO_TCP`, registra el nodo en `peticion->nodos[]` y envía `RESERVE <id> <res> <cant>`.
  4. Si la conexión o el parseo fallan en mitad del proceso, llama a `rollback_y_denegar` que envía `RELEASE` a los nodos ya registrados y notifica `JOB_DENIED` a Erlang.
- **`JOB_RELEASE <id>`**:
  1. Busca la petición por `job_id`, envía `RELEASE <id> <recurso> <cant>` a cada nodo registrado y destruye la petición.

### `void procesar_mensaje_red_c(ClienteConectado *cliente, char *msg)`

Parsea y despacha mensajes de otro agente C:

- **`RESERVE <id> <res> <cant>`**: llama `gestor_manejar_reserva`. Si retorna `1` envía `GRANTED <id>`, si retorna `-1` envía `DENIED <id>`. Si retorna `0` (encolado) no responde de inmediato; `callback_granted_red` notificará cuando haya recursos.
- **`GRANTED <id>`**: busca la petición por `job_id`, marca el nodo (`fd == cliente->fd`) como `grantado = 1`, incrementa `respondidos`. Si `respondidos == total` envía `JOB_GRANTED <id>` a Erlang (la petición se mantiene viva hasta `JOB_RELEASE`).
- **`DENIED <id>`**: busca y destruye la petición, envía `RELEASE <id> <recurso> <cant>` a **todos** sus nodos (independientemente de si ya respondieron), y notifica `JOB_DENIED <id>` a Erlang. El `RELEASE` viaja por TCP en orden, por lo que llegará después del `RESERVE` original incluso si hay un `GRANTED` en tránsito.
- **`RELEASE <id> <recurso> <cant>`**: llama `gestor_manejar_release` para liberar localmente `cant` unidades del `recurso` del job `id`.

---

## Módulo: `Agente`

### Variables globales

| Variable | Tipo | Descripción |
|----------|------|-------------|
| `epoll_fd` | `int` | Descriptor del epoll compartido por los 4 hilos |
| `lsock_publico` | `int` | Socket TCP de escucha en IP pública (para otros agentes C) |
| `lsock_local` | `int` | Socket TCP de escucha en 127.0.0.1 (para Erlang) |
| `usock_udp` | `int` | Socket UDP para ANNOUNCE |
| `erlangSocket` | `int` | fd de la conexión activa con Erlang (-1 si no conectado) |
| `timer_anuncios_fd` | `int` | Timer que dispara cada 5 s para emitir ANNOUNCE |
| `mi_ip_publica` | `char[16]` | IP de la interfaz de red activa (detectada al inicio) |
| `estado` | `EstadoGlobal` | Estado global del agente |

### `void* bucle_principal(void *)`
Loop de 4 hilos sobre `epoll_wait`. Despacha según el fd listo:
- `lsock_publico` / `lsock_local` → `aceptar_cliente`
- `usock_udp` → `atender_cliente_udp` + parseo del ANNOUNCE
- `timer_anuncios_fd` → broadcast UDP `ANNOUNCE`
- cualquier otro fd → `atender_cliente_tcp` + `procesar_mensajes_en_buffer`

Cuando un socket se cierra, llama `manejar_desconexion_socket` y libera el `ClienteConectado`.

### `void procesar_mensajes_en_buffer(ClienteConectado *cliente)`
Extrae mensajes completos del buffer (delimitados por `\n`) y los envía a `procesar_mensaje_erlang` o `procesar_mensaje_red_c` según `cliente->es_erlang`. Hace `memmove` para desplazar el remanente al inicio del buffer.

---

## Módulo: `cola`

Cola FIFO genérica sobre `GList`. Usa punteros de función `FuncionCopia` / `FuncionDestructora` para ser agnóstica del tipo de dato.

| Función | Descripción |
|---------|-------------|
| `cola_crear()` | Aloja una cola vacía |
| `cola_destruir(c, destroy)` | Libera todos los nodos y sus datos usando `destroy` |
| `cola_es_vacia(c)` | Retorna 1 si `primero == NULL` |
| `cola_inicio(c, copy)` | Retorna `copy(primero->data)` sin desencolar. Con `no_copia` (retorna el puntero tal cual) permite inspeccionar y modificar el elemento al frente |
| `cola_encolar(c, dato, copy)` | Inserta al final. Crea la cola si es `NULL` |
| `cola_desencolar(c, destroy)` | Elimina el primer elemento llamando `destroy` sobre sus datos |
| `cola_recorrer(c, visit)` | Aplica `visit` a cada elemento en orden |

---

## Casos borde documentados

| Caso | Comportamiento |
|------|---------------|
| `gestor_manejar_reserva` con recurso inválido (`"disco"`, etc.) | Retorna -1 (DENIED) |
| `gestor_manejar_reserva` con `cantidad = 0` | Retorna -1 (DENIED) |
| `gestor_manejar_reserva` con `cantidad > capacidad_total` | Retorna -1 (DENIED) |
| `gestor_manejar_release` con `cantidad > lo asignado` | Libera solo lo que había registrado; `disponible` nunca supera `capacidad_total` |
| `gestor_liberar_job` para un job inexistente | Imprime aviso y retorna sin modificar recursos |
| Doble `RESERVE` del mismo `job_id` sobre el mismo recurso | Acumula en el libro contable; `gestor_liberar_job` libera el total |
| FIFO con job grande al frente de la cola | Jobs posteriores con menor demanda no saltan la fila aunque quepan; esperan a que el job de la cabeza sea atendido |
| `DENIED` llegado mientras otros nodos están en tránsito | Se envía `RELEASE` a todos los nodos registrados en la `PeticionMulti`; los `GRANTED` tardíos son ignorados (TCP garantiza que `RELEASE` llega después de `RESERVE` en el mismo fd) |
| Desconexión abrupta de un socket | `manejar_desconexion_socket` libera todos los recursos de ese socket y redistribuye a la cola de pendientes |
| Nodo que no se anuncia en 15 s | `gestor_desconectar_nodos` lo elimina del registro; futuras llamadas a `GET_NODES` no lo incluirán |

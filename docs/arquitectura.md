# Agente C — Arquitectura

## Visión general

El agente C es un proceso por nodo que cumple tres roles simultáneos:

1. **Servidor de recursos local**: administra CPU, GPU y RAM de su máquina y responde pedidos de
   reserva de otros nodos.
2. **Cliente de recursos remoto**: cuando su Erlang pide un job con recursos en otro nodo, el agente
   conecta a ese nodo, le envía `RESERVE` y rastrea la respuesta.
3. **Puente de protocolo**: traduce entre el protocolo Erlang (`JOB_REQUEST / JOB_RELEASE`) y el
   protocolo C-a-C (`RESERVE / GRANTED / DENIED / RELEASE`).

Corre un **epoll con 4 hilos worker** que comparten el mismo descriptor. Los sockets de escucha y
los timers se registran con `EPOLLEXCLUSIVE` (evita el thundering herd); los clientes conectados con
`EPOLLONESHOT` (a lo sumo un hilo procesa cada cliente a la vez). La escritura es no bloqueante: se
maneja `EPOLLIN` y `EPOLLOUT` sin bloquear ningún hilo.

---

## Módulos

```
main.c                    — main: setup, descubrimiento inicial y lanzamiento de hilos
servidor.c / .h           — reactor: epoll, accept, dispatch, envío/drenado (EPOLLOUT), mapa fd→cliente
controlador.c / .h        — capa de protocolo: parseo de mensajes y handlers (dueño del estado global)
gestor_estado.c / .h      — fachada thread-safe sobre recursos, jobs, nodos y peticiones
modelo/estado.c / .h      — struct EstadoGlobal + estado_crear / estado_destruir
modelo/recursos.c / .h    — RecursoLocal con cola de pendientes
modelo/jobs.c / .h        — TablaJobs: libro contable de recursos asignados
modelo/nodos.c / .h       — TablaNodos: directorio de agentes conocidos + conexiones cacheadas
modelo/peticiones.c / .h  — PeticionMulti: transacción de un JOB_REQUEST multi-nodo
red/sockets.c / .h        — primitivas de red (TCP/UDP/timerfd)
red/cliente.c / .h        — ClienteConectado: fd + buffer de lectura + buffer de salida
estructuras/              — cola, glist, tablahash genéricas
```

**Capas**: `main` (arranque) → `servidor` (transporte/reactor) → `controlador` (protocolo) →
`gestor_estado` (fachada) → `modelo` (dominio). El servidor no conoce el protocolo ni el estado; el
controlador no conoce el epoll (salvo pedir "enviá esto" o "abrí una conexión"). El estado global
(`EstadoGlobal estado`) lo define y lo usa el controlador.

---

## Protocolos de mensajes

### Erlang → Agente (puerto local 127.0.0.1:4040)

| Mensaje | Descripción |
|---------|-------------|
| `GET_NODES` | Pide la tabla de nodos. Respuesta: `NODES IP:puerto:cpu:X:mem:Y:gpu:Z[;...]` |
| `JOB_REQUEST <id> @IP:recurso:cant [...]` | Solicita reservas en uno o varios nodos. Respuesta eventual: `JOB_GRANTED` / `JOB_DENIED` / `JOB_TIMEOUT` |
| `JOB_RELEASE <id>` | El job terminó; liberar todo lo reservado en nodos remotos |

### Agente → Erlang

| Mensaje | Cuándo |
|---------|--------|
| `JOB_GRANTED <id>` | Todos los nodos concedieron |
| `JOB_DENIED <id>` | Un nodo rechazó el pedido (recurso imposible) → Erlang no reintenta |
| `JOB_TIMEOUT <id>` | La petición no se completó en 30 s (congestión/deadlock) → Erlang reintenta con backoff |

### Agente C ↔ Agente C (puerto público IP:4040)

| Mensaje | Dirección | Descripción |
|---------|-----------|-------------|
| `RESERVE <id> <recurso> <cant>` | A → B | Pide que B reserve `cant` de `recurso` para el job `id` |
| `GRANTED <id>` | B → A | Reserva concedida |
| `DENIED <id>` | B → A | Reserva rechazada (recurso desconocido o cantidad > capacidad) |
| `RELEASE <id> <recurso> <cant>` | A → B | Cancela/libera esa reserva (rollback o fin de job) |

> **No hay mensaje de "TIMEOUT" entre agentes.** El timeout se detecta y resuelve del lado del
> coordinador (ver `estrategia_deadlock.md`).

### UDP broadcast (puerto 12529)

| Mensaje | Descripción |
|---------|-------------|
| `ANNOUNCE <puerto> cpu:<n> gpu:<g> mem:<m>` | Autodescubrimiento cada 5 s. La IP la aporta el datagrama. Un nodo sin ANNOUNCE en 15 s se elimina del registro |

---

## Arranque (`main.c`)

1. Ignora `SIGPIPE`, crea el estado global, descubre su IP y se auto-registra en la tabla de nodos.
2. Crea el epoll, los sockets de escucha (TCP público, TCP local, UDP) y los dos timers.
3. **Descubrimiento inicial** (`servidor_descubrimiento_inicial`): emite un `ANNOUNCE` y durante 2 s
   (`TIEMPO_DESCUBRIMIENTO_MS`) escucha por UDP con `poll()` los anuncios de nodos ya activos,
   poblando el registro antes de atender.
4. Registra los sockets/timers en el epoll con `EPOLLEXCLUSIVE`.
5. Lanza `NUM_HILOS` (4) hilos corriendo `servidor_bucle_principal`.

---

## Bucle de eventos (`servidor.c`)

`servidor_bucle_principal` hace `epoll_wait` y despacha según el fd y la máscara de eventos:

- `lsock_publico` / `lsock_local` → `servidor_aceptar_cliente` (accept + registrar en epoll + mapa)
- `usock_udp` → `servidor_gestion_anuncio_recibido` (lee ANNOUNCE, lo procesa)
- `timer_anuncios_fd` → `controlador_anuncio_recursos` (broadcast periódico)
- `timer_timeout` → `controlador_timer` (expira pedidos/peticiones + desconecta nodos viejos)
- cliente conectado → según `eventos[n].events`:
  - `EPOLLHUP/EPOLLERR` → `servidor_desconectar_cliente`
  - `EPOLLOUT` → `servidor_drenar_salida` (manda lo pendiente del buffer de salida)
  - `EPOLLIN` → `servidor_gestion_cliente` (lee y procesa mensajes)

### Escritura no bloqueante (EPOLLOUT)

`enviar_mensaje_tcp(fd, msg)` (en `servidor.c`) intenta `send()` directo; si el kernel no lo acepta
entero (buffer de envío lleno o parcial), guarda el remanente en el `buffer_salida` del cliente y
arma `EPOLLOUT`. Cuando el socket vuelve a tener lugar, `servidor_drenar_salida` manda lo que quedó y
apaga `EPOLLOUT` al vaciar. Un mapa global `fd → ClienteConectado` permite que el envío (que recibe
un fd crudo) encuentre el buffer del cliente. Cada cliente tiene un `lock_salida` porque cualquier
hilo puede escribirle. El re-armado del cliente calcula la máscara
`EPOLLIN | EPOLLONESHOT | (bytes_salida>0 ? EPOLLOUT : 0)` bajo ese lock.

### Ciclo de vida de las conexiones

El `ClienteConectado` es **propiedad del loop de epoll**: lo crea al aceptar/conectar y lo destruye
en el camino de desconexión. El registro de nodos guarda solo una **referencia débil**
(`nodo->conexion`) que nunca cierra ni libera. Al caerse una conexión cacheada solo se pone
`conexion = NULL`; el nodo sigue vivo hasta el timeout de 15 s.

---

## Protocolo (`controlador.c`)

Ambos `procesar_mensaje_*` son dispatchers que parsean el comando y delegan en handlers `static`:

- **`procesar_mensaje_erlang`** → `erlang_get_nodes`, `erlang_job_request`, `erlang_job_release`.
  - `JOB_REQUEST`: por cada token `@IP:res:cant` obtiene el puerto y la conexión cacheada del nodo
    (bajo lock, vía `gestor_obtener_destino`); si no hay, abre una y la cachea
    (`gestor_registrar_conexion`); registra el nodo en la `PeticionMulti` y manda `RESERVE`.
- **`procesar_mensaje_red_c`** → `red_reserve`, `red_granted`, `red_denied`, `red_release`.
  - `GRANTED`: marca el nodo de la petición (`peticion_buscar_nodo_por_fd` devuelve el primer nodo
    pendiente con ese fd) e incrementa `respondidos`; si `respondidos == total`, `JOB_GRANTED`.
  - `DENIED`: rollback (`RELEASE` a todos) + `JOB_DENIED`.

Handlers del loop: `controlador_anuncio_recibido` (parsea y procesa un ANNOUNCE), `controlador_timer`
(expira pedidos y peticiones, desconecta nodos), `controlador_desconexion_cliente` (limpieza de
estado de un socket caído).

---

## Fachada de estado (`gestor_estado.c`)

Todas las funciones toman `estado->lock` internamente (salvo `gestor_buscar_peticion` /
`gestor_eliminar_peticion`, que se llaman con el lock ya tomado).

- `gestor_manejar_reserva` → `1` GRANTED / `0` encolado / `-1` DENIED.
- `gestor_manejar_release` / `gestor_liberar_job` → liberan y reparten la cola FIFO (greedy).
- `gestor_expirar_pedidos` → **lado dueño**: desencola en silencio los `RESERVE` locales vencidos.
- `gestor_expirar_peticiones(cb)` → **lado coordinador**: expira las `PeticionMulti` sin completar y
  por cada una llama `cb` (RELEASE a sus nodos + `JOB_TIMEOUT` a Erlang) y la destruye.
- `gestor_obtener_destino` / `gestor_registrar_conexion` / `gestor_limpiar_conexion_por_fd` →
  acceso al registro de nodos y sus conexiones cacheadas, bajo lock.
- `gestor_procesar_anuncio` / `gestor_desconectar_nodos` / `gestor_get_nodes` → radar de nodos.
- `manejar_desconexion_socket` → al caer un socket, libera todos los recursos de sus jobs y reparte
  la cola.

---

## Deadlock

Estrategia: **timeout + backoff aleatorio** (detalle en `estrategia_deadlock.md`). El agente detecta
el timeout del lado coordinador (expira la `PeticionMulti`, avisa `JOB_TIMEOUT`) y Erlang reintenta.
El lado dueño solo desencola en silencio los `RESERVE` vencidos. No se agrega ningún mensaje de
timeout entre agentes.

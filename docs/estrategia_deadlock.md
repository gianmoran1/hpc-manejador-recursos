# Estrategia contra deadlock distribuido

## Estrategia elegida: detección por timeout con backoff aleatorio

El sistema implementa **detección de deadlock mediante timeout**, combinada con **liberación de
recursos y reintento con espera aleatoria**. No se usa prevención estática (ordenamiento de
recursos), ya que eso requeriría un orden canónico acordado entre equipos de otros grupos.

### Componentes de la estrategia

**Detección (agente C):** el agente usa `timerfd` (dispara cada 15 s, en `controlador_timer`) y
aplica el timeout en **dos lugares distintos**:

- **Lado dueño del recurso** (`gestor_expirar_pedidos`): si un `RESERVE` que otro agente encoló en
  este nodo lleva más de `TIEMPO_ESPERA_RESERVA` (30 s) sin resolverse, se lo **desencola en
  silencio**. Es higiene de cola: libera el recurso para otros pedidos. **No notifica a nadie.**

- **Lado coordinador** (`gestor_expirar_peticiones`): si una `PeticionMulti` —un job que este agente
  coordina y para el que mandó RESERVE a otros nodos— lleva más de 30 s sin completarse
  (`respondidos < total`), el agente hace **rollback** (`RELEASE` a todos sus nodos) y notifica a su
  planificador Erlang con **`JOB_TIMEOUT <job_id>`**.

Punto clave de diseño: **no se agrega ningún mensaje de timeout entre agentes**. El coordinador
detecta el vencimiento por sí mismo mirando el timestamp de su `PeticionMulti`. El protocolo C-a-C
sigue siendo solo `RESERVE / GRANTED / DENIED / RELEASE`. `DENIED` queda reservado para el rechazo
genuino de un nodo (pedido imposible: recurso desconocido o cantidad > capacidad total).

**Reacción (planificador Erlang):** al recibir `JOB_TIMEOUT`, el worker del job (`jobWorker.erl`)
ejecuta:

1. Registra el evento en el log: `[TIMEOUT] JobId:<id>`.
2. Espera una cantidad aleatoria de tiempo (`rand:uniform(?MAX_RANDOM_ESPERA)`).
3. Reintenta el job desde cero: genera nuevas cantidades y manda un nuevo `JOB_REQUEST`.

La espera aleatoria es el mecanismo central: rompe la simetría entre dos jobs que compiten por los
mismos recursos, haciendo estadísticamente improbable que ambos reintentos colisionen de nuevo.

> **`JOB_DENIED` vs `JOB_TIMEOUT`** (contrato con Erlang, ver `jobWorker.erl`): `JOB_DENIED` = fallo
> permanente, el worker **no** reintenta (un pedido imposible no mejora reintentando). `JOB_TIMEOUT`
> = congestión/deadlock, el worker **sí** reintenta con backoff. Por eso el timeout del coordinador
> se manda como `JOB_TIMEOUT`, no como `JOB_DENIED`.

### Justificación

| Alternativa | Motivo de descarte |
|---|---|
| Ordenamiento global de recursos (prevención) | Requiere un orden canónico de IPs/recursos acordado entre todos los equipos. |
| Detección por grafo de espera | Requiere estado global distribuido difícil de mantener sin coordinador central. |
| Wound-Wait / Wait-Die | Requieren timestamps globales de inicio de cada job, difíciles de sincronizar entre nodos independientes. |
| **Timeout + backoff aleatorio** | Simple, sin estado adicional, localizable en cada agente/worker. La aleatoriedad rompe la simetría sin coordinación global. |

El modelo actor de Erlang favorece esta estrategia: cada job es un proceso independiente que puede
dormirse y reintentarse sin bloquear al scheduler ni a otros workers.

---

## Ejemplo paso a paso con dos nodos

### Configuración

| Nodo | CPU | MEM | GPU |
|------|-----|-----|-----|
| A (192.168.1.10) | 2 | 8 GB | 0 |
| B (192.168.1.11) | 2 | 4 GB | 1 |

- **Job 1001** (Erlang A): solicita `cpu:2` del Nodo A y `gpu:1` del Nodo B.
- **Job 2001** (Erlang B): solicita `gpu:1` del Nodo B y `cpu:2` del Nodo A.

### Fase 1 — Solicitudes simultáneas

```
Agente A → Nodo A:  RESERVE 1001 cpu 2  →  GRANTED   (cpu de A queda en 0)
Agente B → Nodo B:  RESERVE 2001 gpu 1  →  GRANTED   (gpu de B queda en 0)
```

### Fase 2 — Deadlock

```
Agente A → Nodo B:  RESERVE 1001 gpu 1  →  ENCOLADO  (gpu ocupada por Job 2001)
Agente B → Nodo A:  RESERVE 2001 cpu 2  →  ENCOLADO  (cpu ocupada por Job 1001)

Job 1001: tiene cpu@A, espera gpu@B  ←→  Job 2001: tiene gpu@B, espera cpu@A
```

### Fase 3 — Detección por timeout (lado coordinador)

Pasados 30 s ninguna de las dos `PeticionMulti` se completó, así que expiran:

```
Agente A: expira PeticionMulti 1001 → RELEASE a todos sus nodos → JOB_TIMEOUT 1001 → Erlang A
Agente B: expira PeticionMulti 2001 → RELEASE a todos sus nodos → JOB_TIMEOUT 2001 → Erlang B
```

En paralelo, cada nodo desencola en silencio el `RESERVE` ajeno que tenía vencido. Los recursos
quedan libres:

```
Nodo A: cpu vuelve a 2   |   Nodo B: gpu vuelve a 1
```

### Fase 4 — Reacción Erlang (reintento con backoff)

Los workers reciben `{timeout, JobId}` (el scheduler mapea `JOB_TIMEOUT` → `{timeout, JobId}`):

```erlang
% jobWorker.erl — ejecutarTrabajoSimulado/4
{timeout, JobId} ->
    loggerScheduler:log(io_lib:format("[TIMEOUT] JobId:~w", [JobId])),
    timer:sleep(rand:uniform(?MAX_RANDOM_ESPERA)),
    ejecutarTrabajoSimulado(Socket, JobId, PidScheduler, RecursosTotales)  % reintenta
```

Cada worker espera un tiempo aleatorio distinto y reintenta. Con alta probabilidad uno reintenta
antes, adquiere todos sus recursos y libera el camino para el otro → el deadlock se rompe.

### Log generado

```
[TIMEOUT] JobId:2001
[TIMEOUT] JobId:1001
[GRANTED] JobId:2001   (reintento exitoso)
[GRANTED] JobId:1001   (reintento exitoso)
```

---

## Propiedades de la estrategia

**Liveness:** cuando dos jobs están en deadlock, el timeout del coordinador los expira, libera sus
recursos y avisa `JOB_TIMEOUT`. Los workers reintentan con backoff aleatorio, rompiendo la simetría
con alta probabilidad. El riesgo teórico es livelock (reintentos que vuelven a colisionar), mitigado
por la aleatoriedad de la espera.

**Safety:** no hay doble asignación. El coordinador manda `RELEASE` a todos sus nodos antes de
avisar `JOB_TIMEOUT`, y los nodos dueños desencolan/liberan sus pedidos vencidos. Un `GRANTED` tardío
que llegue después de expirar la petición se ignora (la petición ya no está en la tabla).

**Sin coordinación global:** cada agente y cada worker actúan de forma independiente. No hay mensajes
de timeout entre agentes ni estado compartido fuera del agente C local.

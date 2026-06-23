# Estrategia contra deadlock distribuido

## Estrategia elegida: Detección por timeout con backoff aleatorio

El sistema implementa una estrategia de **detección de deadlock mediante timeout**, combinada con
**liberación de recursos y reintento con espera aleatoria**. No se utiliza prevención estática
(como ordenamiento de recursos), ya que esa responsabilidad recae en el agente C, que gestiona
el acceso real a los recursos de cada nodo.

### Componentes de la estrategia

**Detección (agente C):**  
El agente C usa `timerfd` (dispara cada 15 s) para supervisar cada reserva pendiente. Si una reserva
lleva más de 30 s encolada sin resolverse, la descarta y envía `DENIED <job_id>` al agente coordinador
que originó el `RESERVE`. Ese coordinador ejecuta rollback (envía `RELEASE` a todos sus nodos) y
notifica al planificador Erlang con `JOB_DENIED <job_id>`.

**Reacción (planificador Erlang):**  
Al recibir `JOB_TIMEOUT` (originado por el timeout), el worker del job afectado ejecuta tres pasos:

1. Registra el evento en el log: `[TIMEOUT] JobId:<id>`
2. Espera una cantidad aleatoria de tiempo entre 1 y 3000 ms (`rand:uniform(3000)`)
3. Reintenta el job desde cero: genera nuevas cantidades de recursos y envía un nuevo
   `JOB_REQUEST` al agente C

La espera aleatoria es el mecanismo central: rompe la simetría entre dos jobs que compiten
por los mismos recursos, haciendo estadísticamente improbable que ambos reintentos colisionen
de nuevo.

### Justificación

| Alternativa | Motivo de descarte |
|---|---|
| Ordenamiento global de recursos (prevención) | Requiere coordinación entre equipos sobre un orden canónico de IPs/recursos. La responsabilidad fue asignada al agente C. |
| Detección por grafo de espera | Requiere estado global distribuido difícil de mantener en un sistema actor sin coordinador central. |
| Wound-Wait / Wait-Die | Requieren conocer timestamps globales de inicio de cada job; difícil de sincronizar entre nodos Erlang independientes. |
| **Timeout + backoff aleatorio** | Simple, sin estado adicional, localizable en cada worker. La aleatoriedad rompe la simetría con alta probabilidad sin coordinación global. |

El modelo actor de Erlang favorece esta estrategia: cada job es un proceso independiente que
puede dormirse y reintentarse sin bloquear al scheduler ni a otros workers.

---

## Ejemplo paso a paso con dos nodos

### Configuración

| Nodo | CPU | MEM | GPU |
|------|-----|-----|-----|
| A (192.168.1.10) | 2 | 8 GB | 0 |
| B (192.168.1.11) | 2 | 4 GB | 1 |

Dos planificadores Erlang independientes generan simultáneamente:

- **Job 1001** (Erlang A): solicita `cpu:2` del Nodo A y `gpu:1` del Nodo B
- **Job 2001** (Erlang B): solicita `gpu:1` del Nodo B y `cpu:2` del Nodo A

### Fase 1 — Solicitudes simultáneas (deadlock latente)

```
Erlang A  →  Agente A:  JOB_REQUEST 1001 @192.168.1.10:cpu:2 @192.168.1.11:gpu:1
Erlang B  →  Agente B:  JOB_REQUEST 2001 @192.168.1.11:gpu:1 @192.168.1.10:cpu:2
```

Ambos agentes comienzan a reservar recursos en paralelo:

```
Agente A  →  Nodo A:    RESERVE 1001 cpu 2     →  GRANTED   (cpu de A queda en 0)
Agente B  →  Nodo B:    RESERVE 2001 gpu 1     →  GRANTED   (gpu de B queda en 0)
```

### Fase 2 — Deadlock

Cada agente intenta adquirir el recurso que el otro ya tomó:

```
Agente A  →  Nodo B:    RESERVE 1001 gpu 1     →  ENCOLADO  (gpu ocupada por Job 2001)
Agente B  →  Nodo A:    RESERVE 2001 cpu 2     →  ENCOLADO  (cpu ocupada por Job 1001)
```

Ambos agentes quedan bloqueados esperando al otro. El sistema está en **deadlock**.

```
Job 1001: tiene cpu@A, espera gpu@B  ←→  Job 2001: tiene gpu@B, espera cpu@A
```

### Fase 3 — Detección por timeout (agente C)

Después del plazo configurado, los `timerfd` de ambos agentes expiran:

```
Nodo B:  timeout de Job 1001 → DENIED 1001 → Agente A → RELEASE a todos sus nodos → JOB_DENIED 1001 → Erlang A
Nodo A:  timeout de Job 2001 → DENIED 2001 → Agente B → RELEASE a todos sus nodos → JOB_DENIED 2001 → Erlang B
```

Los recursos quedan libres:

```
Nodo A: cpu vuelve a 2   |   Nodo B: gpu vuelve a 1
```

### Fase 4 — Reacción Erlang

Los workers de ambos jobs reciben `{denied, JobId}` del scheduler (el scheduler mapea `JOB_DENIED` → `{denied, JobId}`):

```erlang
% En jobWorker.erl — ejecutarTrabajoSimulado/4
{denied, JobId} ->
    loggerScheduler:log(io_lib:format("[DENIED] JobId:~w", [JobId]));
```

El job finaliza. El scheduler recibe `{jobTerminado, JobId}` y libera el slot en el mapa de jobs activos.

> **Nota:** `tcpClient.erl` también parsea `JOB_TIMEOUT` → `{timeout, JobId}`, que activa un handler con backoff aleatorio y reintento. Sin embargo, con la implementación actual del agente C el timeout se encamina como `DENIED` al coordinador y llega a Erlang como `JOB_DENIED`, por lo que el path `{timeout, JobId}` no se activa.

### Log generado

```
[DENIED]  JobId:2001
[DENIED]  JobId:1001
```

---

## Propiedades de la estrategia

**Liveness:** cuando dos jobs están en deadlock, el timeout los deniega y libera sus recursos. Los slots en el scheduler se liberan y nuevos jobs pueden tomar los recursos disponibles. El backoff aleatorio con reintento (`{timeout, JobId}`) está implementado en Erlang pero requiere que el agente C envíe `JOB_TIMEOUT` directamente; con el flujo actual (`DENIED` → `JOB_DENIED`) los jobs denegados por timeout no se reintentan.

**Safety:** no hay doble asignación porque el agente C libera completamente los recursos antes de enviar `DENIED` al coordinador. El coordinador hace rollback de los recursos ya grantados antes de notificar `JOB_DENIED` a Erlang.

**Sin coordinación global:** cada worker actúa de forma totalmente independiente. No hay
mensajes adicionales entre nodos Erlang ni estado compartido fuera del agente C local.

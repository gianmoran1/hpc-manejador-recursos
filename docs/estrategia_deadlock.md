# Estrategia contra deadlock distribuido

## Estrategia elegida: Detección por timeout con backoff aleatorio

El sistema implementa una estrategia de **detección de deadlock mediante timeout**, combinada con
**liberación de recursos y reintento con espera aleatoria**. No se utiliza prevención estática
(como ordenamiento de recursos), ya que esa responsabilidad recae en el agente C, que gestiona
el acceso real a los recursos de cada nodo.

### Componentes de la estrategia

**Detección (agente C):**  
El agente C usa `timerfd` para supervisar cada reserva pendiente. Si una reserva no se completa
dentro del plazo configurado, el agente libera todos los recursos parcialmente adquiridos para
ese job y notifica al planificador Erlang con `JOB_TIMEOUT <job_id>`.

**Reacción (planificador Erlang):**  
Al recibir el timeout, el worker del job afectado ejecuta tres pasos:

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
Agente A:  timeout de Job 1001 → libera RESERVE 1001 cpu 2 en Nodo A → JOB_TIMEOUT 1001 → Erlang A
Agente B:  timeout de Job 2001 → libera RESERVE 2001 gpu 1 en Nodo B → JOB_TIMEOUT 2001 → Erlang B
```

Los recursos quedan libres:

```
Nodo A: cpu vuelve a 2   |   Nodo B: gpu vuelve a 1
```

### Fase 4 — Reacción Erlang: backoff aleatorio

Los workers de ambos jobs reciben `{timeout, JobId}` del scheduler:

```erlang
% En jobWorker.erl — ejecutarTrabajoSimulado/4
{timeout, JobId} ->
    loggerScheduler:log(io_lib:format("[TIMEOUT] JobId:~w", [JobId])),
    timer:sleep(rand:uniform(3000)),          % espera aleatoria
    ejecutarTrabajoSimulado(Socket, JobId, PidScheduler, RecursosTotales)
```

Supongamos que el sorteo produce:

```
Worker Job 1001:  espera 2314 ms
Worker Job 2001:  espera  587 ms
```

### Fase 5 — Reintento desincronizado

Job 2001 despierta primero (587 ms antes) y reintenta:

```
Erlang B  →  Agente B:  JOB_REQUEST 2001 @192.168.1.11:gpu:1 @192.168.1.10:cpu:2

Agente B  →  Nodo B:    RESERVE 2001 gpu 1   →  GRANTED
Agente B  →  Nodo A:    RESERVE 2001 cpu 2   →  GRANTED   ← recursos libres, sin espera
Agente B  →  Erlang B:  JOB_GRANTED 2001
```

Job 2001 se ejecuta (simula carga), luego libera:

```
Erlang B  →  Agente B:  JOB_RELEASE 2001
Nodo A: cpu vuelve a 2   |   Nodo B: gpu vuelve a 1
```

### Fase 6 — Job 1001 reintenta con recursos disponibles

Job 1001 despierta (1727 ms después de Job 2001):

```
Erlang A  →  Agente A:  JOB_REQUEST 1001 @192.168.1.10:cpu:2 @192.168.1.11:gpu:1

Agente A  →  Nodo A:    RESERVE 1001 cpu 2   →  GRANTED
Agente A  →  Nodo B:    RESERVE 1001 gpu 1   →  GRANTED
Agente A  →  Erlang A:  JOB_GRANTED 1001
```

Job 1001 se ejecuta y libera. **Deadlock resuelto.**

### Log generado

```
[TIMEOUT]  JobId:2001
[TIMEOUT]  JobId:1001
[GRANTED]  JobId:2001
[GRANTED]  JobId:1001
```

---

## Propiedades de la estrategia

**Liveness:** en cada ciclo de timeout, al menos un job progresa con probabilidad 1 − (1/3000)
(la probabilidad de que dos backoffs aleatorios en [1, 3000] ms coincidan exactamente).
En la práctica, con más de dos jobs y ventanas de milisegundos, la probabilidad de colisión
repetida decae exponencialmente con cada reintento.

**Safety:** no hay doble asignación porque el agente C libera completamente los recursos
antes de enviar `JOB_TIMEOUT`. El reintento comienza desde cero sobre recursos limpios.

**Sin coordinación global:** cada worker actúa de forma totalmente independiente. No hay
mensajes adicionales entre nodos Erlang ni estado compartido fuera del agente C local.

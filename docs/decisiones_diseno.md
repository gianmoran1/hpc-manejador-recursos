# Decisiones de diseño — Scheduler Erlang

---

## 1. Un proceso Erlang por job

Cada job es un proceso independiente (`spawn_monitor(jobWorker, iniciarJobWorker, ...)`).

**Por qué:** el scheduler necesita atender mensajes de múltiples fuentes en paralelo
(respuestas del agente C, nuevos jobs, actualizaciones de nodos). Si un job esperara su
`granted/denied/timeout` dentro del propio scheduler, bloquearía a todos los demás.
Con un proceso por job, el `receive` bloqueante vive en el worker y el scheduler nunca
se bloquea.

**Alternativa descartada:** manejar todos los jobs dentro del scheduler con un mapa
de estado y timeouts manuales. Más complejo, más frágil, contrario al modelo actor.

---

## 2. Scheduler como coordinador: workers piden cantidades, scheduler asigna nodos

Los workers solicitan recursos abstractos `{CantCpu, CantMem, CantGpu}`. El scheduler
traduce eso a asignaciones concretas `[{IP, "cpu", N}, {IP, "gpu", M}]` antes de
enviárselas al agente C.

**Por qué:** los workers no conocen la topología del cluster (qué nodos existen, cuánto
tienen). Eso es responsabilidad del scheduler, que mantiene `ListaNodos` actualizada.
Separar "cuánto necesito" de "a quién pedirlo" hace que cada entidad tenga una sola
responsabilidad.

**Consecuencia directa:** si cambia la lista de nodos (el actualizador recibe nuevos
`GET_NODES`), los workers en vuelo no se ven afectados — cada uno ya mandó su solicitud
con nodos concretos.

---

## 3. Algoritmo voraz para asignación de recursos (`manejoRecursos:asignarVoraz`)

Cuando un job pide 7 CPUs y ningún nodo tiene 7, pero dos nodos tienen 4 y 5, el
algoritmo distribuye la demanda entre ambos: `@nodoA:cpu:4 @nodoB:cpu:3`.

**Por qué:** una búsqueda simple "busco un nodo con capacidad suficiente" descartaría
este job incluso cuando el cluster tiene recursos totales suficientes. El voraz recorre
los nodos en orden aleatorio, tomando de cada uno lo que puede hasta cubrir la demanda.

**Propiedad importante:** si no puede cubrir la demanda completa de un recurso, devuelve
`insuficiente` y `asignarNodos` retorna `[]` — el scheduler deniega el job localmente
sin siquiera consultar al agente C.

---

## 4. `spawn_monitor` para workers, `spawn_link` para servicios internos

- **Workers** (`iniciarJobWorker`): `spawn_monitor`. Si un worker crashea, el scheduler
  recibe `{'DOWN', ..., Razon}`, limpia el mapa y sigue vivo. El scheduler no debe morir
  porque un job falló.

- **Job generator y node updater**: `spawn_link`. Son servicios de soporte del scheduler.
  Si el scheduler muere, ellos también deben morir (no tiene sentido generar jobs sin
  scheduler). El link hace esto automático.

**Regla general:** link cuando el proceso hijo no tiene sentido sin el padre. Monitor
cuando el padre debe sobrevivir al hijo.

---

## 5. Estrategia anti-deadlock: timeout + backoff aleatorio

El agente C detecta el deadlock mediante `timerfd` (dispara cada 15 s) en dos planos: el **dueño**
del recurso desencola en silencio los `RESERVE` que llevan más de 30 s encolados (higiene de cola),
y el **coordinador** expira su `PeticionMulti` si el job no se completó en 30 s, hace rollback
(`RELEASE` a todos sus nodos) y avisa `JOB_TIMEOUT` a Erlang. No hay mensaje de timeout entre
agentes: el coordinador lo detecta por sí mismo.

El worker Erlang reacciona según el tipo de mensaje:

- `{denied, JobId}`: loguea `[DENIED]` y **finaliza el job sin reintentar** (rechazo permanente: el
  nodo no puede dar ese recurso ni reintentando).
- `{timeout, JobId}`: loguea `[TIMEOUT]`, espera `rand:uniform(?MAX_RANDOM_ESPERA)` ms y **reintenta**.
  Es el path que rompe el deadlock, alimentado por el `JOB_TIMEOUT` del coordinador.

**Por qué el backoff sería aleatorio:** si dos jobs en deadlock esperaran el mismo tiempo
fijo, volverían a colisionar. La aleatoriedad rompe la simetría — con alta probabilidad
uno reintenta antes, adquiere todos sus recursos y libera el camino para el otro.

**Por qué no Resource Ordering:** requiere un orden global acordado entre todos los nodos
del cluster, incluyendo equipos de otros grupos. Se delegó al agente C, que es el que
tiene visión completa de las reservas distribuidas.

---

## 6. Reconexión con backoff exponencial en `main`

Ante una desconexión (`{cerrarSistema, Razon}`), `main` no termina: reintenta la conexión
esperando `Delay * 2` ms entre intentos, con techo de 30 segundos.

**Secuencia de cierre limpio antes de reconectar:**
1. Scheduler recibe `{errorRecvAgente, Razon}` del oyente TCP
2. Mata todos los workers con `exit(Pid, shutdown)` — evita que queden bloqueados en
   `receive` esperando mensajes que ya no llegarán
3. Notifica a main con `{cerrarSistema, Razon}`
4. Se mata con razón no-normal (`exit(self(), cerrarSistema)`) — lo cual arrastra al job
   generator y node updater por el `spawn_link`
5. Main cierra el socket y reintenta

**Por qué exponencial y no fijo:** un intervalo fijo bajo genera reconexiones agresivas
si el agente C tarda en reiniciarse. El techo de 30s evita que el backoff crezca
indefinidamente.

---

## 7. Logger como proceso registrado (`schedulerLogger`)

Un único proceso abre el archivo de log y atiende mensajes `{log, Linea}` en serie.
Todos los workers llaman a `loggerScheduler:log(...)` que envía a ese proceso.

**Por qué:** múltiples workers escribiendo directamente al mismo archivo desde distintos
procesos produciría intercalado de texto. El proceso logger serializa los accesos sin
necesidad de locks explícitos — es el modelo actor aplicado a I/O.

**Por qué nombre registrado y no PID:** pasar el PID del logger como parámetro a cada
worker y función anida el logger en todas las firmas. Con `register/2` cualquier proceso
puede loguear con `schedulerLogger ! {log, ...}` sin coordinación extra.

---

## 8. Actualización periódica de nodos cada 15 segundos

Un proceso separado (`loopActualizadorNodos`) envía `GET_NODES` al agente C cada 15
segundos, independientemente de la actividad del scheduler.

**Por qué 15 segundos:** el enunciado indica que un nodo se considera caído si no anuncia
presencia durante 15 segundos. Refrescar con ese mismo intervalo garantiza que el
scheduler nunca trabaje con una lista más desactualizada que un ciclo de anuncio UDP.

**Por qué proceso separado y no `send_after`:** `send_after` requiere que el scheduler
lo reprograme en cada ciclo. Un proceso dedicado es autónomo — el scheduler no necesita
recordar reprogramarlo, y si el scheduler está ocupado procesando mensajes, el timer no
se atrasa.

---

## 9. Tope de jobs simultáneos (`?MAX_JOBS`)

El scheduler ignora `generarTrabajo` si `map_size(JobsEnCurso) >= ?MAX_JOBS`.

**Por qué:** sin tope, el job generator acumularía decenas de workers simultáneos, cada
uno pidiendo recursos. Con clusters pequeños (2-3 nodos), todos quedan denegados o en
espera y el sistema se satura de mensajes sin progresar.

**Dónde está definido:** en `config.hrl`, incluido tanto por `scheduler.erl` (para el
guard) como referenciado indirectamente a través del valor `RecursosPorJob` que el
scheduler calcula y pasa al worker. Único punto de cambio para ajustar la carga.

---

## 10. Representación interna de recursos como tupla posicional `{Cpu, Mem, Gpu}`

En todo el código interno se usa `{Cpu, Mem, Gpu}` y no mapas ni listas de propiedades.

**Por qué:** el orden es fijo y conocido por todos los módulos. El acceso es por posición
(pattern matching directo), sin overhead de lookup. Los nombres `cpu`, `mem`, `gpu` solo
aparecen en los bordes del sistema: al parsear la respuesta de `GET_NODES` y al construir
el `JOB_REQUEST` para el agente C.

**Consecuencia:** agregar un cuarto tipo de recurso requiere cambiar la tupla a 4 elementos
en todos los módulos. Es un trade-off consciente: simplicidad sobre extensibilidad, dado
que el enunciado define exactamente tres tipos de recursos.

---

## 11. `{packet, line}` para el framing TCP

La conexión con el agente C usa `{packet, line}` en lugar de `{packet, 0}` (raw).

**Por qué:** con modo raw, si el agente C envía dos respuestas consecutivas rápidamente
(`JOB_GRANTED 1\nJOB_GRANTED 2\n`), Erlang puede recibirlas en un solo `recv` como un
bloque concatenado. Con `{packet, line}`, el kernel garantiza que cada `recv` retorna
exactamente una línea completa, sin importar cómo llegaron los bytes por TCP. El parser
de `procesarMensaje` puede asumir siempre un comando por llamada.

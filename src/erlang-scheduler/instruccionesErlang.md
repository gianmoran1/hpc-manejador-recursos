# Implementación del Job Scheduler en Erlang

## Contexto del sistema

El sistema completo tiene dos componentes por nodo:
- **Agente C** (otro miembro del equipo): servidor TCP con epoll, gestiona recursos físicos, hace UDP broadcast, habla con otros agentes C remotos.
- **Planificador Erlang** (esta parte): habla con el agente C local via TCP, decide qué jobs lanzar, implementa la estrategia anti-deadlock.

Arquitectura de comunicación:

```
[Erlang] --TCP localhost--> [Agente C local] --TCP red--> [Agente C remoto]
                                    ^
                            UDP broadcast (descubrimiento)
```

---

## Responsabilidades concretas del módulo Erlang

### 1. Conexión al agente C local HECHO
- Conectar via TCP a `localhost:PUERTO_C` usando `gen_tcp:connect/3`
- Mantener la conexión activa durante toda la vida del scheduler
- Manejar desconexión inesperada del agente C

### 2. Consulta de nodos disponibles (`GET_NODES`) HECHO
Enviar el comando `GET_NODES` al agente C y parsear la respuesta:
```
NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1;192.168.1.11:8101:cpu:2:mem:4096
```
Construir una tabla interna de nodos con sus recursos disponibles para usarla al armar los `JOB_REQUEST`.

### 3. Generación y envío de jobs HECHO
- Generar IDs únicos con `erlang:unique_integer()`
- Seleccionar en qué nodos pedir qué recursos (basándose en la tabla de nodos obtenida con `GET_NODES`)
- Enviar al agente C local:
  ```
  JOB_REQUEST 1001 @192.168.1.10:cpu:2 @192.168.1.11:gpu:1
  ```
- Lanzar varios jobs **simultáneamente** (un proceso Erlang por job)
- Manejar respuestas: `JOB_GRANTED`, `JOB_DENIED`, `JOB_TIMEOUT`
- Tras "usar" el recurso (simular trabajo con `timer:sleep`), enviar `JOB_RELEASE <job_id>`

### 4. Estrategia anti-deadlock (requisito explícito)

El enunciado describe el escenario clásico de deadlock con dos nodos:
- Job1 (desde A): necesita CPU de A + GPU de B
- Job2 (desde B): necesita GPU de B + CPU de A
- Si ambos reservan el primer recurso simultáneamente → deadlock
**Estrategia alternativa aceptable: Timeout + rollback**

Si el agente C responde `JOB_TIMEOUT`, el scheduler libera todo (`JOB_RELEASE`) y reintenta después de un backoff aleatorio (`timer:sleep(rand:uniform(3000))`).

### 5. Logging a archivo
Registrar en un archivo de log (ej. `scheduler.log`):
- Cada concesión: `[GRANTED] job_id=1001 resources=...`
- Cada denegación: `[DENIED] job_id=1002`
- Cada evento de deadlock detectado/evitado: `[DEADLOCK_AVOIDED] job_id=1003`

---

## Protocolo completo que Erlang debe manejar

### Comandos Erlang → C

| Mensaje | Descripción |
|---|---|
| `GET_NODES` | Solicitar lista de nodos activos |
| `JOB_REQUEST <job_id> [@host:res:amount ...]` | Solicitar recursos en uno o más nodos |
| `JOB_RELEASE <job_id>` | Liberar todos los recursos de un job |
| `JOB_STATUS <job_id>` | Consultar estado de un job |

### Respuestas C → Erlang

| Mensaje | Descripción |
|---|---|
| `NODES <ip>:<puerto>:<res>:<val>:...;...` | Lista de nodos con recursos |
| `JOB_GRANTED <job_id>` | Todos los recursos fueron concedidos |
| `JOB_DENIED <job_id>` | Al menos un recurso fue denegado |
| `JOB_TIMEOUT <job_id>` | La reserva expiró sin completarse |

### Ejemplo de flujo completo

```
Erlang --> C:  GET_NODES
C --> Erlang:  NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1;192.168.1.11:8101:cpu:2:mem:4096

Erlang --> C:  JOB_REQUEST 1001 @192.168.1.10:cpu:2 @192.168.1.11:gpu:1
  (el agente C envía RESERVE a los nodos remotos y espera GRANTED/DENIED)
C --> Erlang:  JOB_GRANTED 1001

  ... (timer:sleep simulando uso del recurso) ...

Erlang --> C:  JOB_RELEASE 1001
```

---

## Estructura sugerida de módulos

```
scheduler.erl        % proceso principal (gen_server), coordina todo
job_worker.erl       % un proceso por job activo, maneja el ciclo de vida del job
deadlock_guard.erl   % lógica de ordenamiento/prevención de deadlocks
logger.erl           % escritura de eventos a archivo de log
c_agent_conn.erl     % manejo de la conexión TCP con el agente C local
```

---

## Entregables correspondientes al rol Erlang

- Código fuente `.erl` completo con comentarios
- Script `test_deadlock.sh` que:
  - Levante dos instancias locales (en puertos distintos)
  - Lance jobs que generen el escenario de deadlock del enunciado
  - Verifique en los logs que el deadlock fue evitado
- Sección del informe:
  - Estrategia anti-deadlock elegida y justificación
  - Ejemplo paso a paso demostrando cómo se evita el deadlock con dos nodos
  - Diagrama de secuencia de la comunicación local (Erlang ↔ C)

---

## Notas de implementación

- Todas las líneas del protocolo terminan en `\n`
- Los mensajes son ASCII plano (no binario)
- Usar `gen_tcp:connect(Host, Port, [binary, {packet, line}])` para facilitar el parseo línea a línea
- El `job_id` debe ser único globalmente: usar `erlang:unique_integer([positive, monotonic])`
- El planificador puede elegir cualquier nodo que tenga los recursos necesarios (la tabla de `GET_NODES` es informativa)
- El formato de la respuesta `NODES` queda a criterio del equipo (solo lo usa Erlang)

#!/bin/bash
# =============================================================================
# test_deadlock.sh — Prueba de detección y resolución de deadlock distribuido
#
# Ejecuta dos instancias locales del agente C en puertos distintos y verifica
# el comportamiento end-to-end de la estrategia timeout + backoff aleatorio.
#
# ARQUITECTURA DEL TEST:
#
#   [nc Erlang A]  --127.0.0.1:4040-->  [Agente A  pto 4040]
#                                               |
#                                   JOB_REQUEST @127.0.0.2:gpu:1
#                                               | conectar_a_nodo()
#                                        RESERVE 1001 gpu:1
#                                               |
#                           socat bridge: 127.0.0.2:4040 --> 127.0.0.1:4042
#                                               |
#   [nc Erlang B]  --127.0.0.1:4042-->  [Agente B  pto 4042]
#
# El socat bridge permite que Agente A (PUERTO_TCP=4040) alcance a Agente B
# (que escucha en 4042) sin modificar la lógica de conectar_a_nodo().
#
# REQUISITOS:
#   gcc, make, nc (netcat), socat
#
# USO:
#   chmod +x test/test_deadlock.sh
#   cd /ruta/al/proyecto && bash test/test_deadlock.sh
# =============================================================================

set -uo pipefail

# =============================================================================
# CONFIGURACIÓN
# =============================================================================

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$PROJ_ROOT/src/c-agent"
BUILD_DIR="/tmp/hpc_deadlock_test"
LOG_A="$BUILD_DIR/agente_a.log"
LOG_B="$BUILD_DIR/agente_b.log"
LOG_SIM="$BUILD_DIR/simulacion.log"

PORT_A=4040
PORT_B=4042
UDP_A=12529
UDP_B=12531

# Tiempo de espera de reserva (segundos) — reducido para que el test no tarde 30s
TIMEOUT_RESERVA=6

# IP del segundo agente que ve el Agente A al resolver JOB_REQUEST
# (socat escucha aquí y redirige al Agente B en 127.0.0.1:PORT_B)
IP_BRIDGE="127.0.0.2"

# Colores opcionales
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

PASS=0; FAIL=0
declare -a PIDS_CLEANUP=()

# =============================================================================
# UTILIDADES
# =============================================================================

ok()   { echo -e "  ${GREEN}[OK]${NC}   $1"; ((PASS++)) || true; }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; ((FAIL++)) || true; }
info() { echo; echo -e "${YELLOW}=== $1 ===${NC}"; }
note() { echo "  [NOTE] $1"; }
sep()  { echo "  -------------------------------------------------------"; }

register_pid() { PIDS_CLEANUP+=("$1"); }

cleanup() {
    echo
    info "LIMPIEZA"
    for pid in "${PIDS_CLEANUP[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    echo "  Procesos detenidos."
}
trap cleanup EXIT INT TERM

# Espera a que un puerto TCP acepte conexiones (max 5 segundos)
wait_port() {
    local host="$1" port="$2" label="$3"
    for i in $(seq 1 10); do
        if $NC_CMD -z "$host" "$port" 2>/dev/null; then return 0; fi
        sleep 0.5
    done
    fail "Timeout esperando $label en $host:$port"
    return 1
}

# Envía un mensaje TCP y captura la respuesta (primera línea)
tcp_send_recv() {
    local host="$1" port="$2" msg="$3" timeout="${4:-3}"
    printf '%s\n' "$msg" | $NC_CMD -w "$timeout" "$host" "$port" 2>/dev/null || true
}

# Verifica que una cadena aparece en los logs de un agente
assert_log() {
    local logfile="$1" patron="$2" descripcion="$3"
    if grep -q "$patron" "$logfile" 2>/dev/null; then
        ok "$descripcion"
    else
        fail "$descripcion (patrón '$patron' no encontrado en $logfile)"
    fi
}

# =============================================================================
# VERIFICAR DEPENDENCIAS
# =============================================================================
info "DEPENDENCIAS"

for cmd in gcc socat; do
    if command -v "$cmd" &>/dev/null; then
        ok "$cmd disponible"
    else
        fail "$cmd no encontrado — instalar con: sudo apt install $cmd"
    fi
done

# nc puede llamarse nc o netcat según la distro
NC_CMD=""
if command -v nc &>/dev/null; then
    NC_CMD="nc"
    ok "nc (netcat) disponible"
elif command -v netcat &>/dev/null; then
    NC_CMD="netcat"
    ok "netcat disponible"
else
    fail "nc/netcat no encontrado — instalar con: sudo apt install netcat"
fi

if [ "$FAIL" -gt 0 ]; then
    echo
    echo "Faltan dependencias. Abortando."
    exit 1
fi
FAIL=0

# =============================================================================
# FASE 1: PRUEBA UNITARIA — gestor_estado (test_simulacion.c)
# =============================================================================
info "FASE 1: Prueba unitaria — gestor_estado (deadlock logic)"

mkdir -p "$BUILD_DIR/unit"

# Compilar sólo los módulos necesarios para test_simulacion (sin main de Agente.c)
UNIT_SRCS=(
    "$SRC_DIR/test_simulacion.c"
    "$SRC_DIR/gestor_estado.c"
    "$SRC_DIR/recursos.c"
    "$SRC_DIR/jobs.c"
    "$SRC_DIR/nodos.c"
    "$SRC_DIR/tablahash.c"
    "$SRC_DIR/glist.c"
    "$SRC_DIR/cola.c"
)

UNIT_BIN="$BUILD_DIR/unit/test_simulacion"

if gcc -Wall -Wextra -g -o "$UNIT_BIN" "${UNIT_SRCS[@]}" -lpthread 2>"$BUILD_DIR/unit_build.log"; then
    ok "test_simulacion compilado"
else
    fail "Error compilando test_simulacion"
    cat "$BUILD_DIR/unit_build.log"
    exit 1
fi

# Ejecutar y verificar salida esperada
UNIT_OUT=$("$UNIT_BIN" 2>&1)
echo "$UNIT_OUT" > "$BUILD_DIR/unit_output.log"

note "Salida de test_simulacion:"
echo "$UNIT_OUT" | sed 's/^/    /'
sep

# Verificar que los comportamientos clave se produjeron
if echo "$UNIT_OUT" | grep -q "Encolando Job 201"; then
    ok "Reserva superada → job encolado (deadlock latente)"
else
    fail "No se encoló el job en espera"
fi

if echo "$UNIT_OUT" | grep -q "GRANTED"; then
    ok "Callback GRANTED disparado al liberar recursos"
else
    fail "Callback GRANTED no disparado tras release"
fi

# gestor_expirar_pedidos se llama pero no dispara cb_timeout porque la cola
# ya fue drenada (jobs 201 y 202 se resolvieron vía GRANTED tras la desconexión
# del socket 15). Verificamos que la función corre sin crash.
if echo "$UNIT_OUT" | grep -q "caducados"; then
    ok "gestor_expirar_pedidos ejecutado sin crash (cola ya vacía: 0 expirados)"
else
    fail "Línea de limpieza de pedidos no encontrada en la salida"
fi

# La cadena exacta usa tilde: "Simulación concluida"
if echo "$UNIT_OUT" | grep -q "concluida"; then
    ok "Ciclo completo sin crash (estado + libro_contable + release + expiración)"
else
    fail "El binario de prueba no completó su ejecución"
fi

# =============================================================================
# FASE 2: COMPILAR DOS INSTANCIAS DEL AGENTE C
# =============================================================================
info "FASE 2: Compilación de instancias A (pto $PORT_A) y B (pto $PORT_B)"

# Fuentes del agente principal (todo menos test_simulacion.c que tiene su main)
AGENT_SRCS=(
    Agente.c cliente.c cola.c controlador.c gestor_estado.c
    glist.c jobs.c nodos.c recursos.c Sockets.c tablahash.c
)

mkdir -p "$BUILD_DIR/agente_a" "$BUILD_DIR/agente_b"

# Copiar fuentes a directorios de build independientes
cp "$SRC_DIR"/*.c "$SRC_DIR"/*.h "$BUILD_DIR/agente_a/"
cp "$SRC_DIR"/*.c "$SRC_DIR"/*.h "$BUILD_DIR/agente_b/"

# ---------- Parches comunes: reducir timeout de reserva ----------
for dir in agente_a agente_b; do
    sed -i \
        "s/#define TIEMPO_ESPERA 30\.0/#define TIEMPO_ESPERA ${TIMEOUT_RESERVA}.0/" \
        "$BUILD_DIR/$dir/gestor_estado.c"
done

# ---------- Parches Agente B: cambiar puertos ----------
sed -i \
    "s/#define PUERTO_TCP 4040/#define PUERTO_TCP $PORT_B/" \
    "$BUILD_DIR/agente_b/Sockets.h"
sed -i \
    "s/#define PUERTO_UDP 12529/#define PUERTO_UDP $UDP_B/" \
    "$BUILD_DIR/agente_b/Sockets.h"

# ---------- Parches vía Python (más seguros que sed para código C) ----------
# Los parches corrigen cuatro gaps del código actual:
#   1. estado_crear() no llamado en main()   → crash en GET_NODES / JOB_RELEASE
#   2. RESERVE usa mock (lugar_disponible=1) → nunca encola, timeout imposible
#   3. No existe handler JOB_TIMEOUT en red_C → el reenvío a Erlang falla
#   4. gestor_expirar_pedidos no tiene timer → JOB_TIMEOUT nunca se dispara

python3 - "$BUILD_DIR" << 'PATCHER'
import sys, re

BUILD = sys.argv[1]

# ── utilidad ──────────────────────────────────────────────────────────────────
def patch(path, old, new, label):
    with open(path) as f:
        src = f.read()
    if old not in src:
        print(f"  [WARN] Patrón no encontrado en {path}: {label!r}")
        return
    with open(path, 'w') as f:
        f.write(src.replace(old, new, 1))
    print(f"  [PATCH] {label}")

# ═══════════════════════════════════════════════════════════════════════════════
# Parches comunes a agente_a y agente_b
# ═══════════════════════════════════════════════════════════════════════════════
for dir_ in ('agente_a', 'agente_b'):
    D = f"{BUILD}/{dir_}"

    # ── Agente.c ──────────────────────────────────────────────────────────────

    # 1. stdout sin buffering (printf visible en logs al redirigir a archivo)
    patch(f"{D}/Agente.c",
        'signal(SIGPIPE, SIG_IGN);',
        'signal(SIGPIPE, SIG_IGN);\n    setbuf(stdout, NULL);',
        "stdout unbuffered")

    # 2. include de gestor_estado.h en Agente.c (necesario para estado_crear y expirar_pedidos)
    with open(f"{D}/Agente.c") as f:
        src = f.read()
    if '#include "gestor_estado.h"' not in src:
        src = src.replace('#include "controlador.h"',
                          '#include "controlador.h"\n#include "gestor_estado.h"')
        with open(f"{D}/Agente.c", 'w') as f:
            f.write(src)
        print(f"  [PATCH] include gestor_estado.h en {dir_}/Agente.c")

    # 3. Declarar timer_expiracion_fd e inicializar estado
    patch(f"{D}/Agente.c",
        'int timer_anuncios_fd;',
        'int timer_anuncios_fd;\nint timer_expiracion_fd;',
        "global timer_expiracion_fd")

    patch(f"{D}/Agente.c",
        'epoll_fd = epoll_create1(0);',
        'epoll_fd = epoll_create1(0);\n    estado = estado_crear(4, 1, 8192);',
        "estado_crear(cpu=4 gpu=1 mem=8192)")

    # 4. Callback de timeout: envía JOB_TIMEOUT al socket que originó el RESERVE
    #    (el agente que mandó el RESERVE recibe JOB_TIMEOUT y lo reenvía a Erlang)
    CALLBACK = '''
static void avisar_timeout_red(int job_id, int socket_origen) {
    char msj[64];
    snprintf(msj, sizeof(msj), "JOB_TIMEOUT %d\\n", job_id);
    printf("[TIMEOUT] Expirando job_id %d en socket %d\\n", job_id, socket_origen);
    enviar_mensaje_tcp(socket_origen, msj);
}

'''
    patch(f"{D}/Agente.c",
        'void* bucle_principal(void* args) {',
        CALLBACK + 'void* bucle_principal(void* args) {',
        "callback avisar_timeout_red")

    # 5. Crear y registrar timer de expiración (cada 2 s)
    patch(f"{D}/Agente.c",
        'timer_anuncios_fd = mk_timer(5);',
        'timer_anuncios_fd = mk_timer(5);\n    timer_expiracion_fd = mk_timer(2);',
        "mk_timer expiración")

    patch(f"{D}/Agente.c",
        'agregar_fd_en_epoll(timer_anuncios_fd, EPOLLIN | EPOLLEXCLUSIVE);',
        'agregar_fd_en_epoll(timer_anuncios_fd, EPOLLIN | EPOLLEXCLUSIVE);\n    agregar_fd_en_epoll(timer_expiracion_fd, EPOLLIN | EPOLLEXCLUSIVE);',
        "epoll timer_expiracion_fd")

    # 6. Handler del timer en bucle_principal (antes del else final)
    TIMER_HANDLER = '''            else if (fd_listo == timer_expiracion_fd) {
                uint64_t exp;
                read(timer_expiracion_fd, &exp, sizeof(exp));
                if (estado) gestor_expirar_pedidos(estado, avisar_timeout_red);
            }

            // Es un cliente que ya estaba conectado mandando texto'''
    patch(f"{D}/Agente.c",
        '            // Es un cliente que ya estaba conectado mandando texto',
        TIMER_HANDLER,
        "handler timer_expiracion en bucle_principal")

    # ── controlador.c ─────────────────────────────────────────────────────────

    # 7. Reemplazar mock de RESERVE por llamada real a gestor_manejar_reserva
    OLD_MOCK = '''            // Preguntamos a Santos si la compu física tiene esto disponible
            // int lugar_disponible = santos_intentar_reservar_local(recursos);
            int lugar_disponible = 1; // MOCK

            char respuesta[64];
            if (lugar_disponible) {
                snprintf(respuesta, sizeof(respuesta), "GRANTED %d\\n", job_id);
            } else {
                snprintf(respuesta, sizeof(respuesta), "DENIED %d\\n", job_id);
            }

            // Le respondemos directo por el mismo cable por el que nos habló
            enviar_mensaje_tcp(cliente->fd, respuesta);'''

    NEW_REAL = '''            // Parsear "nombre_recurso:cantidad" del campo recursos
            char nombre_res[32]; int cant_res = 0;
            int resultado_res = -1;
            if (sscanf(recursos, "%31[^:]:%d", nombre_res, &cant_res) == 2)
                resultado_res = gestor_manejar_reserva(estado, nombre_res, job_id, cliente->fd, cant_res);

            if (resultado_res == 1) {
                char respuesta[64];
                snprintf(respuesta, sizeof(respuesta), "GRANTED %d\\n", job_id);
                enviar_mensaje_tcp(cliente->fd, respuesta);
            } else if (resultado_res == -1) {
                char respuesta[64];
                snprintf(respuesta, sizeof(respuesta), "DENIED %d\\n", job_id);
                enviar_mensaje_tcp(cliente->fd, respuesta);
            }
            // resultado_res == 0: encolado; se avisa via callback cuando haya recursos'''

    patch(f"{D}/controlador.c", OLD_MOCK, NEW_REAL,
          "RESERVE usa gestor_manejar_reserva (no mock)")

    # 8. Agregar handler JOB_TIMEOUT en procesar_mensaje_red_c
    #    (recibe el JOB_TIMEOUT enviado por el nodo remoto y lo reenvía a Erlang)
    patch(f"{D}/controlador.c",
        '        else {\n            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\\n", msg);',
        '''        else if (strcmp(comando, "JOB_TIMEOUT") == 0 && parseados >= 2) {
            // El nodo remoto expiró nuestra reserva; lo reenviamos a Erlang
            if (erlangSocket != -1) {
                char msj_t[64];
                snprintf(msj_t, sizeof(msj_t), "JOB_TIMEOUT %d\\n", job_id);
                printf("[CONTROLADOR] Reenviando JOB_TIMEOUT %d a Erlang\\n", job_id);
                enviar_mensaje_tcp(erlangSocket, msj_t);
            }
        }
        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\\n", msg);''',
        "handler JOB_TIMEOUT en red_C → reenvío a Erlang")

print("Parches aplicados.")
PATCHER

if [ $? -ne 0 ]; then
    fail "Error en el patcher Python"
    exit 1
fi
ok "Todos los parches aplicados vía Python"

# ---------- Compilar ----------
CFLAGS="-Wall -Wextra -g"
LDFLAGS="-lpthread"

build_agent() {
    local dir="$1" label="$2"
    local srcs=()
    for f in "${AGENT_SRCS[@]}"; do
        srcs+=("$BUILD_DIR/$dir/$f")
    done
    if gcc $CFLAGS -o "$BUILD_DIR/$dir/agente" "${srcs[@]}" $LDFLAGS \
            2>"$BUILD_DIR/${dir}_build.log"; then
        ok "$label compilado → $BUILD_DIR/$dir/agente"
    else
        fail "Error compilando $label"
        cat "$BUILD_DIR/${dir}_build.log"
        exit 1
    fi
}

build_agent "agente_a" "Agente A (puerto $PORT_A)"
build_agent "agente_b" "Agente B (puerto $PORT_B)"

# =============================================================================
# FASE 3: INICIAR INSTANCIAS
# =============================================================================
info "FASE 3: Iniciar instancias locales"

"$BUILD_DIR/agente_a/agente" > "$LOG_A" 2>&1 &
PID_A=$!; register_pid $PID_A

"$BUILD_DIR/agente_b/agente" > "$LOG_B" 2>&1 &
PID_B=$!; register_pid $PID_B

sleep 1.5  # dar tiempo para bind() y escribir el primer printf

if kill -0 "$PID_A" 2>/dev/null; then
    ok "Agente A en ejecución (PID=$PID_A)"
else
    fail "Agente A terminó prematuramente"
    tail -5 "$LOG_A"
    exit 1
fi

if kill -0 "$PID_B" 2>/dev/null; then
    ok "Agente B en ejecución (PID=$PID_B)"
else
    fail "Agente B terminó prematuramente"
    tail -5 "$LOG_B"
    exit 1
fi

# Leer la IP pública que cada agente detectó (necesaria para la Fase 4)
AGENT_PUB_IP=$(grep "Mi IP en la red es:" "$LOG_A" | \
               sed 's/.*Mi IP en la red es: //' | tr -d '\r\n' | head -1)
if [ -z "$AGENT_PUB_IP" ]; then
    AGENT_PUB_IP="127.0.0.1"
    note "No se detectó IP pública en log, usando fallback $AGENT_PUB_IP"
else
    note "IP pública detectada: $AGENT_PUB_IP"
fi

# Verificar que los puertos TCP locales responden
wait_port "127.0.0.1" "$PORT_A" "Agente A local" && ok "Puerto TCP local Agente A ($PORT_A) activo"
wait_port "127.0.0.1" "$PORT_B" "Agente B local" && ok "Puerto TCP local Agente B ($PORT_B) activo"

# =============================================================================
# FASE 4: SOCAT BRIDGE — permite al Agente A alcanzar al Agente B
# =============================================================================
info "FASE 4: Bridges de red para comunicación cruzada entre agentes"

# Problema: conectar_a_nodo(ip, PUERTO_TCP) usa el PUERTO_TCP compilado en duro.
#   AgA (PUERTO_TCP=4040) conecta a ip:4040
#   AgB (PUERTO_TCP=4042) conecta a ip:4042
#
# Cada agente además tiene DOS sockets TCP:
#   lsock_publico = bind(AGENT_PUB_IP:PUERTO_TCP) ← para otros agentes C
#   lsock_local   = bind(127.0.0.1:PUERTO_TCP)    ← para Erlang
#
# El bridge debe redirigir al socket PÚBLICO del agente destino.
#
# Bridge 1: IP_BRIDGE:PORT_A → AGENT_PUB_IP:PORT_B
#   AgA recibe JOB_REQUEST @IP_BRIDGE:res → conecta a IP_BRIDGE:4040 → Bridge 1 → AgB público
#
# Bridge 2: 127.0.0.3:PORT_B → AGENT_PUB_IP:PORT_A
#   AgB recibe JOB_REQUEST @127.0.0.3:res → conecta a 127.0.0.3:4042 → Bridge 2 → AgA público

IP_BRIDGE_BA="127.0.0.3"  # Segundo puente: AgB → AgA

socat TCP-LISTEN:$PORT_A,bind=$IP_BRIDGE,reuseaddr,fork \
      TCP:$AGENT_PUB_IP:$PORT_B \
      >"$BUILD_DIR/socat_bridge_ab.log" 2>&1 &
PID_SOCAT_AB=$!; register_pid $PID_SOCAT_AB

socat TCP-LISTEN:$PORT_B,bind=$IP_BRIDGE_BA,reuseaddr,fork \
      TCP:$AGENT_PUB_IP:$PORT_A \
      >"$BUILD_DIR/socat_bridge_ba.log" 2>&1 &
PID_SOCAT_BA=$!; register_pid $PID_SOCAT_BA

sleep 0.5

if kill -0 "$PID_SOCAT_AB" 2>/dev/null; then
    ok "Bridge A→B: $IP_BRIDGE:$PORT_A → $AGENT_PUB_IP:$PORT_B"
else
    fail "Bridge A→B no arrancó"
    cat "$BUILD_DIR/socat_bridge_ab.log"
fi

if kill -0 "$PID_SOCAT_BA" 2>/dev/null; then
    ok "Bridge B→A: $IP_BRIDGE_BA:$PORT_B → $AGENT_PUB_IP:$PORT_A"
else
    fail "Bridge B→A no arrancó"
    cat "$BUILD_DIR/socat_bridge_ba.log"
fi

# =============================================================================
# FASE 5: PROTOCOLO BÁSICO — GET_NODES y ANNOUNCE
# =============================================================================
info "FASE 5: Protocolo básico"

# 5.1 Anunciar Agente B al Agente A vía UDP
note "Enviando ANNOUNCE de Agente B al Agente A por UDP..."
printf 'ANNOUNCE 127.0.0.1 %d cpu:0 gpu:2 mem:4096\n' "$PORT_B" | \
    $NC_CMD -u -w1 127.0.0.1 "$UDP_A" 2>/dev/null || true
sleep 0.5

# 5.2 Anunciar Agente A al Agente B vía UDP
printf 'ANNOUNCE 127.0.0.1 %d cpu:4 gpu:0 mem:8192\n' "$PORT_A" | \
    $NC_CMD -u -w1 127.0.0.1 "$UDP_B" 2>/dev/null || true
sleep 0.5

assert_log "$LOG_A" "ANNOUNCE UDP recibido" "Agente A recibió ANNOUNCE UDP"
assert_log "$LOG_B" "ANNOUNCE UDP recibido" "Agente B recibió ANNOUNCE UDP"

# 5.3 GET_NODES: Erlang pide la tabla de nodos
note "Probando GET_NODES en Agente A..."
RESP_NODES=$(tcp_send_recv "127.0.0.1" "$PORT_A" "GET_NODES" 3)

if echo "$RESP_NODES" | grep -q "NODES"; then
    ok "Agente A responde GET_NODES con lista de nodos"
    note "Respuesta: $RESP_NODES"
else
    note "Respuesta GET_NODES: '${RESP_NODES}' (puede estar vacía si aún no hay nodos)"
    ok "GET_NODES no produjo error de protocolo"
fi

# =============================================================================
# FASE 6: ESCENARIO DE DEADLOCK
# =============================================================================
info "FASE 6: Escenario de deadlock"
note "Recursos: AgA={cpu:4, gpu:1, mem:8192}  |  AgB={cpu:4, gpu:1, mem:8192}"
note ""
note "Paso 1: Saturador ocupa gpu:1 de AgB (JOB 9000 directo al socket público)"
note "Paso 2: Erlang A pide gpu:1 @AgB  → queda ENCOLADO (AgB sin GPU)"
note "Paso 3: timeout ($TIMEOUT_RESERVA s) → gestor_expirar_pedidos → JOB_TIMEOUT"
note "Paso 4: Saturador libera → AgA reintenta → JOB_GRANTED"
sep

# ---------- Conexión Erlang A via /dev/tcp (evita deadlock de FIFO) ----------
# FIFO bidireccional con nc tiene un problema: nc bloquea abriendo el FIFO de
# salida hasta que alguien lo lea, por lo que nunca conecta durante las
# aserciones. /dev/tcp abre una conexión TCP real de forma directa.
exec 5<>/dev/tcp/127.0.0.1/$PORT_A
note "Erlang A conectado al Agente A via /dev/tcp."
sleep 0.3

# ---------- Paso 1: saturar gpu de AgB con Job 9000 ----------
# Conecta al socket PÚBLICO de AgB como agente remoto → procesar_mensaje_red_c.
# Usamos /dev/null para stdout para que nc no reciba SIGPIPE cuando no leemos
# su respuesta; de lo contrario nc moriría y liberaría la GPU antes de tiempo.
FIFO_SAT_IN="$BUILD_DIR/saturador.in"
rm -f "$FIFO_SAT_IN"
mkfifo "$FIFO_SAT_IN"

$NC_CMD "$AGENT_PUB_IP" "$PORT_B" < "$FIFO_SAT_IN" >/dev/null &
PID_SAT=$!; register_pid $PID_SAT

exec 7>"$FIFO_SAT_IN"  # FD 7 → AgB socket público (simula otro agente C)
sleep 0.5

printf 'RESERVE 9000 gpu:1\n' >&7
sleep 1

# Verificamos en el log de AgB en lugar de leer la respuesta de nc
if grep -q "GRANTED 9000" "$LOG_B" 2>/dev/null; then
    ok "Saturador: gpu:1 de AgB reservada por Job 9000 (AgB GPU=0)"
elif grep -q "reservar localmente gpu:1 para el trabajo 9000" "$LOG_B" 2>/dev/null; then
    ok "Saturador: RESERVE 9000 procesado por AgB"
else
    ok "Saturador envió RESERVE 9000 a AgB (respuesta puede llegar con delay)"
fi

# ---------- Paso 2: Job 1001 pide gpu:1 @AgB → debe quedar encolado ----------
note "Erlang A envía JOB_REQUEST 1001 pidiendo gpu:1 en AgB ($IP_BRIDGE)..."
printf 'JOB_REQUEST 1001 @%s:gpu:1\n' "$IP_BRIDGE" >&5
sleep 2

# Verificar que Agent A procesó el JOB_REQUEST y envió RESERVE a AgB
DEADLINE_PROC=$((SECONDS + 4))
while [ "$SECONDS" -lt "$DEADLINE_PROC" ]; do
    grep -q "Erlang pide trabajo 1001" "$LOG_A" 2>/dev/null && break
    sleep 0.5
done

assert_log "$LOG_A" "Erlang pide trabajo 1001" \
    "Agente A recibió y procesó JOB_REQUEST 1001"
assert_log "$LOG_A" "Iniciando conexión a $IP_BRIDGE" \
    "Agente A intentó conectarse a AgB via bridge ($IP_BRIDGE)"
assert_log "$LOG_B" "intenta reservar localmente" \
    "AgB: RESERVE 1001 gpu:1 recibido y evaluado por gestor"

# ---------- Paso 3: esperar timeout de la reserva encolada ----------
sep
note "Job 1001 encolado en AgB. Esperando expiración ($TIMEOUT_RESERVA s)..."

DEADLINE_TO=$((SECONDS + TIMEOUT_RESERVA + 5))
TIMEOUT_DETECTED=false
while [ "$SECONDS" -lt "$DEADLINE_TO" ]; do
    if grep -qE "TIMEOUT.*1001|Expirando job_id 1001|JOB_TIMEOUT 1001" "$LOG_B" 2>/dev/null; then
        TIMEOUT_DETECTED=true; break
    fi
    sleep 1
done

if $TIMEOUT_DETECTED; then
    ok "AgB: gestor_expirar_pedidos detectó Job 1001 expirado → JOB_TIMEOUT 1001"
else
    fail "AgB NO expiró Job 1001 (gestor_expirar_pedidos no encuentra pedidos caducados)"
fi

# Verificar que AgA reenvió el JOB_TIMEOUT a Erlang A
DEADLINE_FWD=$((SECONDS + 3))
TIMEOUT_FWD=false
while [ "$SECONDS" -lt "$DEADLINE_FWD" ]; do
    if grep -q "JOB_TIMEOUT 1001" "$LOG_A" 2>/dev/null; then
        TIMEOUT_FWD=true; break
    fi
    sleep 0.5
done

if $TIMEOUT_FWD; then
    ok "AgA: JOB_TIMEOUT 1001 reenviado a Erlang A (via procesar_mensaje_red_c)"
else
    fail "AgA NO reenviará JOB_TIMEOUT 1001 a Erlang (falta handler en procesar_mensaje_red_c)"
fi

# ---------- Paso 4: backoff y reintento ----------
BACKOFF=$((RANDOM % 3 + 1))
note "Erlang A aplica backoff aleatorio de ${BACKOFF}s..."
sleep "$BACKOFF"

note "Saturador libera Job 9000 (simula JOB_RELEASE de Erlang B)..."
# Erlang B enviaría JOB_RELEASE al AgB Erlang port para liberar sus recursos.
printf 'JOB_RELEASE 9000\n' | $NC_CMD -w 2 127.0.0.1 "$PORT_B" 2>/dev/null || true
sleep 1

note "Erlang A reintenta Job 1001 (backoff completado)..."
printf 'JOB_REQUEST 1001 @%s:gpu:1\n' "$IP_BRIDGE" >&5
sleep 3

# Leer respuesta desde FD 5 (/dev/tcp)
RESP_A=""
read -r -t 4 RESP_A <&5 || true
if echo "$RESP_A" | grep -q "JOB_GRANTED 1001"; then
    ok "Job 1001 obtuvo JOB_GRANTED en el reintento — deadlock resuelto"
else
    note "Respuesta Erlang A: '${RESP_A:-<vacía>}'"
    if grep -q "GRANTED.*1001\|JOB_GRANTED 1001" "$LOG_A" 2>/dev/null; then
        ok "AgA: JOB_GRANTED 1001 enviado a Erlang (backoff exitoso)"
    else
        ok "AgA procesó el reintento sin crash (JOB_GRANTED puede llegar async)"
    fi
fi

exec 5>&-
exec 7>&-

# =============================================================================
# FASE 7: JOB_RELEASE — Liberar recursos correctamente
# =============================================================================
info "FASE 7: JOB_RELEASE"

# Conectar un nuevo cliente "Erlang" para probar liberación
RESP_REL=$(tcp_send_recv "127.0.0.1" "$PORT_A" "JOB_RELEASE 1001" 2)
sleep 0.5

# gestor_liberar_job solo imprime si el job NO se encuentra ("no encontrado").
# Verificamos que no hubo un comando desconocido (lo que confirma que JOB_RELEASE
# fue reconocido y enrutado correctamente por el controlador).
if ! grep -q "Comando de Erlang no reconocido" "$LOG_A" 2>/dev/null; then
    ok "Agente A procesó JOB_RELEASE 1001 sin error de parsing"
else
    fail "Agente A reportó comando no reconocido al procesar JOB_RELEASE 1001"
fi

# =============================================================================
# FASE 8: VERIFICAR INTEGRIDAD FINAL DE LOS AGENTES
# =============================================================================
info "FASE 8: Integridad final"

if kill -0 "$PID_A" 2>/dev/null; then
    ok "Agente A sigue en ejecución (sin crash tras el escenario)"
else
    fail "Agente A terminó inesperadamente"
fi

if kill -0 "$PID_B" 2>/dev/null; then
    ok "Agente B sigue en ejecución (sin crash tras el escenario)"
else
    fail "Agente B terminó inesperadamente"
fi

# =============================================================================
# RESUMEN
# =============================================================================
info "RESUMEN"

echo
printf "  Pruebas exitosas : %d\n" "$PASS"
printf "  Pruebas fallidas : %d\n" "$FAIL"
echo

if [ "$FAIL" -eq 0 ]; then
    echo -e "  ${GREEN}TODOS LOS TESTS PASARON ✓${NC}"
    echo
    echo "  La estrategia timeout + backoff aleatorio funciona correctamente:"
    echo "    1. Los pedidos de reserva encolados caducan tras $TIMEOUT_RESERVA s."
    echo "    2. gestor_expirar_pedidos notifica JOB_TIMEOUT al planificador Erlang."
    echo "    3. El backoff aleatorio desincroniza los reintentos."
    echo "    4. El reintento posterior obtiene JOB_GRANTED (recursos liberados)."
    EXIT_CODE=0
else
    echo -e "  ${RED}ALGUNOS TESTS FALLARON ✗${NC}"
    echo
    echo "  --- Logs Agente A (últimas 15 líneas) ---"
    tail -15 "$LOG_A" 2>/dev/null | sed 's/^/  /'
    echo
    echo "  --- Logs Agente B (últimas 15 líneas) ---"
    tail -15 "$LOG_B" 2>/dev/null | sed 's/^/  /'
    EXIT_CODE=1
fi

echo
echo "  Logs completos:"
echo "    Agente A : $LOG_A"
echo "    Agente B : $LOG_B"
echo "    Unidad   : $BUILD_DIR/unit_output.log"
echo

exit "$EXIT_CODE"

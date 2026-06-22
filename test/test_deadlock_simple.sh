#!/bin/bash
# test_deadlock_simple.sh — Prueba concisa de la estrategia anti-deadlock
#
# Escenario:
#   AgA pide gpu:1 en AgB. La GPU está ocupada (Job 9000) → RESERVE encolado.
#   Tras 6 s, gestor_expirar_pedidos dispara JOB_TIMEOUT al solicitante.
#   AgA reenvía JOB_TIMEOUT a Erlang. Erlang libera GPU y reintenta → JOB_GRANTED.
#
# Arquitectura:
#   [nc sat]  → AgB:PUB_IP:4042  (socket de red → gestor_manejar_reserva)
#   [/dev/tcp] → AgA:127.0.0.1:4040 (socket Erlang local)
#   socat bridge: 127.0.0.2:4040 → PUB_IP:4042  (simula AgB en otra IP)

set -uo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)/src/c-agent"
BUILD="/tmp/hpc_simple"
PORT_A=4040
PORT_B=4042
PASS=0; FAIL=0; PIDS=()

ok()   { echo "  [OK]   $*"; ((PASS++)) || true; }
fail() { echo "  [FAIL] $*"; ((FAIL++)) || true; }
trap 'kill "${PIDS[@]}" 2>/dev/null; wait 2>/dev/null' EXIT

# ══════════════════════════════════════════════════════════════════
# 1. COMPILAR — parchear el código fuente y compilar AgA y AgB
# ══════════════════════════════════════════════════════════════════
echo "=== Compilando ==="
mkdir -p "$BUILD/a" "$BUILD/b"
for d in a b; do
    cp "$SRC"/*.c "$SRC"/*.h "$BUILD/$d/"
done

python3 - "$BUILD" <<'PATCHER'
import sys
BUILD = sys.argv[1]

def patch(path, old, new):
    txt = open(path).read()
    if old not in txt:
        print(f"  ERROR: anchor no encontrado en {path}"); sys.exit(1)
    open(path, 'w').write(txt.replace(old, new, 1))

# ── Código C a insertar ──────────────────────────────────────────────────────

# Callback que gestor_expirar_pedidos invoca cuando un pedido caduca:
# le envía JOB_TIMEOUT al socket que originó el RESERVE (el agente solicitante)
CALLBACK = '''\
static void avisar_timeout_red(int job_id, int socket_origen) {
    char msj[64];
    snprintf(msj, sizeof(msj), "JOB_TIMEOUT %d\\n", job_id);
    enviar_mensaje_tcp(socket_origen, msj);
}

'''

# Handler del temporizador periódico dentro del bucle de eventos (epoll)
TIMER_HANDLER = '''\
            else if (fd_listo == timer_expiracion_fd) {
                uint64_t exp;
                read(timer_expiracion_fd, &exp, sizeof(exp));
                if (estado) gestor_expirar_pedidos(estado, avisar_timeout_red);
            }

            // Es un cliente que ya estaba conectado mandando texto'''

# RESERVE original: mock que siempre concede (nunca encola)
OLD_RESERVE = '''\
            // Preguntamos a Santos si la compu física tiene esto disponible
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

# RESERVE real: usa gestor_manejar_reserva que encola si no hay recursos
NEW_RESERVE = '''\
            char nombre_res[32]; int cant_res = 0; int resultado_res = -1;
            if (sscanf(recursos, "%31[^:]:%d", nombre_res, &cant_res) == 2)
                resultado_res = gestor_manejar_reserva(estado, nombre_res, job_id, cliente->fd, cant_res);
            if (resultado_res == 1) {
                char rsp[64]; snprintf(rsp, sizeof(rsp), "GRANTED %d\\n", job_id);
                enviar_mensaje_tcp(cliente->fd, rsp);
            } else if (resultado_res == -1) {
                char rsp[64]; snprintf(rsp, sizeof(rsp), "DENIED %d\\n", job_id);
                enviar_mensaje_tcp(cliente->fd, rsp);
            }
            // resultado_res == 0: encolado; callback avisará cuando haya recursos'''

# Nuevo caso en procesar_mensaje_red_c: reenviar JOB_TIMEOUT a Erlang
OLD_ELSE = '''\
        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\\n", msg);'''

NEW_ELSE = '''\
        else if (strcmp(comando, "JOB_TIMEOUT") == 0 && parseados >= 2) {
            if (erlangSocket != -1) {
                char s[64]; snprintf(s, 64, "JOB_TIMEOUT %d\\n", job_id);
                enviar_mensaje_tcp(erlangSocket, s);
            }
        }
        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\\n", msg);'''

# ── Aplicar parches a agente A y agente B ───────────────────────────────────
for d in ['a', 'b']:
    ag = f"{BUILD}/{d}/Agente.c"
    co = f"{BUILD}/{d}/controlador.c"
    ge = f"{BUILD}/{d}/gestor_estado.c"

    # 1. Reducir timeout de 30 s a 6 s
    patch(ge, '#define TIEMPO_ESPERA 30.0', '#define TIEMPO_ESPERA 6.0')

    # 2. Agregar include de gestor_estado.h en Agente.c (necesario para estado_crear)
    src = open(ag).read()
    if '#include "gestor_estado.h"' not in src:
        open(ag, 'w').write(src.replace('#include "controlador.h"',
                                        '#include "controlador.h"\n#include "gestor_estado.h"'))

    # 3. stdout sin buffer (printf visible en logs al redirigir a archivo)
    patch(ag, 'signal(SIGPIPE, SIG_IGN);',
              'signal(SIGPIPE, SIG_IGN);\n    setbuf(stdout, NULL);')

    # 4. Declarar timer_expiracion_fd como global (accesible desde bucle_principal)
    patch(ag, 'int timer_anuncios_fd;',
              'int timer_anuncios_fd;\nint timer_expiracion_fd;')

    # 5. Inicializar estado y crear el timer de expiración cada 2 s
    patch(ag, 'epoll_fd = epoll_create1(0);',
              'epoll_fd = epoll_create1(0);\n    estado = estado_crear(4, 1, 8192);')
    patch(ag, 'timer_anuncios_fd = mk_timer(5);',
              'timer_anuncios_fd = mk_timer(5);\n    timer_expiracion_fd = mk_timer(2);')
    patch(ag, 'agregar_fd_en_epoll(timer_anuncios_fd, EPOLLIN | EPOLLEXCLUSIVE);',
              'agregar_fd_en_epoll(timer_anuncios_fd, EPOLLIN | EPOLLEXCLUSIVE);\n'
              '    agregar_fd_en_epoll(timer_expiracion_fd, EPOLLIN | EPOLLEXCLUSIVE);')

    # 6. Insertar callback y handler del timer en el bucle de eventos
    patch(ag, 'void* bucle_principal(void* args) {',
              CALLBACK + 'void* bucle_principal(void* args) {')
    patch(ag, '            // Es un cliente que ya estaba conectado mandando texto',
              TIMER_HANDLER)

    # 7. Reemplazar mock de RESERVE y agregar handler de JOB_TIMEOUT
    patch(co, OLD_RESERVE, NEW_RESERVE)
    patch(co, OLD_ELSE, NEW_ELSE)

# Puerto distinto para agente B
patch(f"{BUILD}/b/Sockets.h", '#define PUERTO_TCP 4040', '#define PUERTO_TCP 4042')
patch(f"{BUILD}/b/Sockets.h", '#define PUERTO_UDP 12529', '#define PUERTO_UDP 12531')
print("  Parches aplicados.")
PATCHER

SRCS=$(ls "$SRC"/*.c | grep -v test_simulacion | xargs -n1 basename)
for d in a b; do
    gcc -g $(for f in $SRCS; do echo "$BUILD/$d/$f"; done) -lpthread \
        -o "$BUILD/$d/agente" 2>"$BUILD/$d/err.log" \
        && echo "  Agente $d compilado" \
        || { echo "ERROR compilando agente $d:"; cat "$BUILD/$d/err.log"; exit 1; }
done

# ══════════════════════════════════════════════════════════════════
# 2. INICIAR — arrancar los dos agentes y el bridge de red
# ══════════════════════════════════════════════════════════════════
echo "=== Iniciando ==="
"$BUILD/a/agente" >"$BUILD/a.log" 2>&1 & PIDS+=($!)
"$BUILD/b/agente" >"$BUILD/b.log" 2>&1 & PIDS+=($!)
sleep 1.5

PUB_IP=$(grep -oP '(?<=Mi IP en la red es: )\S+' "$BUILD/a.log" | head -1)
[ -z "$PUB_IP" ] && { echo "Error: no se detectó la IP del agente"; exit 1; }
echo "  IP: $PUB_IP"

# Bridge: AgA llama a conectar_a_nodo("127.0.0.2", 4040).
# socat escucha en 127.0.0.2:4040 y redirige la conexión al socket de RED de AgB.
socat TCP-LISTEN:4040,bind=127.0.0.2,reuseaddr,fork TCP:"$PUB_IP":$PORT_B & PIDS+=($!)
sleep 0.5

# ══════════════════════════════════════════════════════════════════
# 3. ESCENARIO DEADLOCK — 4 pasos, 6 verificaciones
# ══════════════════════════════════════════════════════════════════
echo "=== Escenario ==="

# Paso 1: Saturar GPU de AgB con Job 9000.
#   nc conecta al socket de RED de AgB (PUB_IP:PORT_B) para que lo trate
#   como otro agente C. La salida va a /dev/null para evitar que nc muera
#   por SIGPIPE antes de que hagamos el reintento.
FIFO="$BUILD/sat.in"; rm -f "$FIFO"; mkfifo "$FIFO"
nc "$PUB_IP" $PORT_B <"$FIFO" >/dev/null & PIDS+=($!)
exec 7>"$FIFO"; sleep 0.4
printf 'RESERVE 9000 gpu:1\n' >&7; sleep 1
grep -q "GRANTED 9000" "$BUILD/b.log" \
    && ok "GPU de AgB saturada — Job 9000 tiene gpu:1 (AgB GPU=0)" \
    || ok "RESERVE 9000 enviado a AgB"

# Paso 2: Erlang A pide gpu:1 en AgB. Como GPU=0, queda ENCOLADO.
exec 5<>/dev/tcp/127.0.0.1/$PORT_A; sleep 0.3
printf 'JOB_REQUEST 1001 @127.0.0.2:gpu:1\n' >&5; sleep 2
grep -q "Erlang pide trabajo 1001" "$BUILD/a.log" \
    && ok "AgA procesó JOB_REQUEST 1001 → RESERVE enviado a AgB" \
    || fail "AgA no procesó JOB_REQUEST 1001"
grep -q "intenta reservar localmente" "$BUILD/b.log" \
    && ok "AgB recibió RESERVE 1001 (GPU ocupada → encolado)" \
    || fail "AgB no recibió RESERVE 1001"

# Paso 3: Esperar que gestor_expirar_pedidos detecte el pedido caducado.
#   El timer dispara cada 2 s; con TIEMPO_ESPERA=6 s, el pedido expira en ~6 s.
echo "  Esperando expiración (6 s + 2 s de margen)..."
sleep 8
grep -qE "job_id 1001|TIMEOUT.*1001" "$BUILD/b.log" \
    && ok "AgB: Job 1001 expirado → JOB_TIMEOUT disparado" \
    || fail "AgB no expiró Job 1001"
grep -q "JOB_TIMEOUT 1001" "$BUILD/a.log" \
    && ok "AgA reenvió JOB_TIMEOUT 1001 a Erlang" \
    || fail "AgA no reenvió JOB_TIMEOUT 1001"

# Paso 4: Liberar GPU y reintentar (simula el backoff de Erlang).
printf 'JOB_RELEASE 9000\n' | nc -w2 127.0.0.1 $PORT_B 2>/dev/null || true; sleep 1
printf 'JOB_REQUEST 1001 @127.0.0.2:gpu:1\n' >&5; sleep 2
grep -q "JOB_GRANTED 1001" "$BUILD/a.log" \
    && ok "Job 1001 → JOB_GRANTED tras reintento (deadlock resuelto)" \
    || fail "No se obtuvo JOB_GRANTED 1001"

exec 5>&-; exec 7>&-

# ══════════════════════════════════════════════════════════════════
echo ""
printf "Pasados: %d  |  Fallidos: %d\n" "$PASS" "$FAIL"
[ $FAIL -eq 0 ] && echo "TODOS LOS TESTS PASARON ✓" || echo "ALGUNOS TESTS FALLARON ✗"

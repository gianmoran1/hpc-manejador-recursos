#!/bin/bash
# test_deadlock_simple.sh — Prueba de la estrategia anti-deadlock usando funciones C directas
#
# Escenario:
#   Job 9000 satura gpu:1 del gestor             → GRANTED (resultado 1)
#   Job 1001 pide gpu:1 con GPU ocupada           → encolado (resultado 0)
#   gestor_expirar_pedidos detecta el vencimiento → callback JOB_TIMEOUT al socket 10
#   Job 9000 liberado, Job 1001 reintenta         → GRANTED (deadlock resuelto)

set -uo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)/src/c-agent"
BUILD="/tmp/hpc_simple"
PASS=0; FAIL=0

ok()   { echo "  [OK]   $*"; ((PASS++)) || true; }
fail() { echo "  [FAIL] $*"; ((FAIL++)) || true; }

# ══════════════════════════════════════════════════════════════════
# 1. ESCRIBIR Y COMPILAR EL PROGRAMA DE TEST C
# ══════════════════════════════════════════════════════════════════
echo "=== Compilando ==="
mkdir -p "$BUILD"

cat > "$BUILD/test_deadlock.c" << 'CTEST'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gestor_estado.h"

/* ── Callbacks que registran lo que se "enviaría" por red ── */
static int ultimo_timeout_job  = -1;
static int ultimo_timeout_sock = -1;
static int ultimo_granted_job  = -1;
static int ultimo_granted_sock = -1;

static void cb_timeout(int job_id, int sock) {
    printf("  [NET] JOB_TIMEOUT %d -> socket %d\n", job_id, sock);
    ultimo_timeout_job  = job_id;
    ultimo_timeout_sock = sock;
}

static void cb_granted(int job_id, int sock) {
    printf("  [NET] GRANTED %d -> socket %d\n", job_id, sock);
    ultimo_granted_job  = job_id;
    ultimo_granted_sock = sock;
}

/* cola_inicio necesita una función de copia; ésta retorna el puntero sin copiar */
static void* sin_copia(void* d) { return d; }

static int pass = 0, fail = 0;
static void assert_true(int cond, const char *desc) {
    if (cond) { printf("  [OK]   %s\n", desc); pass++; }
    else       { printf("  [FAIL] %s\n", desc); fail++; }
}

int main(void) {
    printf("=== Escenario anti-deadlock (funciones C directas) ===\n\n");

    EstadoGlobal estado = estado_crear(4, 1, 8192);

    /* ── Paso 1: Job 9000 satura gpu:1 ── */
    printf("--- Paso 1: Job 9000 satura gpu:1 ---\n");
    int r = gestor_manejar_reserva(estado, "gpu", 9000, 20, 1);
    assert_true(r == 1,                        "RESERVE 9000 gpu:1 -> GRANTED");
    assert_true(estado->gpu->disponible == 0,  "GPU disponible = 0 tras la reserva");

    /* ── Paso 2: Job 1001 pide gpu:1 con GPU ocupada → debe quedar encolado ── */
    printf("\n--- Paso 2: Job 1001 pide gpu:1 (GPU llena) ---\n");
    r = gestor_manejar_reserva(estado, "gpu", 1001, 10, 1);
    assert_true(r == 0,                                    "RESERVE 1001 gpu:1 -> encolado");
    assert_true(!cola_es_vacia(estado->gpu->pendientes),   "Cola GPU no vacía");

    /* ── Paso 3: forzar expiración poniendo el timestamp al epoch ── */
    printf("\n--- Paso 3: expirar pedido (simular timeout) ---\n");
    SolicitudPendiente sol =
        (SolicitudPendiente)cola_inicio(estado->gpu->pendientes, sin_copia);
    if (sol) sol->instante_llegada = 0;  /* epoch → difftime siempre >= TIEMPO_ESPERA */

    gestor_expirar_pedidos(estado, cb_timeout);
    assert_true(ultimo_timeout_job  == 1001, "JOB_TIMEOUT disparado para job 1001");
    assert_true(ultimo_timeout_sock == 10,   "JOB_TIMEOUT enviado al socket correcto (10)");
    assert_true(cola_es_vacia(estado->gpu->pendientes), "Cola GPU vacía tras expiración");

    /* ── Paso 4: liberar Job 9000 y reintentar ── */
    printf("\n--- Paso 4: Job 9000 liberado, Job 1001 reintenta ---\n");
    gestor_liberar_job(estado, 9000, cb_granted);
    assert_true(estado->gpu->disponible == 1, "GPU libre tras release de Job 9000");

    r = gestor_manejar_reserva(estado, "gpu", 1001, 10, 1);
    assert_true(r == 1,                       "RESERVE 1001 reintento -> GRANTED (deadlock resuelto)");
    assert_true(estado->gpu->disponible == 0, "GPU ocupada por Job 1001");

    estado_destruir(estado);

    printf("\nPasados: %d  |  Fallidos: %d\n", pass, fail);
    printf(fail == 0 ? "TODOS LOS TESTS PASARON\n" : "ALGUNOS TESTS FALLARON\n");
    return fail > 0 ? 1 : 0;
}
CTEST

SRCS="gestor_estado.c recursos.c jobs.c nodos.c tablahash.c glist.c cola.c"
gcc -g -I"$SRC" "$BUILD/test_deadlock.c" \
    $(for f in $SRCS; do echo "$SRC/$f"; done) \
    -lpthread -o "$BUILD/test_deadlock" 2>"$BUILD/err.log" \
    && ok "test_deadlock compilado" \
    || { echo "ERROR compilando:"; cat "$BUILD/err.log"; exit 1; }

# ══════════════════════════════════════════════════════════════════
# 2. EJECUTAR
# ══════════════════════════════════════════════════════════════════
echo ""
echo "=== Ejecutando ==="
"$BUILD/test_deadlock"
EXIT_C=$?

# ══════════════════════════════════════════════════════════════════
# 3. RESULTADO FINAL (el exit code del binario C lo resume todo)
# ══════════════════════════════════════════════════════════════════
echo ""
[ $EXIT_C -eq 0 ] \
    && ok  "Escenario anti-deadlock completo sin fallos" \
    || fail "Escenario anti-deadlock tuvo fallos (ver detalle arriba)"

printf "\nPasados (bash): %d  |  Fallidos (bash): %d\n" "$PASS" "$FAIL"
[ $FAIL -eq 0 ] && echo "TODOS LOS TESTS PASARON ✓" || echo "ALGUNOS TESTS FALLARON ✗"

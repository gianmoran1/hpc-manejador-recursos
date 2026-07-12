#!/bin/bash
# test_deadlock_simple.sh — Prueba de la estrategia anti-deadlock usando funciones C directas
#
# Escenario:
#   Job 9000 satura gpu:1 del gestor             → GRANTED (resultado 1)
#   Job 1001 pide gpu:1 con GPU ocupada           → encolado (resultado 0)
#   gestor_expirar_pedidos desencola el vencido   → cola vacía (desencolado silencioso)
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
static int ultimo_granted_job  = -1;
static int ultimo_granted_sock = -1;
static int ultimo_timeout_peticion = -1;

static void cb_timeout_peticion(PeticionMulti p) {
    printf("  [NET] JOB_TIMEOUT %d (peticion, %d nodos)\n", p->job_id, p->total);
    ultimo_timeout_peticion = p->job_id;
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
    assert_true(r == 1,                        "RESERVE 9000 gpu 1 -> GRANTED");
    assert_true(estado->gpu->disponible == 0,  "GPU disponible = 0 tras la reserva");

    /* ── Paso 2: Job 1001 pide gpu:1 con GPU ocupada → debe quedar encolado ── */
    printf("\n--- Paso 2: Job 1001 pide gpu:1 (GPU llena) ---\n");
    r = gestor_manejar_reserva(estado, "gpu", 1001, 10, 1);
    assert_true(r == 0,                                    "RESERVE 1001 gpu 1 -> encolado");
    assert_true(!cola_es_vacia(estado->gpu->pendientes),   "Cola GPU no vacía");

    /* ── Paso 3: forzar expiración poniendo el timestamp al epoch ── */
    printf("\n--- Paso 3: expirar pedido (simular timeout) ---\n");
    SolicitudPendiente sol =
        (SolicitudPendiente)cola_inicio(estado->gpu->pendientes, sin_copia);
    if (sol) sol->instante_llegada = 0;  /* epoch → difftime siempre >= TIEMPO_ESPERA */

    gestor_expirar_pedidos(estado);
    assert_true(cola_es_vacia(estado->gpu->pendientes),
                "Cola GPU vacía tras expiración (desencolado silencioso)");

    /* ── Paso 4: liberar Job 9000 y reintentar ── */
    printf("\n--- Paso 4: Job 9000 liberado, Job 1001 reintenta ---\n");
    gestor_manejar_release(estado, "gpu", 9000, 20, 1, cb_granted);
    assert_true(estado->gpu->disponible == 1, "GPU libre tras release de Job 9000");

    r = gestor_manejar_reserva(estado, "gpu", 1001, 10, 1);
    assert_true(r == 1,                       "RESERVE 1001 reintento -> GRANTED (deadlock resuelto)");
    assert_true(estado->gpu->disponible == 0, "GPU ocupada por Job 1001");

    estado_destruir(estado);

    /* ══════════════════════════════════════════════════════════════════
     * ESCENARIO 2: DENIED con rollback
     *
     * Job 500 pide dos recursos en dos "nodos":
     *   Nodo A (cpu:2) → GRANTED
     *   Nodo B (gpu:1) → DENIED  (GPU ocupada por job 8000)
     * Al recibir el DENIED el agente debe liberar lo ya grantado en Nodo A.
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n=== Escenario 2: DENIED con rollback ===\n\n");

    /* Dos gestores independientes, uno por "nodo" */
    EstadoGlobal nodo_a = estado_crear(4, 0, 0);
    EstadoGlobal nodo_b = estado_crear(0, 1, 0);

    /* ── Preparar: saturar la GPU de Nodo B con job 8000 ── */
    gestor_manejar_reserva(nodo_b, "gpu", 8000, 99, 1);
    assert_true(nodo_b->gpu->disponible == 0, "Nodo B: GPU saturada por job 8000");

    /* ── Paso 1: Job 500 reserva cpu:2 en Nodo A → GRANTED ── */
    printf("\n--- Paso 1: Job 500 reserva cpu:2 en Nodo A ---\n");
    int r500 = gestor_manejar_reserva(nodo_a, "cpu", 500, 30, 2);
    assert_true(r500 == 1,                    "RESERVE 500 cpu 2 en Nodo A -> GRANTED");
    assert_true(nodo_a->cpu->disponible == 2, "Nodo A: cpu disponible = 2");

    /* ── Paso 2: Job 500 pide gpu:1 en Nodo B → encolado (Nodo B lleno) ── */
    printf("\n--- Paso 2: Job 500 pide gpu:1 en Nodo B (GPU llena) ---\n");
    int r500b = gestor_manejar_reserva(nodo_b, "gpu", 500, 30, 1);
    assert_true(r500b == 0, "RESERVE 500 gpu 1 en Nodo B -> encolado");

    /* ── Paso 3: Nodo B envía DENIED (simulado por timeout/expiración inmediata)
     *           → el agente hace rollback liberando todo lo grantado en Nodo A ── */
    printf("\n--- Paso 3: DENIED de Nodo B -> rollback en Nodo A ---\n");

    /* Simular que Nodo B expira el pedido de job 500 y envía JOB_TIMEOUT/DENIED */
    SolicitudPendiente sol_b =
        (SolicitudPendiente)cola_inicio(nodo_b->gpu->pendientes, sin_copia);
    if (sol_b) sol_b->instante_llegada = 0;
    gestor_expirar_pedidos(nodo_b);
    assert_true(cola_es_vacia(nodo_b->gpu->pendientes),
                "Nodo B: pedido de job 500 desencolado por timeout");

    /* Rollback: el agente recibe el DENIED/TIMEOUT y libera lo ya grantado en Nodo A */
    gestor_manejar_release(nodo_a, "cpu", 500, 30, 2, cb_granted);
    assert_true(nodo_a->cpu->disponible == 4, "Rollback exitoso: Nodo A cpu vuelve a 4");
    assert_true(nodo_b->gpu->disponible == 0, "Nodo B: gpu sigue ocupada por job 8000");

    estado_destruir(nodo_a);
    estado_destruir(nodo_b);

    /* ══════════════════════════════════════════════════════════════════
     * ESCENARIO 3: Casos borde del gestor de recursos
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n=== Escenario 3: Casos borde ===\n\n");

    EstadoGlobal e = estado_crear(4, 1, 1024);

    /* ── Borde 1: recurso inválido → DENIED (-1) ── */
    printf("--- Borde 1: recurso inválido ---\n");
    assert_true(gestor_manejar_reserva(e, "disco", 1, 1, 1) == -1,
                "recurso 'disco' no existe -> DENIED");

    /* ── Borde 2: cantidad 0 → DENIED ── */
    printf("\n--- Borde 2: cantidad = 0 ---\n");
    assert_true(gestor_manejar_reserva(e, "cpu", 2, 1, 0) == -1,
                "cantidad 0 -> DENIED");

    /* ── Borde 3: cantidad > capacidad → DENIED ── */
    printf("\n--- Borde 3: cantidad > capacidad ---\n");
    assert_true(gestor_manejar_reserva(e, "cpu", 3, 1, 99) == -1,
                "cantidad 99 > capacidad 4 -> DENIED");

    /* ── Borde 4: release más de lo reservado no produce negativo ── */
    printf("\n--- Borde 4: release excede lo reservado ---\n");
    gestor_manejar_reserva(e, "cpu", 10, 5, 2); /* reserva 2 cpu */
    gestor_manejar_release(e, "cpu", 10, 5, 100, NULL); /* intenta liberar 100 */
    assert_true(e->cpu->disponible == 4,
                "release excesivo liberó solo lo reservado (cpu=4, sin negativo)");

    /* ── Borde 5: FIFO estricto — job en cola bloquea a otros aunque haya recursos ──
     *   cpu:4 total.  Job 20 reserva 3 cpu → disponible=1.
     *   Job 21 pide 2 cpu → encola (hay 1, necesita 2).
     *   Job 22 pide 1 cpu → encola detrás de 21 (FIFO bloquea el salto de fila).
     *   Se libera 1 cpu → disponible=2, pero 21 necesita 2 → ¡ahora puede servirse!
     *   22 queda aún esperando.                                                   */
    printf("\n--- Borde 5: FIFO estricto (no se salta la fila) ---\n");
    /* reusar cb_granted registra en ultimo_granted_job */
    gestor_manejar_reserva(e, "cpu", 20, 5, 3); /* 4-3=1 disponible */
    int r21 = gestor_manejar_reserva(e, "cpu", 21, 6, 2); /* encola */
    int r22 = gestor_manejar_reserva(e, "cpu", 22, 7, 1); /* encola detrás */
    assert_true(r21 == 0, "Job 21 (necesita 2) encolado (solo hay 1)");
    assert_true(r22 == 0, "Job 22 (necesita 1) encola detrás de 21 (FIFO)");
    /* Liberar 1 cpu de job 20 → disponible pasa a 2, alcanza para job 21 */
    ultimo_granted_job = -1;
    gestor_manejar_release(e, "cpu", 20, 5, 1, cb_granted);
    assert_true(ultimo_granted_job == 21,
                "Con 2 cpu disponibles se sirvió job 21 (cabeza de cola)");
    assert_true(!cola_es_vacia(e->cpu->pendientes),
                "Job 22 sigue esperando (FIFO: 21 fue primero)");

    /* ── Borde 6: release de job inexistente es no-op ── */
    printf("\n--- Borde 6: release de job inexistente ---\n");
    int cpu_antes = e->cpu->disponible;
    gestor_manejar_release(e, "cpu", 9999, 0, 1, cb_granted); /* job 9999 nunca existió */
    assert_true(e->cpu->disponible == cpu_antes,
                "release de job inexistente no altera los recursos");

    /* ── Borde 7: double reserve mismo job_id acumula recursos ── */
    printf("\n--- Borde 7: doble RESERVE del mismo job ---\n");
    EstadoGlobal e2 = estado_crear(8, 0, 0);
    gestor_manejar_reserva(e2, "cpu", 77, 10, 2);
    gestor_manejar_reserva(e2, "cpu", 77, 10, 3); /* mismo job, mismo recurso */
    assert_true(e2->cpu->disponible == 3,
                "doble RESERVE acumula: 8-2-3=3 disponibles");
    gestor_manejar_release(e2, "cpu", 77, 10, 5, NULL); /* liberar todo lo acumulado */
    assert_true(e2->cpu->disponible == 8,
                "release del total acumulado (cpu vuelve a 8)");
    estado_destruir(e2);

    estado_destruir(e);

    /* ══════════════════════════════════════════════════════════════════
     * ESCENARIO 4: Timeout del coordinador (gestor_expirar_peticiones)
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n=== Escenario 4: Timeout del coordinador ===\n\n");

    /* Petición incompleta y vieja → debe expirar y disparar JOB_TIMEOUT */
    EstadoGlobal coord = estado_crear(4, 1, 1024);
    PeticionMulti pet = peticion_crear(7777, 55, 2);   /* espera 2 respuestas */
    pet->instante_creacion = 0;                        /* epoch → vencida */
    pet->respondidos = 1;                              /* incompleta: 1 de 2 */
    pet->nodos[0].fd_remoto = 30; strcpy(pet->nodos[0].recurso, "cpu"); pet->nodos[0].cantidad = 2;
    pet->nodos[1].fd_remoto = 31; strcpy(pet->nodos[1].recurso, "gpu"); pet->nodos[1].cantidad = 1;
    gestor_registrar_peticion(coord, pet);

    ultimo_timeout_peticion = -1;
    gestor_expirar_peticiones(coord, cb_timeout_peticion);
    assert_true(ultimo_timeout_peticion == 7777, "Peticion 7777 vencida -> JOB_TIMEOUT");

    pthread_mutex_lock(&coord->lock);
    PeticionMulti buscada = gestor_buscar_peticion(coord, 7777);
    pthread_mutex_unlock(&coord->lock);
    assert_true(buscada == NULL, "Peticion 7777 eliminada tras el timeout");
    estado_destruir(coord);

    /* Una petición COMPLETA (respondidos == total) no debe expirar */
    EstadoGlobal coord2 = estado_crear(4, 1, 1024);
    PeticionMulti completa = peticion_crear(8888, 55, 1);
    completa->instante_creacion = 0;   /* vieja... */
    completa->respondidos = 1;         /* ...pero completa (1 de 1) */
    gestor_registrar_peticion(coord2, completa);
    ultimo_timeout_peticion = -1;
    gestor_expirar_peticiones(coord2, cb_timeout_peticion);
    assert_true(ultimo_timeout_peticion == -1, "Peticion completa NO se expira aunque sea vieja");
    estado_destruir(coord2);

    /* =================================================================
     * ESCENARIO 5: Liberación por desconexión abrupta
     * Un socket que se cae libera todos los recursos de sus jobs y
     * reparte a los pedidos que estaban encolados.
     * ================================================================= */
    printf("\n=== Escenario 5: Liberación por desconexión abrupta ===\n\n");

    EstadoGlobal ez = estado_crear(4, 1, 0);

    /* Job 100 (socket 42) toma toda la cpu */
    gestor_manejar_reserva(ez, "cpu", 100, 42, 4);
    assert_true(ez->cpu->disponible == 0,
                "Socket 42: job 100 toma cpu:4 -> cpu disponible = 0");

    /* Job 300 (socket 77) pide cpu:2 -> encola (no hay cpu) */
    int r300 = gestor_manejar_reserva(ez, "cpu", 300, 77, 2);
    assert_true(r300 == 0 && !cola_es_vacia(ez->cpu->pendientes),
                "Socket 77: job 300 pide cpu:2 -> encolado");

    /* Se cae el socket 42: libera lo de job 100 y reparte a la cola */
    ultimo_granted_job = -1;
    manejar_desconexion_socket(ez, 42, cb_granted);
    assert_true(ez->cpu->disponible == 2,
                "Desconexión del socket 42 liberó cpu:4 y asignó cpu:2 al job 300 (disponible = 2)");
    assert_true(ultimo_granted_job == 300,
                "El job 300 encolado se sirvió al liberarse los recursos");
    assert_true(cola_es_vacia(ez->cpu->pendientes),
                "Cola cpu vacía tras repartir");

    estado_destruir(ez);

    printf("\nPasados: %d  |  Fallidos: %d\n", pass, fail);
    printf(fail == 0 ? "TODOS LOS TESTS PASARON\n" : "ALGUNOS TESTS FALLARON\n");
    return fail > 0 ? 1 : 0;
}
CTEST

SRCS="gestor_estado.c modelo/estado.c modelo/recursos.c modelo/jobs.c modelo/nodos.c modelo/peticiones.c estructuras/tablahash.c estructuras/glist.c estructuras/cola.c"
gcc -Wall -Wextra -g -I"$SRC/../../include" -I"$SRC" "$BUILD/test_deadlock.c" \
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

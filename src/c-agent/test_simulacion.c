#include <stdio.h>
#include <stdlib.h>
#include "gestor_estado.h"

// -----------------------------------------------------------------------------
// MOCKUPS (Simuladores de red)
// -----------------------------------------------------------------------------
void cb_red(int job_id, int socket_origen) {
    printf("  [RED TCP] ---> Enviando 'GRANTED %d' al socket %d\n", job_id, socket_origen);
}

void cb_timeout(int job_id, int socket_origen) {
    printf("  [RED TCP] ---> Enviando 'TIMEOUT %d' al socket %d\n", job_id, socket_origen);
}

// -----------------------------------------------------------------------------
// OPERATIVO: EL ASEDIO
// -----------------------------------------------------------------------------
int main() {
    printf("========================================================\n");
    printf(" PRUEBA DE ESTRÉS: EL ASEDIO AL NODO CENTRAL\n");
    printf("========================================================\n\n");

    // Inicializamos un reino de Alto Rendimiento (HPC)
    EstadoGlobal estado = estado_crear(8, 2, 16384);
    printf("[SISTEMA] Reino inicializado: CPU(8), GPU(2), MEM(16384)\n\n");

    // --- FASE 1: RESERVAS MÚLTIPLES ---
    printf("--- FASE 1: Job 200 (Socket 15) pide un combo pesado (5 CPU, 1 GPU, 10000 MEM) ---\n");
    
    if (manejar_reserva(estado->cpu, 200, 15, 5)) registrar_asignacion(estado->libro_contable, 200, 15, "cpu", 5);
    if (manejar_reserva(estado->gpu, 200, 15, 1)) registrar_asignacion(estado->libro_contable, 200, 15, "gpu", 1);
    if (manejar_reserva(estado->mem, 200, 15, 10000)) registrar_asignacion(estado->libro_contable, 200, 15, "mem", 10000);
    
    printf("  [ESTADO] CPUs disp: %d | GPUs disp: %d | MEM disp: %d\n", 
           estado->cpu->disponible, estado->gpu->disponible, estado->mem->disponible);


    // --- FASE 2: EL CUELLO DE BOTELLA ---
    printf("\n--- FASE 2: Job 201 (Socket 16) pide 4 CPUs ---\n");
    if (!manejar_reserva(estado->cpu, 201, 16, 4)) {
        printf("  [TESORERO] Rechazado. Se necesitan 4, hay %d. Encolando Job 201.\n", estado->cpu->disponible);
    }

    printf("\n--- FASE 3: Job 202 (Socket 17) pide 2 CPUs (¡Intento de salto de fila!) ---\n");
    if (!manejar_reserva(estado->cpu, 202, 17, 2)) {
        printf("  [TESORERO] Rechazado. Se necesitan 2, hay %d. Encolando Job 202 detrás del 201.\n", estado->cpu->disponible);
    }


    // --- FASE 4: EL GOTEO (Verificando la estrictez de la cola FIFO) ---
    printf("\n--- FASE 4: El Nono (Job 99, Socket 10) devuelve 1 CPU que debía de antes ---\n");
    // Alguien ajeno devuelve 1 CPU. Tenemos 3+1 = 4 CPUs. ¡Suficiente para el Job 201!
    manejar_release(estado->cpu, 1, cb_red);
    printf("  [ESTADO] CPUs disp tras el goteo: %d\n", estado->cpu->disponible);


    // --- FASE 5: LA GRAN TRAGEDIA Y EL DESPERTAR EN CADENA ---
    printf("\n========================================================\n");
    printf(" ¡ALERTA ROJA! El epoll reporta que el Socket 15 ha explotado.\n");
    printf(" (El Job 200 cae y sus inmensos recursos quedan a la deriva)\n");
    printf("========================================================\n\n");
    
    manejar_desconexion_socket(estado, 15, cb_red);

    printf("\n--- ESTADO FINAL DEL REINO DESPUÉS DEL CAOS ---\n");
    printf("CPUs disponibles: %d\n", estado->cpu->disponible);
    printf("GPUs disponibles: %d\n", estado->gpu->disponible);
    printf("MEM disponible  : %d\n", estado->mem->disponible);
    
    // Probando tu lógica de expiración (aunque no haya pasado el tiempo real, 
    // nos aseguramos de que no rompa nada al llamarla)
    printf("\n[SISTEMA] Ejecutando rutina de limpieza de pedidos caducados...\n");
    expirar_pedidos(estado->cpu, cb_timeout);
    expirar_pedidos(estado->gpu, cb_timeout);
    expirar_pedidos(estado->mem, cb_timeout);

    estado_destruir(estado);
    printf("\n[SISTEMA] Memoria liberada. Simulación concluida con éxito rotundo.\n");

    return 0;
}
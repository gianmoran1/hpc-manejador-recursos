#include <stdio.h>
#include <stdlib.h>
#include "gestor_estado.h"

// -----------------------------------------------------------------------------
// MOCKUPS (Simuladores de la capa de red)
// -----------------------------------------------------------------------------
void cb_red(int job_id, int socket_origen) {
    printf("  [RED TCP] ---> Enviando 'GRANTED %d' al socket %d\n", job_id, socket_origen);
}

void cb_timeout(int job_id, int socket_origen) {
    printf("  [RED TCP] ---> Enviando 'TIMEOUT %d' al socket %d\n", job_id, socket_origen);
}

// -----------------------------------------------------------------------------
// OPERATIVO: EL ASEDIO AL REY ABSOLUTO
// -----------------------------------------------------------------------------
int main() {
    printf("========================================================\n");
    printf(" PRUEBA DE ESTRÉS: EL ASEDIO AL NODO CENTRAL\n");
    printf("========================================================\n\n");

    // Inicializamos el reino: 8 CPUs, 2 GPUs, 16384 MB RAM
    EstadoGlobal estado = estado_crear(8, 2, 16384);
    printf("[SISTEMA] Reino inicializado: CPU(8), GPU(2), MEM(16384)\n\n");

    // --- FASE 1: RESERVAS MÚLTIPLES ---
    printf("--- FASE 1: Job 200 (Socket 15) pide un combo pesado (5 CPU, 1 GPU, 10000 MEM) ---\n");
    
    // Llamadas limpias a la central de mando
    gestor_manejar_reserva(estado, "cpu", 200, 15, 5);
    gestor_manejar_reserva(estado, "gpu", 200, 15, 1);
    gestor_manejar_reserva(estado, "mem", 200, 15, 10000);
    
    printf("  [ESTADO] CPUs disp: %d | GPUs disp: %d | MEM disp: %d\n", 
           estado->cpu->disponible, estado->gpu->disponible, estado->mem->disponible);


    // --- FASE 2: EL CUELLO DE BOTELLA ---
    printf("\n--- FASE 2: Job 201 (Socket 16) pide 4 CPUs ---\n");
    int res_201 = gestor_manejar_reserva(estado, "cpu", 201, 16, 4);
    if (res_201 == 0) {
        printf("  [TESORERO] Rechazado. Se necesitan 4, hay %d. Encolando Job 201.\n", estado->cpu->disponible);
    }

    // --- FASE 3: INTENTO DE SALTO DE FILA ---
    printf("\n--- FASE 3: Job 202 (Socket 17) pide 2 CPUs (¡Intento de salto de fila!) ---\n");
    int res_202 = gestor_manejar_reserva(estado, "cpu", 202, 17, 2);
    if (res_202 == 0) {
        printf("  [TESORERO] Rechazado. Se necesitan 2, hay %d. Encolando Job 202 detrás del 201.\n", estado->cpu->disponible);
    }

    // --- FASE 4: EL GOTEO (Comprobando la estrictez FIFO) ---
    printf("\n--- FASE 4: El Nono (Job 99, Socket 10) devuelve 1 CPU ajena ---\n");
    // Esto hará que las CPUs suban a 4, despertando automáticamente al Job 201
    gestor_manejar_release(estado, "cpu", 99, 1, cb_red);
    printf("  [ESTADO] CPUs disp tras el goteo: %d\n", estado->cpu->disponible);

    // --- FASE 5: LA TRAGEDIA Y EL EMBARGO ---
    printf("\n========================================================\n");
    printf(" ¡ALERTA ROJA! El epoll reporta que el Socket 15 ha explotado.\n");
    printf(" (El Job 200 cae y sus inmensos recursos quedan a la deriva)\n");
    printf("========================================================\n\n");
    
    // Aquí el nombre correcto de la función según tu .h
    manejar_desconexion_socket(estado, 15, cb_red);

    printf("\n--- ESTADO FINAL DEL REINO DESPUÉS DEL CAOS ---\n");
    printf("CPUs disponibles: %d\n", estado->cpu->disponible);
    printf("GPUs disponibles: %d\n", estado->gpu->disponible);
    printf("MEM disponible  : %d\n", estado->mem->disponible);
    
    // Probamos la expiración masiva
    printf("\n[SISTEMA] Ejecutando rutina de limpieza de pedidos caducados...\n");
    gestor_expirar_pedidos(estado, cb_timeout);

    estado_destruir(estado);
    printf("\n[SISTEMA] Memoria liberada. Simulación concluida con éxito rotundo.\n");

    return 0;
}
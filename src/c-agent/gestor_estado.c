#include "gestor_estado.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Variables estáticas para pasar contexto al callback ciego.
// ¡Esto es seguro únicamente porque el epoll atiende eventos de a uno por vez!
//en caso de hacer hilos, hay que implementar un mecanismo de sincronización para que no se pisen entre sí.
static EstadoGlobal estado_actual = NULL;
static FuncionAviso aviso_red_actual = NULL;

/* * El Traductor: Recibe la orden de jobs.c ("cpu", 2) y la redirige a recursos.c
 */
static void callback_tragedia(char* nombre_recurso, int cantidad) {
    if (strcmp(nombre_recurso, "cpu") == 0) {
        manejar_release(estado_actual->cpu, cantidad, aviso_red_actual);
    } else if (strcmp(nombre_recurso, "gpu") == 0) {
        manejar_release(estado_actual->gpu, cantidad, aviso_red_actual);
    } else if (strcmp(nombre_recurso, "mem") == 0) {
        manejar_release(estado_actual->mem, cantidad, aviso_red_actual);
    }
}

// -----------------------------------------------------------------------------

EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem) {
    EstadoGlobal e = malloc(sizeof(struct estadoGlobal_));
    e->cpu = recurso_crear("cpu", cap_cpu);
    e->gpu = recurso_crear("gpu", cap_gpu);
    e->mem = recurso_crear("mem", cap_mem);
    e->libro_contable = crear_tabla_jobs();
    return e;
}

void estado_destruir(EstadoGlobal estado) {
    recurso_destruir(estado->cpu);
    recurso_destruir(estado->gpu);
    recurso_destruir(estado->mem);
    destruir_tabla_jobs(estado->libro_contable);
    free(estado);
}

void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, FuncionAviso avisar_red) {
    // 1. Preparamos el escenario para la traducción
    estado_actual = estado;
    aviso_red_actual = avisar_red; 
    
    printf("\n[ALERTA] El socket %d ha muerto. Ejecutando protocolo de embargo...\n", socket_caido);
    
    // 2. Ejecutamos la orden al libro contable. 
    // Éste llamará internamente a 'callback_tragedia' por cada recurso embargado,
    // devolviendo el oro a las arcas y avisando a la red si otros trabajos se despiertan.
    liberar_recursos_socket(estado->libro_contable, socket_caido, callback_tragedia);
    
    // 3. Limpiamos el escenario
    estado_actual = NULL;
    aviso_red_actual = NULL;
    printf("[ÉXITO] Recursos del socket %d recuperados y reasignados.\n\n", socket_caido);
}
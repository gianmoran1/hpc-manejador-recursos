#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "controlador.h"
#include "Agente.h"       // Para el epoll global y erlangSocket
#include "Sockets.h"      // Para enviar_mensaje_tcp y conectar_a_nodo
#include "gestor_estado.h"// La magia de tu compañero Santos

// =========================================================================
// INTERFAZ CON ERLANG (Comunicación Local)
// =========================================================================
void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg) {
    char comando[32];
    int job_id = -1;
    char recursos[128];

    // Desarmamos el string de forma segura. 
    // Devuelve cuántas variables logró llenar exitosamente.
    int parseados = sscanf(msg, "%31s %d %127s", comando, &job_id, recursos);

    if (parseados >= 1) {

        if (strcmp(comando, "JOB_REQUEST") == 0 && parseados == 3) {
            printf("[CONTROLADOR] Erlang pide: Trabajo %d, Recursos: %s\n", job_id, recursos);
            

            // 1. INTERACCIÓN CON SANTOS: Buscamos quién tiene esto
            char ip_destino[16];
            // santos_obtener_ip_para_recursos(recursos, ip_destino);

            // MOCK TEMPORAL (Borrar cuando Santos tenga su función)
            strcpy(ip_destino, "192.168.0.50"); 

            if (ip_destino[0] == '\0') {
                printf("[CONTROLADOR] No hay nodos con los recursos %s en la red.\n", recursos);
                char error[64];
                snprintf(error, sizeof(error), "JOB_STATUS %d FAILED\n", job_id);
                enviar_mensaje_tcp(cliente->fd, error);
                return;
            }

            // 2. INTERACCIÓN CON RED: Buscamos si ya tenemos el enchufe abierto
            // int fd_destino = santos_obtener_fd_por_ip(ip_destino);
            int fd_destino = -1; // MOCK

            if (fd_destino == -1) {
                // No hay conexión previa. Usamos tu plomería de Sockets.c
                fd_destino = conectar_a_nodo(ip_destino, PUERTO_TCP);

                if (fd_destino != -1) {
                    // ¡Vital! Lo registramos en tu epoll para escuchar la respuesta
                    ClienteConectado *nuevo_nodo = crear_cliente_conectado(fd_destino, 0); // 0 = Red C
                    agregar_cliente_en_epoll(nuevo_nodo, EPOLLIN | EPOLLONESHOT);

                    // Avisamos a Santos que guarde este nuevo tubo en su libreta
                    // santos_registrar_fd_para_ip(ip_destino, fd_destino);
                }
            }

            // 3. ENVÍO DE LA ORDEN
            if (fd_destino != -1) {
                char msj_red[256];
                snprintf(msj_red, sizeof(msj_red), "RESERVE %d %s\n", job_id, recursos);
                enviar_mensaje_tcp(fd_destino, msj_red);
            } else {
                // El nodo al que intentamos conectar está muerto
                char msj_error[64];
                snprintf(msj_error, sizeof(msj_error), "JOB_DENIED %d\n", job_id);
                // ARMAR EL DENIED 
                enviar_mensaje_tcp(cliente->fd, msj_error);
            }
        }
        else {
            printf("[CONTROLADOR] Comando de Erlang no reconocido o mal formado: %s\n", msg);
        }
    }
}


// =========================================================================
// INTERFAZ CON RED C (Comunicación Externa)
// =========================================================================
void procesar_mensaje_red_c(ClienteConectado *cliente, char* msg) {
    char comando[32];
    int job_id = -1;
    char recursos[128];

    int parseados = sscanf(msg, "%31s %d %127s", comando, &job_id, recursos);

    if (parseados >= 1) {

        // CASO 1: Otro agente nos pide reservar recursos nuestros
        if (strcmp(comando, "RESERVE") == 0 && parseados == 3) {
            printf("[CONTROLADOR] El FD %d intenta reservar localmente %s para el trabajo %d\n", cliente->fd, recursos, job_id);

            // Preguntamos a Santos si la compu física tiene esto disponible
            // int lugar_disponible = santos_intentar_reservar_local(recursos);
            int lugar_disponible = 1; // MOCK

            char respuesta[64];
            if (lugar_disponible) {
                snprintf(respuesta, sizeof(respuesta), "GRANTED %d\n", job_id);
            } else {
                snprintf(respuesta, sizeof(respuesta), "DENIED %d\n", job_id);
            }

            // Le respondemos directo por el mismo cable por el que nos habló
            enviar_mensaje_tcp(cliente->fd, respuesta);
        }
        
        // CASO 2: Un agente nos responde a un RESERVE que nosotros le mandamos antes
        else if (strcmp(comando, "GRANTED") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] ¡Reserva exitosa en otro nodo para el trabajo %d!\n", job_id);
            
            // Avisamos a Erlang (usamos la variable global erlangSocket que expusimos en Agente.h)
            if (erlangSocket != -1) {
                char msj_ok[64];
                snprintf(msj_ok, sizeof(msj_ok), "JOB_STATUS %d OK\n", job_id);
                enviar_mensaje_tcp(erlangSocket, msj_ok);
            }
        }

        else if (strcmp(comando, "DENIED") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] Reserva rechazada por otro nodo para el trabajo %d.\n", job_id);
            
            if (erlangSocket != -1) {
                char msj_fail[64];
                snprintf(msj_fail, sizeof(msj_fail), "JOB_STATUS %d FAILED\n", job_id);
                enviar_mensaje_tcp(erlangSocket, msj_fail);
            }
        }
        
        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\n", msg);
        }
    }
}
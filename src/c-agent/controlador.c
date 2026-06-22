#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "controlador.h"
#include "Agente.h"       // Para el epoll global y erlangSocket
#include "Sockets.h"      // Para enviar_mensaje_tcp y conectar_a_nodo
#include "gestor_estado.h"// La magia de tu compañero Santos

// Estado global definido en Agente.c
extern EstadoGlobal estado;

// Callback que usa el gestor para notificar a un cliente que su job pendiente fue concedido
// static void callback_job_granted(int job_id, int socket_fd) {
//     char msj[64];
//     snprintf(msj, sizeof(msj), "JOB_GRANTED %d\n", job_id);
//     enviar_mensaje_tcp(socket_fd, msj);
// }

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

        // Erlang pide la tabla de nodos conocidos para calcular los recursos del cluster
        if (strcmp(comando, "GET_NODES") == 0 && parseados == 1) {
            char *nodos = gestor_get_nodes(estado);
            if (nodos != NULL) {
                // Extendemos el buffer una posición para agregar el \n y enviar todo en un solo send
                // size_t len = strlen(nodos);
                enviar_mensaje_tcp(cliente->fd, nodos);
                free(nodos);
            }
            printf("nodos enviados a erlang");

            return;
        }
        // Erlang envía: JOB_REQUEST <id> @IP:recurso:cant [@IP2:recurso2:cant2 ...]
        // Cada token describe un pedazo del trabajo en un nodo concreto de la red
        if (strcmp(comando, "JOB_REQUEST") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] Erlang pide trabajo %d\n", job_id);

            // Avanzamos el cursor hasta el primer token @IP:..., saltando "JOB_REQUEST <id> "
            const char *cursor = msg;
            while (*cursor && *cursor != ' ') cursor++; // salta "JOB_REQUEST"
            while (*cursor == ' ') cursor++;             // salta espacios
            while (*cursor && *cursor != ' ') cursor++; // salta el job_id
            while (*cursor == ' ') cursor++;             // queda apuntando al primer token

            char token[128];
            while (sscanf(cursor, "%127s", token) == 1) {
                char ip_destino[50], nombre_recurso[32];
                int cantidad;

                if (sscanf(token, "@%49[^:]:%31[^:]:%d", ip_destino, nombre_recurso, &cantidad) != 3) {
                    printf("[CONTROLADOR] Token mal formado en JOB_REQUEST: '%s'\n", token);
                    char msj_fmt[64];
                    snprintf(msj_fmt, sizeof(msj_fmt), "JOB_DENIED %d\n", job_id);
                    enviar_mensaje_tcp(cliente->fd, msj_fmt);
                    return;
                }

                // Intentamos reutilizar una conexión existente; si no, abrimos una nueva
                // int fd_destino = santos_obtener_fd_por_ip(ip_destino); (gestor_estado)
                int fd_destino = -1; // MOCK: reemplazar con lookup en tabla de nodos
                // Maxi: Creo que el puerto es dinamico, tambien lo devolveria la funcion de santos lookup en tabla de nodos, ya que la constante PUERTO_TCP es nustro puerto y no el del otro nodo
                if (fd_destino == -1) {
                    fd_destino = conectar_a_nodo(ip_destino, PUERTO_TCP);
                    if (fd_destino != -1) {
                        ClienteConectado *nuevo_nodo = crear_cliente_conectado(fd_destino, 0);
                        agregar_cliente_en_epoll(nuevo_nodo, EPOLLIN | EPOLLONESHOT);
                        // santos_registrar_fd_para_ip(ip_destino, fd_destino); (gestor_estado)
                    }
                }

                if (fd_destino != -1) {
                    // Le pedimos al nodo remoto que reserve su porción del trabajo
                    char msj_red[128];
                    snprintf(msj_red, sizeof(msj_red), "RESERVE %d %s %d\n", job_id, nombre_recurso, cantidad);
                    enviar_mensaje_tcp(fd_destino, msj_red);
                } else {
                    // No se pudo alcanzar el nodo; cancelamos todo el trabajo
                    char msj_error[64];
                    snprintf(msj_error, sizeof(msj_error), "JOB_DENIED %d\n", job_id);
                    enviar_mensaje_tcp(cliente->fd, msj_error);
                    return;
                }

                // Avanzamos al siguiente token
                while (*cursor && *cursor != ' ') cursor++;
                while (*cursor == ' ') cursor++;
            }
        }
        // Erlang avisa que el trabajo terminó: liberamos los recursos locales
        // y drenamos la cola de pendientes vía callback_job_granted
        else if (strcmp(comando, "JOB_RELEASE") == 0 && parseados == 2) {
            // gestor_liberar_job(estado, job_id, callback_job_granted);
            printf("liberar %d", job_id);
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

            // Preguntamos a Santos (gestor_estado) si la compu física tiene esto disponible
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
                snprintf(msj_ok, sizeof(msj_ok), "JOB_GRANTED %d\n", job_id);
                enviar_mensaje_tcp(erlangSocket, msj_ok);
            }
        }

        else if (strcmp(comando, "DENIED") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] Reserva rechazada por otro nodo para el trabajo %d.\n", job_id);
            
            if (erlangSocket != -1) {
                char msj_fail[64];
                snprintf(msj_fail, sizeof(msj_fail), "JOB_DENIED %d\n", job_id);
                enviar_mensaje_tcp(erlangSocket, msj_fail);
            }
        }
        
        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\n", msg);
        }
    }
}
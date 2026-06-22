#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "controlador.h"
#include "Agente.h"        // Para el epoll global y erlangSocket
#include "Sockets.h"       // Para enviar_mensaje_tcp y conectar_a_nodo
#include "gestor_estado.h" // La magia de tu compañero Santos
#include "transacciones.h" // PeticionMulti para rollback de JOB_REQUEST

// Estado global definido en Agente.c
extern EstadoGlobal estado;

// Callback C-C: notifica a un nodo coordinador que su RESERVE encolado fue concedido.
// Usa "GRANTED" (protocolo C-C), no "JOB_GRANTED" (que es solo para Erlang).
void callback_granted_red(int job_id, int socket_fd) {
    char msj[64];
    snprintf(msj, sizeof(msj), "GRANTED %d\n", job_id);
    enviar_mensaje_tcp(socket_fd, msj);
}

// ── Helper: cancela todos los RESERVE ya enviados y notifica JOB_DENIED ─────
// Debe llamarse sin tener el lock tomado.
static void rollback_y_denegar(int job_id, int socket_erlang, int n_enviados) {
    int fds[16]; int n = 0;

    pthread_mutex_lock(&estado->lock);
    PeticionMulti p = gestor_buscar_peticion(estado, job_id);
    if (p) {
        for (int i = 0; i < n_enviados; i++)
            fds[n++] = p->nodos[i].fd_remoto;
        gestor_eliminar_peticion(estado, job_id);
    }
    pthread_mutex_unlock(&estado->lock);

    for (int i = 0; i < n; i++) {
        char msj[64];
        snprintf(msj, sizeof(msj), "RELEASE %d\n", job_id);
        enviar_mensaje_tcp(fds[i], msj);
    }
    char msj[64];
    snprintf(msj, sizeof(msj), "JOB_DENIED %d\n", job_id);
    enviar_mensaje_tcp(socket_erlang, msj);
}

// =========================================================================
// INTERFAZ CON ERLANG (Comunicación Local)
// =========================================================================
void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg) {
    char comando[32];
    int job_id = -1;
    char recursos[128];

    int parseados = sscanf(msg, "%31s %d %127s", comando, &job_id, recursos);

    if (parseados >= 1) {

        // Erlang pide la tabla de nodos conocidos para calcular los recursos del cluster
        if (strcmp(comando, "GET_NODES") == 0 && parseados == 1) {
            char *nodos = gestor_get_nodes(estado);
            if (nodos != NULL) {
                enviar_mensaje_tcp(cliente->fd, nodos);
                free(nodos);
            }
            printf("nodos enviados a erlang");
            return;
        }

        // Erlang envía: JOB_REQUEST <id> @IP:recurso:cant [@IP2:recurso2:cant2 ...]
        if (strcmp(comando, "JOB_REQUEST") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] Erlang pide trabajo %d\n", job_id);

            // Avanzamos el cursor hasta el primer token @IP:...
            const char *cursor = msg;
            while (*cursor && *cursor != ' ') cursor++;
            while (*cursor == ' ') cursor++;
            while (*cursor && *cursor != ' ') cursor++;
            while (*cursor == ' ') cursor++;

            // ── Pasada 1: contar tokens para saber cuántos RESERVE vamos a enviar ──
            int total = 0;
            {
                const char *tmp = cursor;
                char tok[128];
                while (sscanf(tmp, "%127s", tok) == 1) {
                    if (tok[0] == '@') total++;
                    while (*tmp && *tmp != ' ') tmp++;
                    while (*tmp == ' ') tmp++;
                }
            }
            if (total == 0) {
                printf("[CONTROLADOR] JOB_REQUEST %d sin tokens válidos\n", job_id);
                char msj[64];
                snprintf(msj, sizeof(msj), "JOB_DENIED %d\n", job_id);
                enviar_mensaje_tcp(cliente->fd, msj);
                return;
            }

            // ── Crear y registrar la peticion ANTES de enviar los RESERVE ──
            PeticionMulti peticion = peticion_crear(job_id, cliente->fd, total);
            gestor_registrar_peticion(estado, peticion);

            // ── Pasada 2: conectar y enviar RESERVE a cada nodo ──
            int idx = 0;
            char token[128];
            while (sscanf(cursor, "%127s", token) == 1) {
                char ip_destino[50], nombre_recurso[32];
                int cantidad;

                if (sscanf(token, "@%49[^:]:%31[^:]:%d",
                           ip_destino, nombre_recurso, &cantidad) != 3) {
                    printf("[CONTROLADOR] Token mal formado en JOB_REQUEST: '%s'\n", token);
                    rollback_y_denegar(job_id, cliente->fd, idx);
                    return;
                }

                // Reutilizar conexión existente al nodo; si no hay, abrir una nueva y registrarla.
                ClienteConectado *cx = nodo_obtener_conexion(ip_destino, PUERTO_TCP,
                                                             estado->registro_nodos);
                int fd_destino;
                if (cx != NULL) {
                    fd_destino = cx->fd;
                } else {
                    fd_destino = conectar_a_nodo(ip_destino, PUERTO_TCP);
                    if (fd_destino != -1) {
                        ClienteConectado *nueva = crear_cliente_conectado(fd_destino, 0);
                        agregar_cliente_en_epoll(nueva, EPOLLIN | EPOLLONESHOT);
                        nodo_registrar_conexion(ip_destino, PUERTO_TCP, nueva,
                                                estado->registro_nodos);
                    }
                }

                if (fd_destino == -1) {
                    printf("[CONTROLADOR] No se pudo conectar a %s para job %d\n",
                           ip_destino, job_id);
                    rollback_y_denegar(job_id, cliente->fd, idx);
                    return;
                }

                // Registrar el nodo en la peticion y enviar RESERVE
                peticion->nodos[idx].fd_remoto = fd_destino;
                strncpy(peticion->nodos[idx].recurso, nombre_recurso,
                        sizeof(peticion->nodos[idx].recurso) - 1);
                peticion->nodos[idx].cantidad = cantidad;
                peticion->nodos[idx].grantado = 0;
                idx++;

                char msj_red[128];
                snprintf(msj_red, sizeof(msj_red), "RESERVE %d %s:%d\n",
                         job_id, nombre_recurso, cantidad);
                enviar_mensaje_tcp(fd_destino, msj_red);

                while (*cursor && *cursor != ' ') cursor++;
                while (*cursor == ' ') cursor++;
            }
        }

        // Erlang avisa que el trabajo terminó: liberamos recursos en nodos remotos y locales
        else if (strcmp(comando, "JOB_RELEASE") == 0 && parseados == 2) {
            // 1. Enviar RELEASE a todos los nodos remotos de esta peticion
            int fds[16]; int n = 0;

            pthread_mutex_lock(&estado->lock);
            PeticionMulti p = gestor_buscar_peticion(estado, job_id);
            if (p) {
                for (int i = 0; i < p->total; i++)
                    fds[n++] = p->nodos[i].fd_remoto;
                gestor_eliminar_peticion(estado, job_id);
            }
            pthread_mutex_unlock(&estado->lock);

            for (int i = 0; i < n; i++) {
                char msj[64];
                snprintf(msj, sizeof(msj), "RELEASE %d\n", job_id);
                enviar_mensaje_tcp(fds[i], msj);
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
            printf("[CONTROLADOR] El FD %d intenta reservar localmente %s para el trabajo %d\n",
                   cliente->fd, recursos, job_id);

            char nombre_res[32]; int cant_res = 0; int resultado_res = -1;
            if (sscanf(recursos, "%31[^:]:%d", nombre_res, &cant_res) == 2)
                resultado_res = gestor_manejar_reserva(estado, nombre_res, job_id, cliente->fd, cant_res);

            if (resultado_res == 1) {
                char rsp[64];
                snprintf(rsp, sizeof(rsp), "GRANTED %d\n", job_id);
                enviar_mensaje_tcp(cliente->fd, rsp);
            } else if (resultado_res == -1) {
                char rsp[64];
                snprintf(rsp, sizeof(rsp), "DENIED %d\n", job_id);
                enviar_mensaje_tcp(cliente->fd, rsp);
            }
            // resultado_res == 0: encolado; callback avisará cuando haya recursos
        }

        // CASO 2: Un agente nos responde a un RESERVE que nosotros le mandamos antes
        else if (strcmp(comando, "GRANTED") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] ¡Reserva exitosa en otro nodo para el trabajo %d!\n", job_id);

            int enviar_granted = 0, skt_erlang = -1;

            pthread_mutex_lock(&estado->lock);
            PeticionMulti p = gestor_buscar_peticion(estado, job_id);
            if (p) {
                NodoReserva *nr = peticion_buscar_nodo_por_fd(p, cliente->fd);
                if (nr) nr->grantado = 1;
                p->respondidos++;
                if (p->respondidos == p->total) {
                    enviar_granted = 1;
                    skt_erlang = p->socket_erlang;
                    // No destruir: la peticion se necesita hasta JOB_RELEASE
                }
            }
            pthread_mutex_unlock(&estado->lock);

            if (enviar_granted) {
                char msj[64];
                snprintf(msj, sizeof(msj), "JOB_GRANTED %d\n", job_id);
                enviar_mensaje_tcp(skt_erlang, msj);
            }
        }

        // CASO 3: Un agente rechazó nuestro RESERVE → rollback de todos los nodos
        else if (strcmp(comando, "DENIED") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] Reserva rechazada por otro nodo para el trabajo %d.\n", job_id);

            int fds[16]; int n = 0; int skt_erlang = -1;

            pthread_mutex_lock(&estado->lock);
            PeticionMulti p = gestor_buscar_peticion(estado, job_id);
            if (p) {
                for (int i = 0; i < p->total; i++)
                    fds[n++] = p->nodos[i].fd_remoto;
                skt_erlang = p->socket_erlang;
                gestor_eliminar_peticion(estado, job_id);
            }
            pthread_mutex_unlock(&estado->lock);

            // RELEASE a TODOS los nodos (no sabemos cuáles llegaron a GRANTED)
            for (int i = 0; i < n; i++) {
                char msj[64];
                snprintf(msj, sizeof(msj), "RELEASE %d\n", job_id);
                enviar_mensaje_tcp(fds[i], msj);
            }
            if (skt_erlang != -1) {
                char msj[64];
                snprintf(msj, sizeof(msj), "JOB_DENIED %d\n", job_id);
                enviar_mensaje_tcp(skt_erlang, msj);
            }
        }

        // CASO 4: Otro agente nos pide que liberemos un job (rollback remoto)
        else if (strcmp(comando, "RELEASE") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] RELEASE recibido para job %d, liberando recursos locales.\n", job_id);
            gestor_liberar_job(estado, job_id, callback_granted_red);
        }

        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\n", msg);
        }
    }
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "config.h"
#include "controlador.h"
#include "servidor.h"      // globals del servidor: estado, usock_udp, timers
#include "red/sockets.h"   // enviar/atender_* y conectar_a_nodo
#include "gestor_estado.h" // implementacion del gestor de recursos
#include "modelo/transacciones.h" // PeticionMulti para rollback de JOB_REQUEST

EstadoGlobal estado = NULL;

void controlador_anuncio_recibido(char* buffer_udp) {
    char ip_node[50], cmd[32];
    int puerto_node = 0, cpu = 0, gpu = 0, mem = 0;
    int n = sscanf(buffer_udp, "%49s %31s %d", ip_node, cmd, &puerto_node);
    if (n == 3 && strcmp(cmd, "ANNOUNCE") == 0) {
        // Avanzar el cursor hasta los tokens de recursos
        const char *ptr = buffer_udp;
        for (int i = 0; i < 3; i++) {
            while (*ptr && *ptr != ' ') ptr++;
            while (*ptr == ' ') ptr++;
        }
        char tok[64];
        while (sscanf(ptr, "%63s", tok) == 1) {
            char res[16]; int val;
            if (sscanf(tok, "%15[^:]:%d", res, &val) == 2) {
                if      (strcmp(res, "cpu") == 0) cpu = val;
                else if (strcmp(res, "gpu") == 0) gpu = val;
                else if (strcmp(res, "mem") == 0) mem = val;
            }
            while (*ptr && *ptr != ' ') ptr++;
            while (*ptr == ' ') ptr++;
        }
        gestor_procesar_anuncio(estado, ip_node, puerto_node, cpu, gpu, mem);
    }
}

void controlador_anuncio_recursos() {
    uint64_t _expiraciones;
    read(timer_anuncios_fd, &_expiraciones, sizeof(_expiraciones));

    int disp_cpu, disp_gpu, disp_mem;
    gestor_recursos_disponibles(estado, &disp_cpu, &disp_gpu, &disp_mem);

    char msj[TAM_BUFFER_ANUNCIO];
    snprintf(msj, sizeof(msj), "ANNOUNCE %d cpu:%d gpu:%d mem:%d\n",
            PUERTO_TCP, disp_cpu, disp_gpu, disp_mem);
    enviar_mensaje_udp(usock_udp, IP_BROADCAST, PUERTO_UDP, msj);
}





void controlador_desconexion_cliente(int cliente_fd) {
    manejar_desconexion_socket(estado, cliente_fd, callback_granted_red);
    // Evitar que nodo_destruir intente close+free de un ClienteConectado
    // que ya vamos a liberar nosotros justo debajo.
    nodo_limpiar_conexion_por_fd(cliente_fd, estado->registro_nodos);
}

void controlador_mensaje_cliente(ClienteConectado *cliente) {
    char *salto_linea;

    while ((salto_linea = strchr(cliente->buffer, '\n')) != NULL) {
        // Cortamos el mensaje limpio hasta el '\n'
        int longitud_mensaje = salto_linea - cliente->buffer;
        char mensaje_limpio[TAM_BUFFER_MENSAJE];
        strncpy(mensaje_limpio, cliente->buffer, longitud_mensaje);
        mensaje_limpio[longitud_mensaje] = '\0';

        if (cliente->es_erlang)
            procesar_mensaje_erlang(cliente, mensaje_limpio);
        else
            procesar_mensaje_red_c(cliente, mensaje_limpio);

        // Movemos lo que sobró al principio del buffer
        int bytes_restantes = cliente->bytes_leidos - (longitud_mensaje + 1);
        memmove(cliente->buffer, salto_linea + 1, bytes_restantes);
        cliente->bytes_leidos = bytes_restantes;
        cliente->buffer[cliente->bytes_leidos] = '\0';
    }
}







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
    int fds[16]; char recursos_rb[16][32]; int cantidades_rb[16]; int n = 0;

    pthread_mutex_lock(&estado->lock);
    PeticionMulti p = gestor_buscar_peticion(estado, job_id);
    if (p) {
        for (int i = 0; i < n_enviados; i++) {
            fds[n] = p->nodos[i].fd_remoto;
            strncpy(recursos_rb[n], p->nodos[i].recurso, 31);
            recursos_rb[n][31] = '\0';
            cantidades_rb[n] = p->nodos[i].cantidad;
            n++;
        }
        gestor_eliminar_peticion(estado, job_id);
    }
    pthread_mutex_unlock(&estado->lock);

    for (int i = 0; i < n; i++) {
        char msj[64];
        snprintf(msj, sizeof(msj), "RELEASE %d %s %d\n", job_id, recursos_rb[i], cantidades_rb[i]);
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
            // printf("nodos enviados a erlang\n");
            return;
        }

        // Erlang envía: JOB_REQUEST <id> @IP:recurso:cant [@IP2:recurso2:cant2 ...]
        if (strcmp(comando, "JOB_REQUEST") == 0 && parseados >= 2) {
            printf("[CONTROLADOR] Erlang pide RECURSOS: %s para el trabajo %d\n", recursos, job_id);

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
                Nodo nodo = gestor_buscar_nodo_por_ip(ip_destino, estado);
                int puerto_destino = nodo->puerto;
                // Reutilizar conexión existente al nodo; si no hay, abrir una nueva y registrarla.
                ClienteConectado *cx = nodo_obtener_conexion(ip_destino, puerto_destino,
                                                             estado->registro_nodos);
                int fd_destino;
                if (cx != NULL) {
                    fd_destino = cx->fd;
                } else {
                    printf("tratando de conectar\n");
                    fd_destino = conectar_a_nodo(ip_destino, puerto_destino);
                    if (fd_destino != -1) {
                        ClienteConectado *nueva = crear_cliente_conectado(fd_destino, 0);
                        servidor_agregar_cliente_en_epoll(nueva, EPOLLIN | EPOLLONESHOT);
                        nodo_registrar_conexion(ip_destino, puerto_destino, nueva,
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
                snprintf(msj_red, sizeof(msj_red), "RESERVE %d %s %d\n",
                         job_id, nombre_recurso, cantidad);
                enviar_mensaje_tcp(fd_destino, msj_red);

                while (*cursor && *cursor != ' ') cursor++;
                while (*cursor == ' ') cursor++;
            }
        }

        // Erlang avisa que el trabajo terminó: liberamos recursos en nodos remotos y locales
        else if (strcmp(comando, "JOB_RELEASE") == 0 && parseados == 2) {
            // 1. Enviar RELEASE a todos los nodos remotos de esta peticion
            int fds[16]; char recursos_jb[16][32]; int cantidades_jb[16]; int n = 0;
            pthread_mutex_lock(&estado->lock);
            PeticionMulti p = gestor_buscar_peticion(estado, job_id);
            if (p) {
                for (int i = 0; i < p->total; i++) {
                    fds[n] = p->nodos[i].fd_remoto;
                    strncpy(recursos_jb[n], p->nodos[i].recurso, 31);
                    recursos_jb[n][31] = '\0';
                    cantidades_jb[n] = p->nodos[i].cantidad;
                    n++;
                }
                gestor_eliminar_peticion(estado, job_id);
            }
            pthread_mutex_unlock(&estado->lock);

            for (int i = 0; i < n; i++) {
                char msj[64];
                snprintf(msj, sizeof(msj), "RELEASE %d %s %d\n", job_id, recursos_jb[i], cantidades_jb[i]);
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
    char recurso[32];
    int cant_res = 0;
    int parseados = sscanf(msg, "%31s %d %31s %d", comando, &job_id, recurso, &cant_res);

    if (parseados >= 1) {

        // CASO 1: Otro agente nos pide reservar recursos nuestros
        if (strcmp(comando, "RESERVE") == 0 && parseados == 4) {
            printf("[CONTROLADOR] El FD %d intenta reservar localmente %s %d para el trabajo %d\n",
                   cliente->fd, recurso, cant_res, job_id);
            // CHEQUEAR PORQUE NO RESERVA BIEN CUANDO RECIBE DE OTRO NODO, DEVUELVE SIEMPRE DENIED
            // char nombre_res[32]; int cant_res = 0; int resultado_res = -1;
            // if (sscanf(recursos, "%31[^:]:%d", nombre_res, &cant_res) == 2)
            int resultado_res = -1;
            resultado_res = gestor_manejar_reserva(estado, recurso, job_id, cliente->fd, cant_res);

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

            int fds[16]; char recursos_dn[16][32]; int cantidades_dn[16]; int n = 0; int skt_erlang = -1;

            pthread_mutex_lock(&estado->lock);
            PeticionMulti p = gestor_buscar_peticion(estado, job_id);
            if (p) {
                for (int i = 0; i < p->total; i++) {
                    fds[n] = p->nodos[i].fd_remoto;
                    strncpy(recursos_dn[n], p->nodos[i].recurso, 31);
                    recursos_dn[n][31] = '\0';
                    cantidades_dn[n] = p->nodos[i].cantidad;
                    n++;
                }
                skt_erlang = p->socket_erlang;
                gestor_eliminar_peticion(estado, job_id);
            }
            pthread_mutex_unlock(&estado->lock);

            // RELEASE a TODOS los nodos (no sabemos cuáles llegaron a GRANTED)
            for (int i = 0; i < n; i++) {
                char msj[64];
                snprintf(msj, sizeof(msj), "RELEASE %d %s %d\n", job_id, recursos_dn[i], cantidades_dn[i]);
                enviar_mensaje_tcp(fds[i], msj);
            }
            if (skt_erlang != -1) {
                char msj[64];
                snprintf(msj, sizeof(msj), "JOB_DENIED %d\n", job_id);
                enviar_mensaje_tcp(skt_erlang, msj);
            }
        }

        // CASO 4: Otro agente nos pide que liberemos recursos específicos de un job
        else if (strcmp(comando, "RELEASE") == 0 && parseados == 4) {
            // char nombre_res[32]; int cant_res = 0;
            // if (sscanf(recurso, "%31[^:]:%d", nombre_res, &cant_res) == 2) {
                printf("[CONTROLADOR] RELEASE recibido para job %d: %s %d\n", job_id, recurso, cant_res);
                gestor_manejar_release(estado, recurso, job_id, cant_res, callback_granted_red);
            // } else {
            //     printf("[CONTROLADOR] RELEASE mal formado: %s\n", msg);
            // }
        }

        else {
            printf("[CONTROLADOR] Comando de red C ignorado o mal formado: %s\n", msg);
        }
    }
}

// =========================================================================
// HANDLERS DEL LOOP DE EVENTOS (invocados desde bucle_principal en servidor.c)
// =========================================================================

// Callback de timeout: cuando un RESERVE lleva más de TIEMPO_ESPERA_RESERVA
// encolado sin resolverse, avisamos DENIED al coordinador que lo originó para
// que haga rollback y notifique JOB_DENIED a Erlang.
static void notificar_denied_timeout(int job_id, int socket_fd) {
    char msj[64];
    snprintf(msj, sizeof(msj), "DENIED %d\n", job_id);
    enviar_mensaje_tcp(socket_fd, msj);
}



void timer_deadlock_nodos(void) {
    uint64_t expiraciones;
    read(timer_timeout, &expiraciones, sizeof(expiraciones));
    // Expirar RESERVE encolados vencidos (anti-deadlock)
    gestor_expirar_pedidos(estado, notificar_denied_timeout);
    // Desconectar nodos que no enviaron ANNOUNCE recientemente
    gestor_desconectar_nodos(estado);
}


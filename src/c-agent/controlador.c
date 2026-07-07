#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>

#include "config.h"
#include "controlador.h"
#include "servidor.h"      // globals del servidor: estado, usock_udp, timers
#include "red/sockets.h"   // enviar/atender_* y conectar_a_nodo
#include "gestor_estado.h" // implementacion del gestor de recursos
#include "modelo/peticiones.h" // PeticionMulti para rollback de JOB_REQUEST

#define IP_BROADCAST "255.255.255.255"
#define TAM_BUFFER_ANUNCIO 256

EstadoGlobal estado = NULL;

static void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg);
static void erlang_get_nodes(ClienteConectado *cliente);
static void erlang_job_request(ClienteConectado *cliente, int job_id, char* msg);
static void erlang_job_release(int job_id);
static void rollback_y_denegar(int job_id, int socket_erlang, int n_enviados);
static void procesar_mensaje_red_c(ClienteConectado *cliente, char* msg);
static void red_reserve(ClienteConectado *cliente, int job_id, char* recurso, int cant_res);
static void red_granted(ClienteConectado *cliente, int job_id);
static void red_denied(int job_id);
static void red_release(int job_id, char* recurso, int cant_res);
static void notificar_timeout_peticion(PeticionMulti p);

void controlador_anuncio_recibido(char* buffer_udp) {
    char ip_node[TAM_BUFFER_IP], cmd[32];
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
                if      (strcmp(res, RECURSO_CPU) == 0) cpu = val;
                else if (strcmp(res, RECURSO_GPU) == 0) gpu = val;
                else if (strcmp(res, RECURSO_MEM) == 0) mem = val;
            }
            while (*ptr && *ptr != ' ') ptr++;
            while (*ptr == ' ') ptr++;
        }
        gestor_procesar_anuncio(estado, ip_node, puerto_node, cpu, gpu, mem);
    }
}

void controlador_emitir_anuncio() {
    int disp_cpu, disp_gpu, disp_mem;
    gestor_recursos_disponibles(estado, &disp_cpu, &disp_gpu, &disp_mem);

    char msj[TAM_BUFFER_ANUNCIO];
    snprintf(msj, sizeof(msj), "ANNOUNCE %d cpu:%d gpu:%d mem:%d\n",
            PUERTO_TCP, disp_cpu, disp_gpu, disp_mem);
    enviar_mensaje_udp(usock_udp, IP_BROADCAST, PUERTO_UDP, msj);
}

void controlador_anuncio_recursos() {
    uint64_t _expiraciones;
    read(timer_anuncios_fd, &_expiraciones, sizeof(_expiraciones));
    controlador_emitir_anuncio();
}

void controlador_timer_deadlock_y_desconexion() {
    uint64_t _expiraciones;
    read(timer_timeout, &_expiraciones, sizeof(_expiraciones));
    gestor_expirar_pedidos(estado);
    gestor_expirar_peticiones(estado, notificar_timeout_peticion);
    gestor_desconectar_nodos(estado);
}

// -----------------------------------------------------------------------------

void controlador_desconexion_cliente(int cliente_fd) {
    manejar_desconexion_socket(estado, cliente_fd, callback_granted_red);
    gestor_limpiar_conexion_por_fd(estado, cliente_fd);
}

// Callback C-C: notifica a un nodo coordinador que su RESERVE encolado fue concedido.
// Usa "GRANTED" (protocolo C-C), no "JOB_GRANTED" (que es solo para Erlang).
void callback_granted_red(int job_id, int socket_fd) {
    char msj[64];
    snprintf(msj, sizeof(msj), "GRANTED %d\n", job_id);
    enviar_mensaje_tcp(socket_fd, msj);
}

// -----------------------------------------------------------------------------

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

static void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg) {
    char comando[32];
    int job_id = -1;
    char recursos[128];
    int parseados = sscanf(msg, "%31s %d %127s", comando, &job_id, recursos);
    if (parseados < 1)
        return;

    if (strcmp(comando, "GET_NODES") == 0 && parseados == 1)
        erlang_get_nodes(cliente);
    else if (strcmp(comando, "JOB_REQUEST") == 0 && parseados >= 2)
        erlang_job_request(cliente, job_id, msg);
    else if (strcmp(comando, "JOB_RELEASE") == 0 && parseados == 2)
        erlang_job_release(job_id);
}

// Erlang pide la tabla de nodos conocidos del clúster.
static void erlang_get_nodes(ClienteConectado *cliente) {
    printf("[CONTROLADOR] Erlang hace GET_NODES\n");
    char *nodos = gestor_get_nodes(estado);
    if (nodos != NULL) {
        enviar_mensaje_tcp(cliente->fd, nodos);
        free(nodos);
    }
}

// Erlang envía: JOB_REQUEST <id> @IP:recurso:cant [@IP2:recurso2:cant2 ...]
// Conecta a cada nodo destino y le manda un RESERVE; ante cualquier fallo hace
// rollback de lo ya enviado y devuelve JOB_DENIED.
static void erlang_job_request(ClienteConectado *cliente, int job_id, char* msg) {
    char recursos[128] = {0};
    sscanf(msg, "%*s %*d %127s", recursos);
    printf("[CONTROLADOR] Erlang pide RECURSOS: %s para el trabajo %d\n", recursos, job_id);

    // Avanzamos el cursor hasta el primer token @IP:...
    const char *cursor = msg;
    while (*cursor && *cursor != ' ') cursor++;
    while (*cursor == ' ') cursor++;
    while (*cursor && *cursor != ' ') cursor++;
    while (*cursor == ' ') cursor++;

    // Contar tokens para saber cuántos RESERVE vamos a enviar
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
    if (total == 0 || total > MAX_NODOS_PETICION) {
        char msj[64];
        snprintf(msj, sizeof(msj), "JOB_DENIED %d\n", job_id);
        enviar_mensaje_tcp(cliente->fd, msj);
        return;
    }

    // Crear y registrar la peticion antes de enviar los RESERVE.
    PeticionMulti peticion = peticion_crear(job_id, cliente->fd, total);
    gestor_registrar_peticion(estado, peticion);

    // Conectar y enviar RESERVE a cada nodo
    int idx = 0;
    char token[128];
    while (sscanf(cursor, "%127s", token) == 1) {
        char ip_destino[TAM_BUFFER_IP], nombre_recurso[32];
        int cantidad;

        if (sscanf(token, "@%49[^:]:%31[^:]:%d",
                   ip_destino, nombre_recurso, &cantidad) != 3) {
            rollback_y_denegar(job_id, cliente->fd, idx);
            return;
        }

        // Lookup atómico bajo lock: puerto del nodo + fd de su conexión cacheada
        // (-1 si no hay). No se retiene ningún puntero a Nodo/conexión fuera del lock.
        int puerto_destino, fd_destino;
        if (!gestor_obtener_destino(estado, ip_destino, &puerto_destino, &fd_destino)) {
            rollback_y_denegar(job_id, cliente->fd, idx);
            return;
        }

        // Sin conexión cacheada: abrir una nueva. conectar_a_nodo bloquea, así que
        // va FUERA del lock; después se registra en el epoll y se cachea bajo lock.
        if (fd_destino == -1) {
            fd_destino = conectar_a_nodo(ip_destino, puerto_destino);
            if (fd_destino == -1) {
                rollback_y_denegar(job_id, cliente->fd, idx);
                return;
            }
            ClienteConectado *nueva = crear_cliente_conectado(fd_destino, 0);
            servidor_agregar_cliente_en_epoll(nueva, EPOLLIN | EPOLLONESHOT);
            gestor_registrar_conexion(estado, ip_destino, puerto_destino, nueva);
        }

        pthread_mutex_lock(&estado->lock);
        peticion->nodos[idx].fd_remoto = fd_destino;
        strncpy(peticion->nodos[idx].recurso, nombre_recurso,
                sizeof(peticion->nodos[idx].recurso) - 1);
        peticion->nodos[idx].cantidad = cantidad;
        peticion->nodos[idx].grantado = 0;
        idx++;
        pthread_mutex_unlock(&estado->lock);

        char msj_red[128];
        snprintf(msj_red, sizeof(msj_red), "RESERVE %d %s %d\n",
                 job_id, nombre_recurso, cantidad);
        enviar_mensaje_tcp(fd_destino, msj_red);

        while (*cursor && *cursor != ' ') cursor++;
        while (*cursor == ' ') cursor++;
    }
}

// Erlang avisa que el trabajo terminó: manda RELEASE a todos los nodos remotos de
// la peticion y la destruye.
static void erlang_job_release(int job_id) {
    int fds[MAX_NODOS_PETICION]; 
    char recursos_jb[MAX_NODOS_PETICION][32]; 
    int cantidades_jb[MAX_NODOS_PETICION]; int n = 0;
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
        snprintf(msj, sizeof(msj), "RELEASE %d %s %d\n", job_id, recursos_jb[i], 
                cantidades_jb[i]);
        enviar_mensaje_tcp(fds[i], msj);
    }
}

// Helper: cancela todos los RESERVE ya enviados y notifica JOB_DENIED.
// Debe llamarse sin tener el lock tomado.
static void rollback_y_denegar(int job_id, int socket_erlang, int n_enviados) {
    int fds[MAX_NODOS_PETICION]; char recursos_rb[MAX_NODOS_PETICION][32]; 
    int cantidades_rb[MAX_NODOS_PETICION];
    int n = 0;

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

static void procesar_mensaje_red_c(ClienteConectado *cliente, char* msg) {
    char comando[32];
    int job_id = -1;
    char recurso[32];
    int cant_res = 0;
    int parseados = sscanf(msg, "%31s %d %31s %d", comando, &job_id, recurso, &cant_res);
    if (parseados < 1)
        return;

    if (strcmp(comando, "RESERVE") == 0 && parseados == 4)
        red_reserve(cliente, job_id, recurso, cant_res);
    else if (strcmp(comando, "GRANTED") == 0 && parseados >= 2)
        red_granted(cliente, job_id);
    else if (strcmp(comando, "DENIED") == 0 && parseados >= 2)
        red_denied(job_id);
    else if (strcmp(comando, "RELEASE") == 0 && parseados == 4)
        red_release(job_id, recurso, cant_res);
}

// Otro agente nos pide reservar un recurso local para un job suyo.
static void red_reserve(ClienteConectado *cliente, int job_id, char* recurso, int cant_res) {
    printf("[CONTROLADOR] El FD %d intenta reservar localmente %s %d para el trabajo %d\n",
            cliente->fd, recurso, cant_res, job_id);
    int resultado = gestor_manejar_reserva(estado, recurso, job_id, cliente->fd, cant_res);
    if (resultado == 1) {
        char rsp[64];
        snprintf(rsp, sizeof(rsp), "GRANTED %d\n", job_id);
        enviar_mensaje_tcp(cliente->fd, rsp);
    } else if (resultado == -1) {
        char rsp[64];
        snprintf(rsp, sizeof(rsp), "DENIED %d\n", job_id);
        enviar_mensaje_tcp(cliente->fd, rsp);
    }
    // resultado == 0: encolado; callback_granted_red avisará cuando haya recursos.
}

// Un nodo nos responde GRANTED a un RESERVE que le mandamos. Cuando TODOS los
// nodos de la peticion respondieron, avisamos JOB_GRANTED a Erlang.
static void red_granted(ClienteConectado *cliente, int job_id) {
    printf("[CONTROLADOR] Reserva exitosa en otro nodo para el trabajo %d\n", job_id);

    int enviar_granted = 0, skt_erlang = -1;
    pthread_mutex_lock(&estado->lock);
    PeticionMulti p = gestor_buscar_peticion(estado, job_id);
    if (p) {
        NodoReserva *nr = peticion_buscar_nodo_por_fd(p, cliente->fd);
        if (nr) {
            nr->grantado = 1;
            p->respondidos++;        
            if (p->respondidos == p->total) {
                enviar_granted = 1;
                skt_erlang = p->socket_erlang;
                // No destruir: la peticion se necesita hasta JOB_RELEASE.
            }
        }
    }
    pthread_mutex_unlock(&estado->lock);

    if (enviar_granted) {
        char msj[64];
        snprintf(msj, sizeof(msj), "JOB_GRANTED %d\n", job_id);
        enviar_mensaje_tcp(skt_erlang, msj);
    }
}

// Un nodo rechazó nuestro RESERVE -> rollback: RELEASE a TODOS los nodos de la
// peticion (no sabemos cuáles llegaron a GRANTED) y JOB_DENIED a Erlang.
static void red_denied(int job_id) {
    printf("[CONTROLADOR] Reserva rechazada por otro nodo para el trabajo %d.\n", job_id);

    int fds[MAX_NODOS_PETICION];
    char recursos_dn[MAX_NODOS_PETICION][32];
    int cantidades_dn[MAX_NODOS_PETICION];
    int n = 0, skt_erlang = -1;

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

// Un nodo nos pide liberar un recurso local de un job (rollback o fin de trabajo).
static void red_release(int job_id, char* recurso, int cant_res) {
    printf("[CONTROLADOR] RELEASE recibido para job %d: %s %d\n", job_id, recurso, cant_res);
    gestor_manejar_release(estado, recurso, job_id, cant_res, callback_granted_red);
}

//------------------------------------------------------------------------------

// Callback de timeout del lado coordinador: la PeticionMulti venció sin
// completarse. Manda RELEASE a todos sus nodos (rollback) y JOB_TIMEOUT a su
// Erlang para que reintente. Se invoca con estado->lock tomado (desde el gestor).
static void notificar_timeout_peticion(PeticionMulti p) {
    for (int i = 0; i < p->total; i++) {
        char msj[64];
        snprintf(msj, sizeof(msj), "RELEASE %d %s %d\n",
                 p->job_id, p->nodos[i].recurso, p->nodos[i].cantidad);
        enviar_mensaje_tcp(p->nodos[i].fd_remoto, msj);
    }
    char msj[64];
    snprintf(msj, sizeof(msj), "JOB_TIMEOUT %d\n", p->job_id);
    enviar_mensaje_tcp(p->socket_erlang, msj);
}



#ifndef __PETICIONES_H__
#define __PETICIONES_H__

#include "config.h"

#include <time.h>

#define MAX_NODOS_PETICION 16

typedef struct nodoReserva_ {
    int fd_remoto;
    char recurso[TAM_NOMBRE_RECURSO];
    int cantidad;
    int grantado;  /* 0 = pendiente, 1 = GRANTED recibido */
} NodoReserva;

// Pedidos para nuestro Erlang.
typedef struct peticionMulti_ {
    int    job_id;
    int    socket_erlang;      /* fd de Erlang para JOB_GRANTED / JOB_DENIED / JOB_TIMEOUT */
    int    total;              /* cuántos RESERVE se enviaron */
    int    respondidos;        /* cuántos ya respondieron */
    time_t instante_creacion;  /* para expirar la petición si no se completa a tiempo */
    NodoReserva nodos[MAX_NODOS_PETICION];
} *PeticionMulti;

/**
 * Crea una PeticionMulti para un JOB_REQUEST con total recursos remotos a
 * pedir. Inicializa respondidos = 0 y todos los nodos[] en cero (fd_remoto,
 * cantidad y grantado en 0, recurso en ""); el caller todavía tiene que
 * llenar cada nodos[i] a medida que va conectando y mandando cada RESERVE.
 */
PeticionMulti peticion_crear(int job_id, int socket_erlang, int total);

/**
 * Búsqueda lineal en nodos[0..total) por fd_remoto. Devuelve el puntero al
 * NodoReserva o NULL si no está.
 */
NodoReserva* peticion_buscar_nodo_por_fd(PeticionMulti p, int fd);

#endif /* __PETICIONES_H__ */

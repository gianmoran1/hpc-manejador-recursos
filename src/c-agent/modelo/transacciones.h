#ifndef __TRANSACCIONES_H__
#define __TRANSACCIONES_H__

// Tope de recursos remotos que puede llevar un mismo JOB_REQUEST (ver BUG
// más abajo, junto a PeticionMulti).
#define MAX_NODOS_PETICION 16

typedef struct nodoReserva_ {
    int  fd_remoto;
    char recurso[32];
    int  cantidad;
    int  grantado;  /* 0 = pendiente/esperando, 1 = GRANTED recibido */
} NodoReserva;

typedef struct peticionMulti_ {
    int        job_id;
    int        socket_erlang;   /* fd de Erlang para JOB_GRANTED / JOB_DENIED */
    int        total;           /* cuántos RESERVE se enviaron */
    int        respondidos;     /* cuántos ya respondieron */
    // BUG: total llega desde afuera (controlador.c cuenta los tokens
    // "@IP:recurso:cant" del JOB_REQUEST de Erlang) y nada acá ni en el
    // llamador valida que sea <= MAX_NODOS_PETICION antes de escribir en
    // nodos[idx] para idx en [0, total). Un JOB_REQUEST con más de
    // MAX_NODOS_PETICION recursos desborda este arreglo fijo (heap buffer
    // overflow sobre el resto de este malloc). El fix real va en
    // controlador.c: cortar/rechazar el JOB_REQUEST si total > MAX_NODOS_PETICION
    // antes de llamar a peticion_crear.
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
 * NodoReserva o NULL si no está. El arreglo es chico (<= MAX_NODOS_PETICION)
 * así que la búsqueda lineal es apropiada.
 */
NodoReserva*  peticion_buscar_nodo_por_fd(PeticionMulti p, int fd);

/**
 * Libera la petición. Los NodoReserva viven embebidos en el struct (no son
 * punteros propios), así que no hay nada más que liberar aparte de p.
 */
void          peticion_destruir(PeticionMulti p);

#endif /* __TRANSACCIONES_H__ */

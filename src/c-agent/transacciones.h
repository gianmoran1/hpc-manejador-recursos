#ifndef __TRANSACCIONES_H__
#define __TRANSACCIONES_H__

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
    NodoReserva nodos[16];      /* máx. 16 recursos por job */
    struct peticionMulti_ *sig;
} *PeticionMulti;

PeticionMulti peticion_crear(int job_id, int socket_erlang, int total);
NodoReserva*  peticion_buscar_nodo_por_fd(PeticionMulti p, int fd);
void          peticion_destruir(PeticionMulti p);

#endif /* __TRANSACCIONES_H__ */

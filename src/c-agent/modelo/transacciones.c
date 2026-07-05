#include "transacciones.h"
#include <stdlib.h>
#include <string.h>

PeticionMulti peticion_crear(int job_id, int socket_erlang, int total) {
    PeticionMulti p  = malloc(sizeof(struct peticionMulti_));
    p->job_id        = job_id;
    p->socket_erlang = socket_erlang;
    p->total         = total;
    p->respondidos   = 0;
    memset(p->nodos, 0, sizeof(p->nodos));
    return p;
}

NodoReserva* peticion_buscar_nodo_por_fd(PeticionMulti p, int fd) {
    for (int i = 0; i < p->total; i++)
        if (p->nodos[i].fd_remoto == fd && p->nodos[i].grantado == 0) 
            return &p->nodos[i];
    return NULL;
}

void peticion_destruir(PeticionMulti p) {
    free(p);
}

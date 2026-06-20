#ifndef __NODOS_H__
#define __NODOS_H__

#include "tablahash.h"
#include "glist.h"
#include <time.h>


typedef struct nodo_{
    char ip[50];
    int puerto;
    int cpu;
    int gpu;
    int mem;
    time_t ultimo_anuncio;
} *Nodo;

typedef struct tabla_nodos{
    TablaHash tabla;
    GList     lista;
}*TablaNodos;


Nodo crear_nodo(char* ip, int puerto, int cpu, int gpu, int mem);

void agregar_nodo(Nodo, TablaNodos);

void reiniciar_timestamp(char* ip, TablaNodos);

TablaNodos crear_tabla_nodos();

void destruir_tabla_nodos(TablaNodos);

void desconectar(TablaNodos tabla_nodos);






#endif /* __NODOS_H__ */
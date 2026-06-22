#ifndef __NODOS_H__
#define __NODOS_H__

#include "tablahash.h"
#include "glist.h"
#include "cliente.h"
#include <time.h>


typedef struct nodo_{
    char ip[50];
    int puerto;
    int cpu;
    int gpu;
    int mem;
    time_t ultimo_anuncio;
    ClienteConectado *conexion; /* NULL = sin conexión activa */
} *Nodo;

typedef struct tabla_nodos{
    TablaHash tabla;
    GList     lista;
}*TablaNodos;

/*crea una estructura de nodo */
Nodo crear_nodo(char* ip, int puerto, int cpu, int gpu, int mem);

/*agrega un nodo a la tabla */
void agregar_nodo(Nodo, TablaNodos);

/*reinicia el timestamp de un nodo, devuelve 1 si lo encontró, 0 en caso contrario*/
int reiniciar_timestamp(char* ip, int puerto, TablaNodos);

/*crea una tabla de nodos vacía */
TablaNodos crear_tabla_nodos();

/*destruye una tabla de nodos */
void destruir_tabla_nodos(TablaNodos);

/*elimina los nodos que no han enviado anuncios en los últimos 15 segundos*/
void desconectar(TablaNodos tabla_nodos);

/*obtiene la lista de nodos en formato de string*/
char* get_nodes(TablaNodos tabla_nodos);

/*busca un nodo en la tabla, devuelve 1 si lo encuentra, 0 en caso contrario*/
int buscar_nodo(char* ip, int puerto, TablaNodos tabla_nodos);

/*procesa un anuncio de un nodo*/
void procesar_anuncio(TablaNodos tabla_nodos, char* ip, int puerto, int cpu, int gpu, int mem);

/* Devuelve la conexión activa del nodo (ip, puerto), o NULL si no existe o no tiene conexión */
ClienteConectado* nodo_obtener_conexion(char* ip, int puerto, TablaNodos tabla_nodos);

/* Asocia una conexión al nodo (ip, puerto). No-op si el nodo no está en la tabla. */
void nodo_registrar_conexion(char* ip, int puerto, ClienteConectado* cliente, TablaNodos tabla_nodos);

/* Pone a NULL el campo conexion del nodo que tenga ese fd. No libera memoria (el caller lo hace). */
void nodo_limpiar_conexion_por_fd(int fd, TablaNodos tabla_nodos);

#endif /* __NODOS_H__ */
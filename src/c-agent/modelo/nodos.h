#ifndef __NODOS_H__
#define __NODOS_H__

#include "estructuras/tablahash.h"
#include "estructuras/glist.h"
#include "red/cliente.h"
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

/**
 * Crea una estructura de nodo, con conexion = NULL y ultimo_anuncio = ahora.
 */
Nodo crear_nodo(char* ip, int puerto, int cpu, int gpu, int mem);

/**
 * Agrega un nodo ya creado a la tabla (hash + lista). No verifica si ya
 * existía uno con la misma ip; para upsert usar procesar_anuncio.
 */
void agregar_nodo(Nodo, TablaNodos);

/**
 * Reinicia el timestamp ultimo_anuncio de un nodo a ahora.
 * Devuelve 1 si lo encontró, 0 en caso contrario.
 * NOTA: no tiene ningún llamador en el proyecto actualmente (procesar_anuncio
 * hace su propio upsert). Candidata a eliminar.
 */
int reiniciar_timestamp(char* ip, int puerto, TablaNodos);

/**
 * Crea y devuelve una tabla de nodos vacía.
 */
TablaNodos crear_tabla_nodos();

/**
 * Destruye la tabla de nodos completa: libera cada Nodo (y su conexión
 * cacheada, si tenía una) a través de la tabla hash, y luego los nodos de
 * la lista con un destructor no-op para no liberar el mismo Nodo dos veces.
 */
void destruir_tabla_nodos(TablaNodos);

/**
 * Elimina los nodos que no han enviado un ANNOUNCE en los últimos 15
 * segundos. NO cierra ni libera la conexión TCP cacheada del nodo
 * (nodo->conexion): esa es propiedad del loop de epoll. Si el nodo se saca
 * con una conexión viva, esta queda huérfana y se libera cuando se cierra.
 * Debe llamarse con estado->lock tomado (lo hace gestor_desconectar_nodos).
 */
void desconectar(TablaNodos tabla_nodos);

/**
 * Arma el string de respuesta para GET_NODES:
 * "NODES IP:puerto:cpu:X:mem:Y:gpu:Z[;...]". El caller debe hacer free().
 */
char* get_nodes(TablaNodos tabla_nodos);

/**
 * Busca un nodo por (ip, puerto). Devuelve 1 si lo encuentra, 0 si no.
 * NOTA: no tiene ningún llamador en el proyecto actualmente (todo el
 * código usa buscar_nodo_por_ip). Candidata a eliminar.
 */
int buscar_nodo(char* ip, int puerto, TablaNodos tabla_nodos);

/**
 * Busca un nodo solo por ip (ignora puerto). Devuelve el Nodo o NULL.
 */
Nodo buscar_nodo_por_ip(char* ip, TablaNodos tabla_nodos);

/**
 * Upsert de un anuncio: si ya existe un nodo con esa ip+puerto, actualiza
 * sus recursos y renueva ultimo_anuncio; si no existe, lo crea y agrega.
 */
void procesar_anuncio(TablaNodos tabla_nodos, char* ip, int puerto, int cpu, int gpu, int mem);

/**
 * Asocia una conexión ya establecida al nodo (ip, puerto), para poder
 * reutilizarla en el próximo RESERVE hacia ese mismo nodo. No-op si el nodo
 * no está en la tabla. Debe llamarse bajo lock (usar gestor_registrar_conexion).
 */
void nodo_registrar_conexion(char* ip, int puerto, ClienteConectado* cliente, TablaNodos tabla_nodos);

/**
 * Busca por fd el nodo que tiene esa conexión cacheada y le pone
 * conexion = NULL. NO elimina el nodo del registro ni libera el
 * ClienteConectado. Debe llamarse bajo lock (usar gestor_limpiar_conexion_por_fd).
 */
void nodo_limpiar_conexion_por_fd(int fd, TablaNodos tabla_nodos);

#endif /* __NODOS_H__ */
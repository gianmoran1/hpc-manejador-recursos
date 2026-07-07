#include "nodos.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TIEMPO_VIDA_NODO        15.0 // sin ANNOUNCE en este lapso, el nodo se desconecta

// Callbacks para la tabla de nodos.

static Nodo nodo_no_copiar(Nodo nodo){
    return nodo;
}

// La funcion no destruye ClienteConectado, ya que de eso se encarga el servido
// en la parte de epoll.
static void nodo_destruir(Nodo nodo){
    free(nodo);
}

static void no_destruye(__attribute__((unused)) Nodo nodo){
    return;
}

static int nodo_comparar(Nodo a, Nodo b){
    int cmp = strcmp(a->ip, b->ip);
    return cmp;
}

static unsigned nodo_hash(Nodo a){
    char* s = a->ip;
    unsigned hashval;
    for (hashval = 0; *s != '\0'; ++s) {
        hashval = *s + 31 * hashval;
    }
    return hashval;
}

/**
 * Crea una estructura de nodo, con conexion = NULL y ultimo_anuncio = ahora.
 */
static Nodo crear_nodo(char* ip, int puerto, int cpu, int gpu, int mem){
    Nodo nodo = malloc(sizeof (struct nodo_));
    strncpy(nodo->ip, ip, (sizeof (nodo->ip) -1));
    nodo->ip[sizeof(nodo->ip) - 1] = '\0';
    nodo->puerto = puerto;
    nodo->cpu = cpu;
    nodo->gpu = gpu;
    nodo->mem = mem;
    nodo->ultimo_anuncio = time(NULL);
    nodo->conexion = NULL;
    return nodo;
}

/**
 * Agrega un nodo ya creado a la tabla (hash + lista). No verifica si ya
 * existía uno con la misma ip.
 */
static void agregar_nodo(Nodo nodo, TablaNodos tabla_nodos){ 
    tablahash_insertar(tabla_nodos->tabla, nodo);
    tabla_nodos->lista = glist_agregar_inicio(tabla_nodos->lista, nodo, (
                                                FuncionCopia)nodo_no_copiar);
}

//------------------------------------------------------------------------------

TablaNodos crear_tabla_nodos(){
    TablaNodos tabla_nodos = malloc(sizeof(struct tabla_nodos));
    assert(tabla_nodos);
    tabla_nodos->tabla = tablahash_crear(TAM_INICIAL_TABLA_HASH, 
                                (FuncionCopia)nodo_no_copiar, 
                                (FuncionComparadora) nodo_comparar, 
                                (FuncionDestructora) nodo_destruir,
                                (FuncionHash) nodo_hash);
    tabla_nodos->lista = NULL;
    return tabla_nodos;
}

void destruir_tabla_nodos(TablaNodos tabla_nodos){ 
    tablahash_destruir(tabla_nodos->tabla);
    glist_destruir(tabla_nodos->lista ,(FuncionDestructora) no_destruye);
    free(tabla_nodos);
}

void nodos_procesar_anuncio(TablaNodos tabla_nodos, char* ip, int puerto, int cpu, 
                        int gpu, int mem) {
    // Crea un nodo de busqueda
    struct nodo_ busqueda; // Es local no hace falta liberarlo
    strncpy(busqueda.ip, ip, (sizeof(busqueda.ip) - 1));
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';
    busqueda.puerto = puerto;

    Nodo encontrado = tablahash_buscar(tabla_nodos->tabla, &busqueda);

    if (encontrado) { // Si el nodo ya esta lo actualizo
        encontrado->ultimo_anuncio = time(NULL);
        encontrado->cpu = cpu;
        encontrado->gpu = gpu;
        encontrado->mem = mem;
    } else {
        Nodo nuevo = crear_nodo(ip, puerto, cpu, gpu, mem);
        agregar_nodo(nuevo, tabla_nodos);
    }
}







void gestor_desconectar_nodos_timeout(TablaNodos tabla_nodos){
    GList temp = tabla_nodos->lista;
    if (temp == NULL) return; // No hay nodos

    time_t ahora = time(NULL); 
    time_t tiempo_nodo;

    while (temp != NULL) {  // Hay nodos, elimina los que corresponda
        tiempo_nodo = ((Nodo)temp->data)->ultimo_anuncio;
        
        if (difftime(ahora, tiempo_nodo) >= TIEMPO_VIDA_NODO) {
            GList next = temp->next;
            tablahash_eliminar(tabla_nodos->tabla, temp->data);
            free(temp);
            temp = next;
            tabla_nodos->lista = temp; 
        } else {
            break;
        }
    }

    if (temp == NULL) return;

    while (temp->next != NULL) {
        tiempo_nodo = ((Nodo)temp->next->data)->ultimo_anuncio; 
        if (difftime(ahora, tiempo_nodo) >= TIEMPO_VIDA_NODO) {
            GList next = temp->next->next;
            tablahash_eliminar(tabla_nodos->tabla, temp->next->data);
            free(temp->next);
            temp->next = next;
            
        } else {
            temp = temp->next; 
        }
    }
}

char* get_nodes(TablaNodos tabla_nodos) {
    int capacidad_actual = 2048;
    char* buffer = malloc(capacidad_actual);
    if (!buffer) return NULL;
    
    strcpy(buffer, "NODES ");
    
    GList temp = tabla_nodos->lista;
    int primero = 1;
    
    while (temp != NULL) {
        Nodo n = (Nodo)temp->data;
        char temp_str[256];
        
        // Formato del enunciado
        sprintf(temp_str, "%s%s:%d:cpu:%d:mem:%d:gpu:%d", 
                primero ? "" : ";", n->ip, n->puerto, n->cpu, n->mem, n->gpu);
        
        // Reallocamos si es necesario
        if (strlen(buffer) + strlen(temp_str) + 1 >= (size_t)capacidad_actual) {
            capacidad_actual *= 2;
            buffer = realloc(buffer, capacidad_actual);
        }
        strcat(buffer, temp_str);
        primero = 0;
        temp = temp->next;
    }
    
    if (strlen(buffer) + 2 > (size_t)capacidad_actual) {
        capacidad_actual += 2; 
        buffer = realloc(buffer, capacidad_actual);
    }
    strcat(buffer, "\n");
    return buffer;
}

Nodo buscar_nodo_por_ip(char* ip, TablaNodos tabla_nodos) {
    struct nodo_ busqueda;
    strncpy(busqueda.ip, ip, (sizeof(busqueda.ip) - 1));
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';

    Nodo encontrado = tablahash_buscar(tabla_nodos->tabla, &busqueda);
    return encontrado;
}

/* Helper interno: devuelve el puntero al nodo por ip+puerto, o NULL. */
static Nodo buscar_nodo_ptr(char* ip, int puerto, TablaNodos tabla_nodos) {
    struct nodo_ busqueda;
    strncpy(busqueda.ip, ip, sizeof(busqueda.ip) - 1);
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';
    busqueda.puerto = puerto;
    return (Nodo)tablahash_buscar(tabla_nodos->tabla, &busqueda);
}

void nodo_registrar_conexion(char* ip, int puerto, ClienteConectado* cliente, TablaNodos tabla_nodos) {
    Nodo n = buscar_nodo_ptr(ip, puerto, tabla_nodos);
    if (n != NULL) n->conexion = cliente;
}

void nodo_limpiar_conexion_por_fd(int fd, TablaNodos tabla_nodos) {
    // Solo desvincula la conexión cacheada (conexion = NULL). NO elimina el nodo
    // del registro (eso lo decide el timeout de 15s en desconectar()) ni libera
    // el ClienteConectado (lo hace el loop de epoll, su dueño).
    for (GList temp = tabla_nodos->lista; temp != NULL; temp = temp->next) {
        Nodo n = (Nodo)temp->data;
        if (n != NULL && n->conexion != NULL && n->conexion->fd == fd) {
            n->conexion = NULL;
            return;
        }
    }
}

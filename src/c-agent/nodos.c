#include "nodos.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>




static Nodo nodo_copiar(Nodo nodo){
    return nodo;
}

static void nodo_destruir(Nodo nodo){
    free(nodo);
}



static void no_destruye(__attribute__((unused)) Nodo nodo){
    return;
}

static int nodo_comparar(Nodo a, Nodo b){
    return strcmp(a->ip , b->ip);
}

static unsigned nodo_hash(Nodo a){
    char* s = a->ip;
    unsigned hashval;
    for (hashval = 0; *s != '\0'; ++s) {
        hashval = *s + 31 * hashval;
    }
    return hashval;
}

TablaNodos crear_tabla_nodos(){
    TablaNodos tabla_nodos = malloc(sizeof(struct tabla_nodos));
    TablaHash tablahash = tablahash_crear(100, (FuncionCopiadora)nodo_copiar, 
                                (FuncionComparadora) nodo_comparar, 
                                (FuncionDestructora) nodo_destruir,
                                (FuncionHash) nodo_hash);

    tabla_nodos->tabla = tablahash;
    tabla_nodos->lista = NULL;

    return tabla_nodos;
}

void destruir_tabla_nodos(TablaNodos tabla_nodos){
    tablahash_destruir(tabla_nodos->tabla);
    glist_destruir(tabla_nodos->lista ,(FuncionDestructora) no_destruye);
    free(tabla_nodos);
}

Nodo crear_nodo(char* ip, int puerto, int cpu, int gpu, int mem){
    Nodo nodo = malloc(sizeof (struct nodo_));
    strncpy(nodo->ip, ip, (sizeof (nodo->ip) -1));
    nodo->ip[sizeof(nodo->ip) - 1] = '\0'; 
    nodo->puerto = puerto;
    nodo->cpu = cpu;
    nodo->gpu = gpu;
    nodo->mem = mem;
    nodo->ultimo_anuncio = time(NULL);
    return nodo;
}

void agregar_nodo(Nodo nodo, TablaNodos tabla_nodos){
    tablahash_insertar(tabla_nodos->tabla, nodo);
    tabla_nodos->lista = glist_agregar_inicio(tabla_nodos->lista, nodo, (FuncionCopiadora)nodo_copiar);
}


void reiniciar_timestamp(char* ip, TablaNodos tabla_nodos){
    Nodo nodoBusqueda = malloc(sizeof(struct nodo_));
    strncpy(nodoBusqueda->ip, ip, (sizeof (nodoBusqueda->ip) -1));
    nodoBusqueda->ip[sizeof(nodoBusqueda->ip) - 1] = '\0'; 
    Nodo nodo = tablahash_buscar(tabla_nodos->tabla,nodoBusqueda); 
    if (nodo != NULL){
        nodo->ultimo_anuncio = time(NULL);
    }
    free(nodoBusqueda);
}


void desconectar(TablaNodos tabla_nodos){
    GList temp = tabla_nodos->lista;
    if (temp == NULL) return;
    
    time_t ahora = time(NULL);
    time_t tiempo_nodo;

    
    while (temp != NULL) {
        tiempo_nodo = ((Nodo)temp->data)->ultimo_anuncio;
        
        if (difftime(ahora, tiempo_nodo) >= 15.0) {
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
        
        if (difftime(ahora, tiempo_nodo) >= 15.0) {
            GList next = temp->next->next;
            tablahash_eliminar(tabla_nodos->tabla, temp->next->data);
            free(temp->next);
            temp->next = next;
            
        } else {
            
            temp = temp->next; 
        }
    }
}



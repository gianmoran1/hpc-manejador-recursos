#include "nodos.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/*funciones para la tabla de nodos*/

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
    int cmp = strcmp(a->ip, b->ip);
    if (cmp != 0) return cmp;
    return a->puerto - b->puerto;
}

static unsigned nodo_hash(Nodo a){
    char* s = a->ip;
    unsigned hashval;
    for (hashval = 0; *s != '\0'; ++s) {
        hashval = *s + 31 * hashval;
    }
    return hashval + a->puerto;
}
//-------------------------------------------------------------------------------------------------------------------------------------

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
    // destruimos tanto lista como tabla.
    tablahash_destruir(tabla_nodos->tabla);
    glist_destruir(tabla_nodos->lista ,(FuncionDestructora) no_destruye);
    free(tabla_nodos);
}

Nodo crear_nodo(char* ip, int puerto, int cpu, int gpu, int mem){
    //creamos un nodo
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
    //insertamos tanto a tabla como a lista
    tablahash_insertar(tabla_nodos->tabla, nodo);
    tabla_nodos->lista = glist_agregar_inicio(tabla_nodos->lista, nodo, (FuncionCopiadora)nodo_copiar);
}


int reiniciar_timestamp(char* ip, int puerto, TablaNodos tabla_nodos){ 
    //creamos nodo de busqueda (podria ser local)
    Nodo nodoBusqueda = malloc(sizeof(struct nodo_));
    strncpy(nodoBusqueda->ip, ip, (sizeof (nodoBusqueda->ip) -1));
    nodoBusqueda->ip[sizeof(nodoBusqueda->ip) - 1] = '\0'; 
    nodoBusqueda->puerto = puerto;
    Nodo nodo = tablahash_buscar(tabla_nodos->tabla,nodoBusqueda); 

    if (nodo != NULL){ //si lo encontramos reiniciamos el timestamp
        nodo->ultimo_anuncio = time(NULL);
        free(nodoBusqueda);
        return 1;
    }
    free(nodoBusqueda); //liberamos el nodo de busqueda
    return 0;
}


void desconectar(TablaNodos tabla_nodos){
    GList temp = tabla_nodos->lista;
    if (temp == NULL) return; //no hay nodos
    
    time_t ahora = time(NULL); 
    time_t tiempo_nodo;

    
    while (temp != NULL) {  //hay nodos, elimina los que corresponda
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


void procesar_anuncio(TablaNodos tabla_nodos, char* ip, int puerto, int cpu, int gpu, int mem) {
    //crea un nodo de busqueda
    struct nodo_ busqueda; //es local no hace falta liberarlo
    strncpy(busqueda.ip, ip, (sizeof(busqueda.ip) - 1));
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';
    busqueda.puerto = puerto;

    
    Nodo encontrado = tablahash_buscar(tabla_nodos->tabla, &busqueda);

    if (encontrado) {
        //procesa el anuncio
        encontrado->ultimo_anuncio = time(NULL);
        encontrado->cpu = cpu;
        encontrado->gpu = gpu;
        encontrado->mem = mem;
    } else {
        //agrega el nodo
        Nodo nuevo = crear_nodo(ip, puerto, cpu, gpu, mem);
        agregar_nodo(nuevo, tabla_nodos);
    }
}


char* get_nodes(TablaNodos tabla_nodos) {
    int capacidad_actual = 2048; // Búfer inicial de 2KB
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
        
        // reallocamos si es necesario
        if (strlen(buffer) + strlen(temp_str) + 1 >= (size_t)capacidad_actual) {
            capacidad_actual *= 2;
            buffer = realloc(buffer, capacidad_actual);
        }
        
        strcat(buffer, temp_str);
        
        primero = 0;
        temp = temp->next;
    }
    
    
    // \n\0
    if (strlen(buffer) + 2 > (size_t)capacidad_actual) {
        capacidad_actual += 2; 
        buffer = realloc(buffer, capacidad_actual);
    }
    strcat(buffer, "\n");

    /*IMPORTANTE*/
    // Este string debe ser liberado con free() después de enviarlo por el socket
    return buffer;
}

int buscar_nodo(char* ip, int puerto, TablaNodos tabla_nodos){
    /*creamos nodo de busqueda*/
    struct nodo_ busqueda;
    strncpy(busqueda.ip, ip, (sizeof(busqueda.ip) - 1));
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';
    busqueda.puerto = puerto;

    Nodo encontrado = tablahash_buscar(tabla_nodos->tabla, &busqueda);
    return encontrado != NULL;
}
#include "nodos.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static Nodo nodo_copiar(Nodo nodo){
    return nodo;
}

// BUG (concurrencia): nodo->conexion es el mismo ClienteConectado* que el
// loop de epoll en Agente.c guarda en eventos[n].data.ptr y despacha desde
// cualquiera de los 4 hilos worker, sin tomar estado->lock antes de leer
// entidad->fd. Si este destructor corre (vía desconectar(), llamado cada
// 15s con el lock tomado) justo cuando otro hilo está despachando un
// evento para ese mismo fd, ese hilo termina leyendo/usando memoria ya
// liberada (use-after-free), y el fd puede haber sido reciclado por el
// kernel para un socket completamente distinto entre el close() de acá y
// el recv() del otro hilo. No hay ningún mecanismo hoy que coordine el
// cierre de una conexión cacheada con el despacho de epoll.
static void nodo_destruir(Nodo nodo){
    if (nodo->conexion != NULL) {
        close(nodo->conexion->fd);
        free(nodo->conexion);
    }
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

TablaNodos crear_tabla_nodos(){
    TablaNodos tabla_nodos = malloc(sizeof(struct tabla_nodos));
    TablaHash tablahash = tablahash_crear(100, (FuncionCopia)nodo_copiar, 
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
    nodo->conexion = NULL;
    return nodo;
}

void agregar_nodo(Nodo nodo, TablaNodos tabla_nodos){ 
    tablahash_insertar(tabla_nodos->tabla, nodo);
    tabla_nodos->lista = glist_agregar_inicio(tabla_nodos->lista, nodo, (
                                                FuncionCopia)nodo_copiar);
}

int reiniciar_timestamp(char* ip, int puerto, TablaNodos tabla_nodos){ 
    // Creamos nodo de busqueda (podria ser local)
    Nodo nodoBusqueda = malloc(sizeof(struct nodo_));
    strncpy(nodoBusqueda->ip, ip, (sizeof (nodoBusqueda->ip) -1));
    nodoBusqueda->ip[sizeof(nodoBusqueda->ip) - 1] = '\0'; 
    nodoBusqueda->puerto = puerto;
    Nodo nodo = tablahash_buscar(tabla_nodos->tabla, nodoBusqueda); 

    if (nodo != NULL){ // Si lo encontramos reiniciamos el timestamp
        nodo->ultimo_anuncio = time(NULL);
        free(nodoBusqueda);
        return 1;
    }
    free(nodoBusqueda); // Liberamos el nodo de busqueda
    return 0;
}

void desconectar(TablaNodos tabla_nodos){
    GList temp = tabla_nodos->lista;
    if (temp == NULL) return; // No hay nodos

    time_t ahora = time(NULL); 
    time_t tiempo_nodo;

    while (temp != NULL) {  // Hay nodos, elimina los que corresponda
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

void procesar_anuncio(TablaNodos tabla_nodos, char* ip, int puerto, int cpu, 
                        int gpu, int mem) {
    // Crea un nodo de busqueda
    struct nodo_ busqueda; // Es local no hace falta liberarlo
    strncpy(busqueda.ip, ip, (sizeof(busqueda.ip) - 1));
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';
    busqueda.puerto = puerto;

    Nodo encontrado = tablahash_buscar(tabla_nodos->tabla, &busqueda);

    if (encontrado) {
        encontrado->ultimo_anuncio = time(NULL);
        encontrado->cpu = cpu;
        encontrado->gpu = gpu;
        encontrado->mem = mem;
    } else {
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

int buscar_nodo(char* ip, int puerto, TablaNodos tabla_nodos){
    struct nodo_ busqueda;
    strncpy(busqueda.ip, ip, (sizeof(busqueda.ip) - 1));
    busqueda.ip[sizeof(busqueda.ip) - 1] = '\0';
    busqueda.puerto = puerto;

    Nodo encontrado = tablahash_buscar(tabla_nodos->tabla, &busqueda);
    return encontrado != NULL;
}

Nodo buscar_nodo_por_ip(char* ip, TablaNodos tabla_nodos){
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

ClienteConectado* nodo_obtener_conexion(char* ip, int puerto, TablaNodos tabla_nodos) {
    Nodo n = buscar_nodo_ptr(ip, puerto, tabla_nodos);
    return (n != NULL) ? n->conexion : NULL;
}

void nodo_registrar_conexion(char* ip, int puerto, ClienteConectado* cliente, TablaNodos tabla_nodos) {
    Nodo n = buscar_nodo_ptr(ip, puerto, tabla_nodos);
    if (n != NULL) n->conexion = cliente;
}

void nodo_limpiar_conexion_por_fd(int fd, TablaNodos tabla_nodos) {
    GList temp = tabla_nodos->lista;
    if (temp == NULL) return; // La lista está vacía

    // BUG: acá se pone conexion = NULL antes de tablahash_eliminar, así que
    // nodo_destruir no vuelve a cerrar/liberar esa conexión (eso está bien).
    // Pero tablahash_eliminar no solo "limpia la conexión": borra el Nodo
    // ENTERO de la tabla, contradiciendo el nombre de la función y el
    // comentario del .h ("no libera memoria, el caller lo hace"). El nodo
    // remoto completo (ip/cpu/gpu/mem conocidos) desaparece del registro
    // apenas se cae esta conexión cacheada, en vez de esperar los 15s sin
    // ANNOUNCE documentados en arquitectura.md como único criterio de
    // desconexión. El fix sería no llamar tablahash_eliminar acá y dejar
    // que sea el propio nodo el que quede con conexion = NULL hasta el
    // próximo ANNOUNCE o hasta que desconectar() lo expire por timeout.

    // El fd esta primero en la lista
    Nodo n_cabeza = (Nodo)temp->data;
    if (n_cabeza != NULL && n_cabeza->conexion != NULL && n_cabeza->conexion->fd == fd) {
        n_cabeza->conexion = NULL;
        tablahash_eliminar(tabla_nodos->tabla, n_cabeza);
        tabla_nodos->lista = temp->next; // La nueva cabeza es el segundo nodo
        free(temp); // Liberamos el eslabón de la GList
        return;
    }

    // El fd esta en la lista pero no es el primero
    while (temp->next != NULL) {
        Nodo n_siguiente = (Nodo)temp->next->data;

        if (n_siguiente != NULL && n_siguiente->conexion != NULL && n_siguiente->conexion->fd == fd) {
            n_siguiente->conexion = NULL;
            tablahash_eliminar(tabla_nodos->tabla, n_siguiente);

            // Reestructuramos los punteros para puentear el eslabón eliminado
            GList borrar = temp->next;
            temp->next = temp->next->next; 
            free(borrar);
            return;
        }
        // Avanzamos al siguiente eslabón
        temp = temp->next;
    }
}

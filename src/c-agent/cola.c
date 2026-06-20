#include "cola.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>


Cola cola_crear(){
  Cola cola = malloc(sizeof(_Cola));
  cola->primero = NULL;
  cola->ultimo = NULL;
  return cola;
}

void cola_destruir(Cola cola, FuncionDestructora destroy){
  if (cola != NULL){
    for(GNode* primero = cola->primero; primero != NULL;primero = cola->primero ){
      cola->primero = primero->next;
      destroy(primero->data);
      free(primero);
    }
    free(cola);
  }
}

int cola_es_vacia(Cola cola){
  if (cola->primero == NULL) return 1;
  return 0;
}

void* cola_inicio(Cola cola, FuncionCopia copy){
  if (cola == NULL) return NULL;
  if (cola->primero == NULL) return NULL;
  return copy(cola->primero->data);
}

Cola cola_encolar(Cola cola, void* dato, FuncionCopia copy){
  if (cola == NULL) cola = cola_crear();
  GNode* nodoNuevo = malloc(sizeof(GNode));
  nodoNuevo->data = copy(dato);
  nodoNuevo->next = NULL;
  if (cola->primero == NULL){
    cola->primero = nodoNuevo; 
  }
  else{ //cola->ultimo != NULL
    cola->ultimo->next = nodoNuevo;
  }
  cola->ultimo = nodoNuevo;
  return cola;
}//le tengo fe.

void cola_encolar_void(Cola* cola, void* dato, FuncionCopia copy){
  if (*cola == NULL) *cola = cola_crear();
  GNode* nodoNuevo = malloc(sizeof(GNode));
  nodoNuevo->data = copy(dato);
  nodoNuevo->next = NULL;
  if ((*cola)->primero == NULL){
    (*cola)->primero = nodoNuevo; 
  }
  else{ //cola->ultimo != NULL
   (*cola)->ultimo->next = nodoNuevo;
  }
  (*cola)->ultimo = nodoNuevo;
}

Cola cola_desencolar(Cola cola, FuncionDestructora destroy){
  if (cola == NULL) return NULL;
  if (cola->primero == NULL) return NULL;
  GNode *temp = cola->primero;
  cola->primero = cola->primero->next;
  if(cola->primero == NULL) cola->ultimo = NULL;
  destroy(temp->data);
  free(temp);
  return cola;
}

void cola_desencolar_void(Cola *cola, FuncionDestructora destroy){
  if (cola != NULL && (*cola)->primero != NULL){
    GNode *temp = (*cola)->primero;
    (*cola)->primero = (*cola)->primero->next;
    if((*cola)->primero == NULL) (*cola)->ultimo = NULL;
    destroy(temp->data);
    free(temp);
  } 
}

void cola_recorrer(Cola cola, FuncionVisitante visit){
  if (cola != NULL){
    for(GNode* temp = cola->primero; temp !=NULL; temp = temp->next){
      visit(temp->data);
    }
  }
}
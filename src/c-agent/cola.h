#ifndef __COLA_H__
#define __COLA_H__

#include "glist.h"

typedef struct { //
  GNode *primero;
  GNode *ultimo;
}_Cola;

typedef _Cola *Cola;


Cola cola_crear();
void cola_destruir(Cola cola, FuncionDestructora destroy);
void* cola_inicio(Cola cola, FuncionCopia copy);
int cola_es_vacia(Cola cola);
Cola cola_encolar(Cola cola, void* dato, FuncionCopia copy);
void cola_encolar_void(Cola* cola, void* dato, FuncionCopia copy);
Cola cola_desencolar(Cola cola, FuncionDestructora destroy);
void cola_desencolar_void(Cola *cola, FuncionDestructora destroy);
void cola_recorrer(Cola cola, FuncionVisitante visit);

#endif /* __COLA_H__ */
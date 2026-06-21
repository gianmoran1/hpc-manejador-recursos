#ifndef __CONTROLADOR_H__
#define __CONTROLADOR_H__

#include "cliente.h" // Necesitamos saber qué es un ClienteConectado

// Reemplazamos los viejos "mocks" de Agente.c por estas funciones reales
void procesar_mensaje_erlang(ClienteConectado *cliente, char* msg);
void procesar_mensaje_red_c(ClienteConectado *cliente, char* msg);

#endif /* __CONTROLADOR_H__ */
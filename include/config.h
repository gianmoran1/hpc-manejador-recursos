#ifndef __CONFIG_H__
#define __CONFIG_H__

// Definiciones que son parte de varios modulos del proyecto

#define RECURSO_CPU "cpu"
#define RECURSO_GPU "gpu"
#define RECURSO_MEM "mem"

#define TAM_INICIAL_TABLA_HASH 100

#define TAM_BUFFER_IP 50
#define TAM_BUFFER_MENSAJE 1024  // Buffer de acumulación por cliente TCP

#define PUERTO_TCP 4040
#define PUERTO_UDP 12529
#define IP_LOCAL "127.0.0.1"

#define TIEMPO_ESPERA_RESERVA 30.0 // un RESERVE encolado más tiempo expira (anti-deadlock)

#endif /* __CONFIG_H__ */

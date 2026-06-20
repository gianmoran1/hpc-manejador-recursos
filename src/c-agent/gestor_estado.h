#ifndef __GESTOR_ESTADO_H__
#define __GESTOR_ESTADO_H__

#include "recursos.h"
#include "jobs.h"


typedef struct estadoGlobal_ {
    RecursoLocal cpu;
    RecursoLocal gpu;
    RecursoLocal mem;
    TablaJobs libro_contable;
} *EstadoGlobal;


EstadoGlobal estado_crear(int cap_cpu, int cap_gpu, int cap_mem);
void estado_destruir(EstadoGlobal estado);


void manejar_desconexion_socket(EstadoGlobal estado, int socket_caido, FuncionAviso avisar_red);

#endif /* __GESTOR_ESTADO_H__ */
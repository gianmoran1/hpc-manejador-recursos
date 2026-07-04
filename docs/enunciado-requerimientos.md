La idea es diseñar un middleware distribuido que gestione recursos de un cluster HPC simulado (CPU, RAM, GPU). Cada equipo construye un nodo independiente.

	Puntos iniciales a tener en cuenta:
Se debe usar epoll para gestionar cientos de conexiones TCP de forma no bloqueante en C.
Desarrollar un sistema concurrente en Erlang, comunicándose con un servidor en C. El ensamble de C y Erlang tiene que ser mediante un protocolo claramente definido.
Aplicar un protocolo de asignación de recursos entre diferentes PC, con memorias distintas, CPU distintas, etc.
Aplicar un algoritmo de prevención de deadlock distribuido.
Lograr la interoperabilidad entre diferentes equipos.
Hacer un readme.

	Descripción general:
	Agente manejador de recursos en C:
El agente actúa como servidor de escucha en un puerto TCP público para recibir peticiones de otros agentes remotos. También actúa como cliente cuando su planificador Erlang necesita un recurso remoto, el agente C inicia una conexión TCP saliente hacia el agente remoto.
Administra los recursos locales.
Mantiene conexiones salientes hacia otros agentes.
Usa epoll, además debe ser usado con multi-thread.
Ofrece una interfaz local para que el proceso Erlang envíe comandos.
Implementa un descubrimiento dinámico de nodos mediante UDP broadcast.
Job Scheduler en Erlang:
Se conecta al servidor de C localmente.
Toma decisiones sobre que job lanzar y que recursos solicitar en cada nodo.
Implementa una estrategia contra deadlocks.
Simula carga de trabajo.

	Protocolos de comunicación:
	Entre agentes de C: RESERVE <job_id> <resource_name> <amount>; GRANTED <job_id>; DENIED <job_id>; RELEASE <job_id> <resource_name> <amount>. Todas líneas ASCII terminadas en \n.
	Interfaz Erlang y C: 
Desde Erlang: JOB_REQUEST <job_id> [@host:res:amount, …]; JOB_RELEASE <job_id>; JOB_STATUS <job_id>.
Desde C: JOB_GRANTED <job_id>; JOB_DENIED <job_id>; JOB_TIMEOUT <job_id>.

Requerimientos funcionales:
Servidor C con epoll: Abrir dos sockets de escucha en el mismo puerto. Uno para conexiones entrantes de otros agentes y otro para conexión local de agentes.
Agregar los descriptores de sockets (escucha, conexiones establecidas, temporizadores). Se procesarán eventos EPOLLIN y EPOLLOUT sin bloqueos.
Usar timerfd para implementar timeouts en reservas pendientes.
La gestión de recursos locales es: Cada recurso tiene una capacidad total, una cantidad disponible y una cola de solicitudes pendientes (identificadas por job_id y socket de origen). Al recibir RESERVE sobre un recurso local: Si hay suficiente disponible se descuenta y responde GRANTED, si no hay se encola la solicitud. Al recibir RELEASE se libera la cantidad, se descuenta el job y se atienden las solicitudes encoladas en ese orden.
El agente debe llevar una tabla con los jobs activos con los recursos concedidos para poder liberarlos en caso de desconexión inesperada.
Lógica del planificador de Erlang: Se conecta al agente y pide la lista de nodos participantes (IP, puerto y recursos disponibles) con el comando GET_NODES, el agente de C responde con algo como: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1;192.168.1.11:8101:cpu:2:mem:4096.
Con esos nodos pide recursos, además debe evitar deadlocks. Por último registra en un archivo log las concesiones, denegaciones y eventos de deadlock.
Interoperabilidad entre agentes: El sistema debe descubrir nodos nuevos y mantener la lista de nodos actualizada. Primero hace un anuncio periódico mediante broadcast UDP con el mensaje: ANNOUNCE <puerto> <recursos>. La IP de cada nodo deberá extraerse de  recvfrom() para obtener la información completa.
Cada agente mantiene en memoria una tabla con entradas: IP, puerto, recursos, timestamp del último anuncio recibido. Si no se recibe un anuncio de un nodo durante 15 segundos se considera caído y se lo elimina de la tabla. La tabla se usa para decidir a qué nodos enviar solicitudes RESERVE.
Al iniciar cada nodo envía un anuncio y espera durante 2 segundos para recibir anuncios de otros nodos ya activos. El socket UDP se agrega al conjunto de epoll para eventos EPOLLIN. Luego comienza a atender peticiones normalmente.
	

#ifndef __SOCKETS_H__
#define __SOCKETS_H__

#include "red/cliente.h"

/*
 * Imprime un mensaje de error del sistema y aborta la ejecución del programa.
 * Recibe un mensaje de contexto a imprimir antes del error de perror.
 */
void quit(char *s);

/*
 * Descubre la IP local de la máquina en la red realizando una conexión UDP simulada.
 * Recibe el buffer donde se escribirá la IP resultante (ej. "192.168.0.x").
 */
void obtener_mi_ip_local(char *buffer_ip);

/*
 * Modifica las flags de un file descriptor para que opere de forma no bloqueante.
 * Recibe el file descriptor a modificar.
 * Devuelve 0 en caso de éxito, o -1 si hubo un error.
 */
int set_nonblocking(int fd);

/*
 * Crea, configura y pone a la escucha un socket TCP no bloqueante.
 * Recibe el puerto de escucha y la dirección IP donde se hará el bind.
 * Devuelve el file descriptor del socket de escucha.
 */
int mk_tcp_lsock(int port, const char* ip);

/*
 * Crea y configura un socket UDP no bloqueante habilitado para recibir broadcasts.
 * Recibe el puerto UDP de escucha.
 * Devuelve el file descriptor del socket UDP.
 */
int mk_udp_lsock(int port);

/*
 * Crea un temporizador no bloqueante utilizando timerfd.
 * Recibe el intervalo en segundos para cada disparo del temporizador.
 * Devuelve el file descriptor asociado al temporizador.
 */
int mk_timer(int segundos);

/*
 * Drena todos los datos disponibles en el socket TCP hasta vaciarlo.
 * Acumula los datos en el buffer interno del cliente. Incluye protección Anti-DoS.
 * Recibe un puntero al cliente del cual se leerán los datos.
 * Devuelve 1 si leyó correctamente, o 0 si el cliente se desconectó o hubo error.
 */
int atender_cliente_tcp(ClienteConectado *cliente);

/*
 * Lee un datagrama UDP entrante y lo formatea con la IP del remitente.
 * Recibe el FD del socket UDP de escucha, el buffer de destino y su tamano máximo.
 * Devuelve 1 si se extrajo un mensaje válido, o 0 si la red estaba vacía.
 */
int atender_cliente_udp(int usock_udp, char *buffer_destino, size_t tamano_maximo);

/*
 * Envía un datagrama UDP a un destino específico.
 * Recibe el FD del socket UDP, la IP de destino (ej. "255.255.255.255"),
 * el puerto y la cadena de texto a enviar.
 * Devuelve 1 si el envío a la red fue exitoso, o -1 en caso de error.
 */
int enviar_mensaje_udp(int usock_udp, const char *ip_destino, int puerto_destino, const char *mensaje);

/*
 * Intenta establecer una conexión TCP no bloqueante con un nodo remoto.
 * Utiliza select() internamente para manejar un timeout máximo de 2 segundos.
 * Recibe la dirección IP y el puerto TCP del nodo remoto.
 * Devuelve el file descriptor conectado en caso de éxito, o -1 si falló.
 */
int conectar_a_nodo(const char *ip_destino, int puerto_destino);

#endif /* __SOCKETS_H__ */

#ifndef __SOCKETS_H__
#define __SOCKETS_H__

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h> /* For O_* constants */
#include <sys/stat.h> /* For mode constants */
#include <sys/mman.h> /* mmap */
#include <sys/wait.h> /* wait */
#include <unistd.h> /* ftruncate */
#include <pthread.h>
#include <sys/epoll.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/timerfd.h> /* Para timerfd_create */
#include <time.h>
#include <errno.h>
#include "cliente.h"

#define MAX_EVENTS 10
#define PUERTO_TCP 4040
#define PUERTO_UDP 12529

void quit(char *s);

void obtener_mi_ip_local(char *buffer_ip); 

int set_nonblocking(int fd);

int mk_tcp_lsock(int port, const char* ip);

int mk_udp_lsock(int port);

int mk_timer(int segundos);

int conectar_a_nodo(const char *ip_destino, int puerto_destino);

int atender_cliente_tcp(ClienteConectado *cliente);

int atender_cliente_udp(int usock_udp, char *buffer_destino, size_t tamaño_maximo);

int enviar_mensaje_tcp(int fd, const char *mensaje);

int enviar_mensaje_udp(int usock_udp, const char *ip_destino, int puerto_destino, const char *mensaje);







#endif /* __SOCKETS_H__ */









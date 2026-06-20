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

#define MAX_EVENTS 10
#define PUERTO 4040

void quit(char *s);

void obtener_mi_ip_local(char *buffer_ip); 

int set_nonblocking(int fd);

int mk_tcp_server(int port, const char* ip);

int mk_udp_server(int port);

int mk_timer(int segundos);














#ifndef _TAXI_UTILS_H_
#define _TAXI_UTILS_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void get_server_addr(const char *ip, struct sockaddr_in *addr);
extern int bind_server(const char *ip, int port);

#ifdef __cplusplus
}
#endif

#endif

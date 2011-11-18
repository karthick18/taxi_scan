#ifndef _TAXI_CLIENT_H_
#define _TAXI_CLIENT_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include "taxi.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int update_taxi_location(struct taxi *taxi);
extern int delete_taxi(struct taxi *taxi);
extern int get_nearest_taxis(double latitude, double longitude,
                             struct taxi **taxis, int *num_taxis);
extern int taxi_client_initialize(const char *ip, int port);

#ifdef __cplusplus
}
#endif

#endif

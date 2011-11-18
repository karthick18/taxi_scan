#ifndef _TAXI_SERVER_H_
#define _TAXI_SERVER_H_

#include "taxi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAXI_DISTANCE_BIAS (0.250)

#define output(...) do { fprintf(stderr, __VA_ARGS__); } while(0)

extern int add_taxi(struct taxi *taxi);
extern int del_taxi(struct taxi *taxi);
extern int find_taxis_by_location(double latitude, double longitude,
                                  struct taxi **matched_taxis, int *num_taxis);

#ifdef __cplusplus
}
#endif

#endif

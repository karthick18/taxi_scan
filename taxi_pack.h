#ifndef _TAXI_PACK_H_
#define _TAXI_PACK_H_

#include "taxi.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char *taxis_pack(struct taxi *taxis, int num_taxis);
extern unsigned char *taxi_pack(struct taxi *taxi);
extern unsigned char *taxi_location_pack(double latitude, double longitude);

extern unsigned char *taxis_pack_with_buf(struct taxi *taxis, int num_taxis,
                                          unsigned char **r_buf, int len, int offset);
extern unsigned char *taxi_pack_with_buf(struct taxi *taxi, unsigned char **r_buf,
                                         int len, int offset);

extern unsigned char *taxi_location_pack_with_buf(double latitude, double longitude,
                                                  unsigned char **r_buf, int len, int offset);
extern int taxi_unpack(unsigned char *buf, int len, struct taxi *taxi);

#ifdef __cplusplus
}
#endif

#endif

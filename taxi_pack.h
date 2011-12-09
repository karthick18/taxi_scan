#ifndef _TAXI_PACK_H_
#define _TAXI_PACK_H_

#include "taxi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __MAX_PACKET_LEN (64000)
#define _TAXI_OVERHEAD (sizeof(unsigned int)*2) /*per entry overhead of 2 words for len and start marker*/
#define _TAXI_CMD_BASE (0x1000)
#define __TAXI_CMD(off) (_TAXI_CMD_BASE + (off))
#define _TAXI_LOCATION_CMD __TAXI_CMD(1)
#define _TAXI_DELETE_CMD   __TAXI_CMD(2)
#define _TAXI_FETCH_CMD    __TAXI_CMD(3)
#define _TAXI_LIST_CMD     __TAXI_CMD(4)
#define _TAXI_PING_CMD     __TAXI_CMD(5)
#define _TAXI_PING_REPLY_CMD __TAXI_CMD(6)
#define _TAXI_PING_INTIMATION_CMD __TAXI_CMD(7)

extern unsigned char *taxis_pack(struct taxi *taxis, int num_taxis);
extern unsigned char *taxi_pack(struct taxi *taxi);
extern unsigned char *taxi_location_pack(double latitude, double longitude);

extern unsigned char *taxis_pack_with_buf(struct taxi *taxis, int num_taxis,
                                          unsigned char **r_buf, int *p_len, int offset);
extern unsigned char *taxi_pack_with_buf(struct taxi *taxi, unsigned char **r_buf,
                                         int *p_len, int offset);

extern unsigned char *taxi_location_pack_with_buf(double latitude, double longitude,
                                                  unsigned char **r_buf, int *p_len, int offset);
extern unsigned char *taxi_list_pack_with_buf(struct taxi *taxis, int num_taxis,
                                              unsigned char **r_buf, int *p_len, int offset);
extern unsigned char *taxi_list_pack(struct taxi *taxis, int num_taxis);
extern unsigned char *taxis_ping_pack_with_buf(struct taxi *customer, struct taxi *taxis, int num_taxis, 
                                               unsigned char **r_buf, int *p_len, int offset);
extern unsigned char *taxis_ping_pack(struct taxi *customer, struct taxi *taxis, int num_taxis);
extern int taxi_unpack(unsigned char *buf, int *p_len, struct taxi *taxi);
extern int taxis_unpack(unsigned char *buf, int *p_len, 
                        struct taxi **p_taxis, int *p_num_taxis);
extern int taxi_list_unpack(unsigned char *buf, int *p_len,
                            struct taxi **p_taxis, int *p_num_taxis);
extern int taxis_ping_unpack(unsigned char *buf, int *p_len, struct taxi *customer,
                             struct taxi **p_taxis, int *p_num_taxis);
#ifdef __cplusplus
}
#endif

#endif

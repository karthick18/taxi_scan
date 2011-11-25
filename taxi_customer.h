#ifndef _TAXI_CUSTOMER_H_
#define _TAXI_CUSTOMER_H_

#include "taxi.h"
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct taxi_customer
{
    struct taxi taxi; /*customer instance*/
    struct taxi *taxis; /*nearest matching taxis*/
    int num_taxis; /* num taxis matching the request*/
    struct list_head taxi_list;/* list head of taxis per customer*/
    struct list_head list; /*marker to the global list*/
    int num_approaching;
};

extern int add_taxis_customer(struct taxi *customer, struct taxi *taxis, int num_taxis);
extern int del_taxi_customer(unsigned char *taxi_id, int taxi_id_len,
                             unsigned char *id, int id_len, struct taxi *result);
extern int find_taxi_customer(unsigned char *taxi_id, int taxi_id_len,
                              unsigned char *id, int id_len, struct taxi *result);
extern int update_taxi_state_customer(unsigned char *taxi_id, int taxi_id_len,
                                      unsigned char *customer_id, int customer_id_len, int new_state);
extern int del_customer(unsigned char *id, int id_len);

extern int get_taxis_excluding_self_customer(unsigned char *id, int id_len, short hint,
                                             struct taxi **p_taxis, int *p_num_taxis);

extern int get_taxis_customer(unsigned char *id, int id_len, struct taxi **p_taxis, int *p_num_taxis);

extern int get_taxis_matching_self_customer(unsigned char *id, int id_len, struct taxi *self);

extern int set_taxi_id(unsigned char *id, int id_len);

extern int set_customer_id(unsigned char *id, int id_len);

#ifdef __cplusplus
}
#endif

#endif

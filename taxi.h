#ifndef _TAXI_H_
#define _TAXI_H_

#include <arpa/inet.h>
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define output(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#define _TAXI_SERVER_PORT (20000)
#define _TAXI_SERVER_IP   "localhost"

struct taxi
{
#define _TAXI_STATE_IDLE (0x1)  /* not serving anyone*/
#define _TAXI_STATE_PICKUP (0x2) /* state to pickup */
#define _TAXI_STATE_ACTIVE (0x4) /* activated and with a customer*/

#define MAX_ID_LEN 20
    double latitude;
    double longitude;
    unsigned char id[MAX_ID_LEN];
    int id_len;
    int state;
    int num_customers;
    struct sockaddr_in addr;
    struct list_head customer_list; /* list of customers for this taxi */
    struct list_head list; /* marker to the global taxi list*/
};

#ifdef __cplusplus
}
#endif

#endif


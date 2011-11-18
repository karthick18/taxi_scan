#ifndef _TAXI_H_
#define _TAXI_H_

#ifdef __cplusplus
extern "C" {
#endif

#define output(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#define _TAXI_SERVER_PORT (20000)
#define _TAXI_SERVER_IP   "localhost"

struct taxi
{
#define MAX_ID_LEN 20
    double latitude;
    double longitude;
    unsigned char id[MAX_ID_LEN];
    int id_len;
};

#ifdef __cplusplus
}
#endif

#endif


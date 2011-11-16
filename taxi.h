#ifndef _TAXI_H_
#define _TAXI_H_

#ifdef __cplusplus
extern "C" {
#endif

#define TAXI_DISTANCE_BIAS (0.250)

#define output(...) do { fprintf(stderr, __VA_ARGS__); } while(0)

struct taxi
{
#define MAX_ID_LEN (20)
    double latitude;
    double longitude;
    unsigned char id[MAX_ID_LEN];
    int id_len;
};

extern int add_taxi(double latitude, double longitude, unsigned char *id, int id_len);
extern int del_taxi(unsigned char *id, int id_len);
extern int find_taxis_by_location(double latitude, double longitude,
                                  struct taxi **matched_taxis, int *num_taxis);
#ifdef __cplusplus
}
#endif

#endif


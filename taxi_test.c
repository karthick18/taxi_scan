#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "taxi.h"
#include "taxi_client.h"

#define TEST_FILE_NAME "locations.txt"

static struct taxi *taxis;
int num_taxis;

static int add_taxi_location(double latitude, double longitude, unsigned char *id, int id_len)
{
    taxis = realloc(taxis, sizeof(*taxis) * (num_taxis+1));
    assert(taxis);
    taxis[num_taxis].latitude = latitude;
    taxis[num_taxis].longitude = longitude;
    int len = id_len > sizeof(taxis[num_taxis].id) ? sizeof(taxis[num_taxis].id) : id_len;
    taxis[num_taxis].id_len = len;
    memcpy(taxis[num_taxis].id, id, len);
    num_taxis++;
    return update_taxi_location(&taxis[num_taxis-1]);
}

static int del_taxis(void)
{
    for(int i = 0; i < num_taxis; ++i)
    {
        int ret = delete_taxi(&taxis[i]);
        assert(ret == 0);
        printf("Taxi [%.*s] with latitude [%lg], longitude [%lg] deleted successfully\n", 
               taxis[i].id_len, taxis[i].id, taxis[i].latitude, taxis[i].longitude);
    }
    free(taxis);
    num_taxis = 0;
    taxis = NULL;
    return 0;
}

static void display_taxis(struct taxi *taxis, int num_taxis)
{
    output("----Displaying [%d] taxis------\n", num_taxis);
    for(int i = 0; i < num_taxis; ++i)
    {
        output(" Taxi [%.*s] found near [%G:%G]\n", taxis[i].id_len, taxis[i].id, 
               taxis[i].latitude, taxis[i].longitude);
    }
}

static int find_taxis(struct taxi *search_taxis, int num_searches)
{
    for(int i = 0; i < num_searches; ++i)
    {
        struct taxi *taxis = NULL;
        int num_taxis = 0;
        get_nearest_taxis(search_taxis[i].latitude,
                          search_taxis[i].longitude,
                          &taxis, &num_taxis);
        printf("Matched [%d] taxis for query [%lg:%lg]\n", num_taxis,
               search_taxis[i].latitude, search_taxis[i].longitude);
        if(num_taxis > 0)
        {
            display_taxis(taxis, num_taxis);
            free(taxis);
        }
    }
    return 0;
}

static int test_taxi_scan(const char *fname)
{
    FILE *fptr;
    fptr = fopen(fname, "r");
    char buf[0xff+1];
    struct taxi *search_taxis = NULL;
    int num_searches = 0;
    while(fgets(buf, sizeof(buf), fptr) != NULL)
    {
        double latitude = 0, longitude = 0;
        unsigned char *id;
        int ret;
        ret = sscanf(buf, "%lg,%lg,%ms\n", &latitude, &longitude, (char**)&id);
        if(ret != 2 && ret != 3)
        {
            printf("sscanf error\n");
            continue;
        }
        /*
         * mark for search.
         */
        if(ret == 2)
        {
            search_taxis = realloc(search_taxis, sizeof(*search_taxis) * (num_searches+1));
            assert(search_taxis);
            search_taxis[num_searches].latitude = latitude;
            search_taxis[num_searches].longitude = longitude;
            search_taxis[num_searches].id_len = 0;
            search_taxis[num_searches++].id[0] = 0;
        }
        else
        {
            add_taxi_location(latitude, longitude, id, strlen((const char*)id));
            free(id);
        }
    }
    fclose(fptr);
    if(num_searches > 0)
    {
        find_taxis(search_taxis, num_searches);
        free(search_taxis);
    }
    del_taxis();
    return 0;
}

int main(int argc, char **argv)
{
    char fname[40] = TEST_FILE_NAME;
    if(argc > 1)
    {
        strncat(fname, argv[1], sizeof(fname)-1);
    }
    int err = taxi_client_initialize(_TAXI_SERVER_IP, _TAXI_SERVER_PORT);
    if(err < 0)
    {
        fprintf(stderr, "Error initializing taxi client\n");
        return -1;
    }
    test_taxi_scan(fname);
    return 0;
}

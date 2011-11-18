#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include "taxi.h"
#include "taxi_client.h"

#define _XSTR(X) #X
#define _STR(X) _XSTR(X)
#define ID_WIDTH _STR(MAX_ID_LEN)

#define TEST_FILE_NAME "locations.txt"

static struct taxi *taxis;
int num_taxis;
static struct taxi_test_args
{
    char server[20];
    int port;
    int test_delete;
    char fname[20];
} taxi_test_args = { .server = _TAXI_SERVER_IP, .port = _TAXI_SERVER_PORT, 
                     .test_delete = 0, .fname = TEST_FILE_NAME ,
};

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
    if(!fptr) 
    {
        output("Unable to open file [%s]\n", fname);
        return -1;
    }
    char buf[0xff+1];
    struct taxi *search_taxis = NULL;
    int num_searches = 0;
    while(fgets(buf, sizeof(buf), fptr) != NULL)
    {
        double latitude = 0, longitude = 0;
        unsigned char id[MAX_ID_LEN+1];
        int ret;
        ret = sscanf(buf, "%lg,%lg,%"ID_WIDTH"s\n", &latitude, &longitude, id);
        if(ret != 2 && ret != 3)
        {
            output("sscanf error\n");
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
            id[MAX_ID_LEN] = 0;
            add_taxi_location(latitude, longitude, id, strlen((const char*)id));
        }
    }
    fclose(fptr);
    if(num_searches > 0)
    {
        find_taxis(search_taxis, num_searches);
        free(search_taxis);
    }
    if(taxi_test_args.test_delete)
        del_taxis();
    return 0;
}

static char *prog;
static void usage(void)
{
    fprintf(stderr, "%s [ -s | server ] [ -p | port ] [ -d | test deletion ] [ -h | this help ]\n",
            prog);
    exit(1);
}

int main(int argc, char **argv)
{
    int c;
    char *s;
    prog = argv[0];
    if( (s = strrchr(prog, '/') ) )
        prog = s+1;

    opterr = 0;
    while ( (c = getopt(argc, argv, "s:p:dh") ) != EOF )
    {
        switch(c)
        {
        case 's':
            taxi_test_args.server[0] = 0;
            strncat(taxi_test_args.server, optarg, sizeof(taxi_test_args.server)-1);
            break;

        case 'p':
            taxi_test_args.port = atoi(optarg);
            break;
            
        case 'd':
            taxi_test_args.test_delete = 1;
            break;

        case 'h':
        case '?':
        default:
            usage();
        }
    }

    if(optind != argc)
    {
        taxi_test_args.fname[0] = 0;
        strncat(taxi_test_args.fname, argv[optind], sizeof(taxi_test_args.fname)-1);
    }

    int err = taxi_client_initialize(taxi_test_args.server, taxi_test_args.port);
    if(err < 0)
    {
        output("Error initializing taxi client\n");
        return -1;
    }
    test_taxi_scan(taxi_test_args.fname);
    return 0;
}

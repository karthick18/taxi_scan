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
#define MAKE_TEST_MASK(m) (1 << (m))
#define CHECK_TEST_MASK(m, b) ( (m) & (1<<(b)) )
static struct taxi *taxis;
int num_taxis;
static struct taxi_test_args
{
#define TEST_ADD  (0x0)
#define TEST_DELETE (0x1)
#define TEST_PING (0x2)
#define TEST_SEARCH (0x3)
    char server[20];
    int port;
    unsigned int test_mask;
    char fname[20];
} taxi_test_args = { .server = _TAXI_SERVER_IP, .port = _TAXI_SERVER_PORT, 
                     .test_mask = MAKE_TEST_MASK(TEST_ADD) | MAKE_TEST_MASK(TEST_SEARCH),
                     .fname = TEST_FILE_NAME ,
};

static int add_taxi_location(double latitude, double longitude, unsigned char *id, int id_len)
{
    int err = 0;
    taxis = realloc(taxis, sizeof(*taxis) * (num_taxis+1));
    assert(taxis);
    taxis[num_taxis].latitude = latitude;
    taxis[num_taxis].longitude = longitude;
    int len = id_len > sizeof(taxis[num_taxis].id) ? sizeof(taxis[num_taxis].id) : id_len;
    taxis[num_taxis].id_len = len;
    memcpy(taxis[num_taxis].id, id, len);
    num_taxis++;
    if(CHECK_TEST_MASK(taxi_test_args.test_mask, TEST_ADD))
        err = update_taxi_location(&taxis[num_taxis-1]);
    return err;
}

static int del_taxis(void)
{
    if(CHECK_TEST_MASK(taxi_test_args.test_mask, TEST_DELETE))
    {
        for(int i = 0; i < num_taxis; ++i)
        {
            int ret = delete_taxi(&taxis[i]);
            assert(ret == 0);
            printf("Taxi [%.*s] with latitude [%lg], longitude [%lg] deleted successfully\n", 
                   taxis[i].id_len, taxis[i].id, taxis[i].latitude, taxis[i].longitude);
        }
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
        output(" Taxi [%.*s] found near [%G:%G] at ip [%s], port [%d]\n", 
               taxis[i].id_len, taxis[i].id, 
               taxis[i].latitude, taxis[i].longitude,
               inet_ntoa(taxis[i].addr.sin_addr), ntohs(taxis[i].addr.sin_port));
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
            if(CHECK_TEST_MASK(taxi_test_args.test_mask, TEST_DELETE) 
               && num_taxis >= 2)
            {
                printf("Re-running the query with deletion of id [%.*s]\n",
                       taxis[0].id_len, taxis[0].id);
                delete_taxi(&taxis[0]);
                i--;
            }
            else if(CHECK_TEST_MASK(taxi_test_args.test_mask, TEST_PING))
            {
                printf("Testing ping command send to the taxis nearby\n");
                struct taxi customer = {0};
                strncat((char*)customer.id, "foobar", sizeof(customer.id)-1);
                customer.id_len = strlen((const char*)customer.id);
                customer.latitude = 38.4;
                customer.longitude = -122.31;
                ping_nearby_taxis(&customer, taxis, num_taxis);
            }
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
        if(CHECK_TEST_MASK(taxi_test_args.test_mask, TEST_SEARCH))
            find_taxis(search_taxis, num_searches);
        free(search_taxis);
    }
    del_taxis();
    return 0;
}

static char *prog;
static void usage(void)
{
    fprintf(stderr, "%s [ -s | server ] [ -p | port ] [ -d | test deletion ] "
            " [ -a | test add ] [ -i | test ping ] [ -f | test search ] [ -h | this help ]\n",
            prog);
    exit(1);
}

int main(int argc, char **argv)
{
    int c;
    unsigned int test_mask = 0;
    int loop = 0;
    char *s;
    prog = argv[0];
    if( (s = strrchr(prog, '/') ) )
        prog = s+1;

    opterr = 0;
    while ( (c = getopt(argc, argv, "s:p:dafihw") ) != EOF )
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
            test_mask |= MAKE_TEST_MASK(TEST_DELETE);
            break;

        case 'a':
            test_mask |= MAKE_TEST_MASK(TEST_ADD);
            break;
            
        case 'i':
            test_mask |= MAKE_TEST_MASK(TEST_PING);
            break;

        case 'f':
            test_mask |= MAKE_TEST_MASK(TEST_SEARCH);
            break;

        case 'w':
            loop = 1;
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
    if(test_mask)
    {
        taxi_test_args.test_mask = test_mask;
    }
    int err = taxi_client_initialize(taxi_test_args.server, taxi_test_args.port);
    if(err < 0)
    {
        output("Error initializing taxi client\n");
        return -1;
    }
    test_taxi_scan(taxi_test_args.fname);
    if(loop)
        for(;;) sleep(3);

    return 0;
}

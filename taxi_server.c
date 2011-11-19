#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include "taxi_utils.h"
#include "taxi_pack.h"
#include "taxi_server.h"

static struct server_args
{
    int port;
    int verbose;
} server_args = {.port = _TAXI_SERVER_PORT, .verbose = 0, };

static void fetch_taxi_list(struct taxi *taxi_location, struct taxi **taxis, int *num_taxis)
{
    find_taxis_by_location(taxi_location->latitude, taxi_location->longitude,
                           taxis, num_taxis);
}

/*
 * Pack and send back the taxi list for this location.
 */
static int send_taxi_list(struct taxi *taxi, struct taxi *taxis, int num_taxis, int sd, 
                          struct sockaddr *dest, socklen_t addrlen)
{
    printf("Matched [%d] taxis for location [%lg:%lg]\n", num_taxis,
           taxi->latitude, taxi->longitude);
    int len = 1024, err = -1;
    unsigned char *buf = calloc(1, len);
    assert(buf);
    unsigned char *s = buf;
    int offset = sizeof(unsigned int) * 2;
    *(unsigned int *)s = htonl(_TAXI_LIST_CMD);
    s += sizeof(unsigned int);
    *(unsigned int*)s = htonl(num_taxis);
    unsigned char *taxis_marker = s;
    s += sizeof(unsigned int);
    int max_taxis =  __MAX_PACKET_LEN/(sizeof(struct taxi)  + _TAXI_OVERHEAD);
    if(num_taxis >= max_taxis) 
    {
        num_taxis = max_taxis;
        *(unsigned int*)taxis_marker = htonl(num_taxis);
    }
    buf = taxis_pack_with_buf(taxis, num_taxis, &buf, &len, offset);
    assert(buf);
    len += offset;
    int nbytes = sendto(sd, buf, len, 0, dest, addrlen);
    if(nbytes != len)
    {
        printf("Couldn't send [%d] bytes to destination\n", len);
        goto out_free;
    }
    err = 0;

    out_free:
    free(buf);
    return err;
}

static int process_request(int sd, unsigned char *buf, int bytes, struct sockaddr_in *dest, socklen_t addrlen)
{
    int err = -1;
    printf("Got [%d] bytes of data from dest [%s], port [%d]\n", 
           bytes, inet_ntoa(dest->sin_addr), ntohs(dest->sin_port));
    unsigned char *s = buf;
    if(bytes < sizeof(unsigned int))
    {
        printf("Request too short\n");
        goto out;
    }
    if(bytes == sizeof(unsigned int))
    {
        /*
         * exit request.
         */
        err = 1;
        goto out;
    }
    unsigned int cmd = ntohl(*(unsigned int*)s);
    bytes -= sizeof(unsigned int);
    s += sizeof(unsigned int);

    switch(cmd)
    {
        /*
         * Location update command.
         */
    case _TAXI_LOCATION_CMD:
        {
            struct taxi taxi = {0};
            err = taxi_unpack(s, &bytes, &taxi);
            if(err < 0)
            {
                printf("Error unpacking taxi location data\n");
                goto out;
            }
            add_taxi(&taxi); /* add the taxi into the db*/
        }
        break;

    case _TAXI_DELETE_CMD:
        {
            
            struct taxi taxi = {0};
            err = taxi_unpack(s, &bytes, &taxi);
            if(err < 0)
            {
                printf("Error unpacking taxi delete command\n");
                goto out;
            }
            printf("Deleting taxi with id [%.*s]\n", taxi.id_len, taxi.id);
            del_taxi(&taxi);
        }
        break;

        /*
         * Return taxis matching the location.
         */
    case _TAXI_FETCH_CMD:
        {
            struct taxi taxi = {0};
            err = taxi_unpack(s, &bytes, &taxi);
            if(err < 0)
            {
                printf("Error unpacking taxi fetch by location command\n");
                goto out;
            }
            struct taxi *taxis = NULL;
            int num_taxis = 0;
            fetch_taxi_list(&taxi, &taxis, &num_taxis);
            send_taxi_list(&taxi, taxis, num_taxis, sd, (struct sockaddr*)dest, addrlen);
            if(taxis) free(taxis);
            break;
        }
        break;
    default:
        break;
    }
    err = 0;
    out:
    return err;
}

int taxi_server_start(const char *ip, int port)
{
    char buf[0xffff+1];
    int err = -1;
    int sd = bind_server(ip, port);
    if(sd < 0) goto out;
    for(;;)
    {
        struct sockaddr_in dest;
        socklen_t addrlen = sizeof(dest);
        memset(&dest, 0, sizeof(dest));
        int nbytes = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr*)&dest, &addrlen);
        if(nbytes <= 0)
        {
            if(nbytes == 0 || errno == EINTR) continue;
            perror("recvfrom server error. exiting:");
            goto out_close;
        }
        if(process_request(sd, (unsigned char*)buf, nbytes, &dest, addrlen) == 1)
        {
            printf("Server exiting...\n");
            break;
        }
    }

    err = 0;
    out_close:
    close(sd);
    out:
    return err;
}

static char *prog;
static void usage(void)
{
    fprintf(stderr, "%s [ -p | port ] [ -v | verbose ]\n", prog);
    exit(1);
}

int main(int argc, char **argv)
{
    int c;
    opterr = 0;
    prog = argv[0];
    char *s;
    if( (s = strrchr(prog, '/') ) )
        prog = ++s;
    while( (c = getopt(argc, argv, "p:vh") ) != EOF )
    {
        switch(c)
        {
        case 'p':
            server_args.port = atoi(optarg);
            break;
        case 'v':
            server_args.verbose = 1;
            break;
        case 'h':
        case '?':
        default:
            usage();
        }
    }
    if(optind != argc) usage();
    taxi_server_start(NULL, server_args.port);
    return 0;
}

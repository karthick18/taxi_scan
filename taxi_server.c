#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
static void get_server_addr(const char *ip, struct sockaddr_in *addr)
{
    struct hostent *h = gethostbyname(ip);
    if(!h)
    {
        addr->sin_addr.s_addr = inet_addr(ip);
    }
    else
    {
        memcpy(&addr->sin_addr, *h->h_addr_list, h->h_length);
    }
}

static int bind_server(const char *ip, int port)
{
    int sd, err = -1;
    struct sockaddr_in addr;
    sd = socket(PF_INET, SOCK_DGRAM, 0);
    if(sd < 0)
        goto out;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = PF_INET;
    addr.sin_port = htons(port);
    if(ip)
        get_server_addr(ip, &addr);
    else
        addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(sd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind server error:");
        goto out_close;
    }

    err = sd;
    goto out;

    out_close: 
    close(sd);
    out:
    return err;
}

static void fetch_taxis(struct taxi *taxi_location, struct taxi **taxis, int *num_taxis)
{
    find_taxis_by_location(taxi_location->latitude, taxi_location->longitude,
                           taxis, num_taxis);
}

/*
 * Pack and send back the taxi list for this location.
 */
static int send_taxis(struct taxi *taxi, struct taxi *taxis, int num_taxis, int sd, 
                      struct sockaddr_in *dest, socklen_t addrlen)
{
#define _BUF_SPACE (1024)
    printf("Matched [%d] taxis for location [%lg:%lg]\n", num_taxis,
           taxi->latitude, taxi->longitude);
    unsigned char *buf = calloc(1, _BUF_SPACE);
    assert(buf);
    unsigned char *s = buf;
    _CHECK_SPACE(sizeof(unsigned int)*2);
    *(unsigned int *)s = _TAXI_LIST_CMD;
    s += sizeof(unsigned int);
    *(unsigned int*)s = num_taxis;
    s += sizeof(unsigned int);
    if(num_taxis > 0)
    {
    }
    return 0;
}

static int process_request(int sd, unsigned char *buf, int bytes, struct sockaddr_in *dest, socklen_t addrlen)
{
    int err = -1;
    printf("Got [%d] bytes of data from dest [%s], port [%d]\n", 
           bytes, inet_ntoa(dest->sin_addr), ntohs(dest->sin_port));
    unsigned char *s = (unsigned char *)buf;
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
    unsigned int cmd = *(unsigned int*)s;
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
            err = taxi_unpack(s, bytes, &taxi);
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
            err = taxi_unpack(s, bytes, &taxi);
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
            err = taxi_unpack(s, bytes, &taxi);
            if(err < 0)
            {
                printf("Error unpacking taxi fetch by location command\n");
                goto out;
            }
            struct taxi *taxis = NULL;
            int num_taxis = 0;
            fetch_taxis(&taxi, &taxis, &num_taxis);
            send_taxis(&taxi, taxis, num_taxis, sd, dest, addrlen);
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
        if(process_request(sd, buf, nbytes, &dest, addrlen) == 1)
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

static int taxi_client_send(const char *ip, int port)
{
    int sd;
    int err = -1;
    struct sockaddr_in destaddr;
    sd = bind_server(NULL, 0);
    if(sd < 0)
        goto out;
    memset(&destaddr, 0, sizeof(destaddr));
    destaddr.sin_port = htons(port);
    destaddr.sin_family = PF_INET;
    get_server_addr(ip, &destaddr);
    for(int i = 0; i < 100; ++i)
    {
        char buf[40];
        if(i != 99)
            snprintf(buf, sizeof(buf), "hello:%d", i+1);
        else snprintf(buf, sizeof(buf), "exit");
        int nbytes;
        retry:
        nbytes = sendto(sd, buf, strlen(buf), 0, (struct sockaddr*)&destaddr, sizeof(destaddr));
        if(nbytes < 0)
        {
            if(errno == EINTR)
                goto retry;
            perror("sendto server error:"); 
            goto out_close;
        }
        else printf("Sent [%d] bytes successfully to [%s]\n", nbytes, inet_ntoa(destaddr.sin_addr));
        if(i != 99)
        {
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            socklen_t addrlen = sizeof(server_addr);
            nbytes = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr*)&server_addr, &addrlen);
            if(nbytes > 0)
                process_request("CLIENT", buf, nbytes, &server_addr);
        }
    }
    err = 0;
    out_close:
    close(sd);
    out:
    return err;
}

int main(int argc, char **argv)
{
    char *server = NULL;
    char *client = "localhost";
    int port = 20000;
    if(argc > 1) client = argv[1];
    if(argc > 2) port = atoi(argv[2]);
    if(fork()==0)
    {
        taxi_server_start(server, port); 
        exit(0);
    }
    sleep(1);
    if(fork() == 0)
    {
        taxi_client_send(client, port);
        exit(0);
    }
    while( waitpid(-1, NULL, 0) != -1 ); /* wait till all children exit*/
    printf("done with the tests...\n");
    return 0;
}

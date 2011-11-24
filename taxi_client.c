#include "taxi_client.h"
#include "taxi_utils.h"
#include "taxi_pack.h"
#include "taxi_customer.h"
#include "dispatcher.h"
#include <poll.h>

#define _TAXI_LIST_TIMEOUT (2000) /* 1 second response timeout from the server*/

static struct sockaddr_in client_addr;
static socklen_t client_addrlen = sizeof(client_addr);

static int send_taxis_ping_cmd(struct taxi *customer, struct taxi *taxis, int num_taxis, int sd)
{
    int len = 1024;
    unsigned char *buf = calloc(1, len);
    assert(buf != NULL);
    unsigned char *s = buf;
    *(unsigned int*)s = htonl(_TAXI_PING_CMD);
    s += sizeof(unsigned int);
    buf = taxis_ping_pack_with_buf(customer, taxis, num_taxis, &buf, &len, sizeof(unsigned int));
    assert(buf != NULL);
    int cur_packed = len + sizeof(unsigned int);
    for(int i = 0; i < num_taxis; ++i)
    {
        int bytes = sendto(sd, buf, cur_packed, 0, (struct sockaddr*)&taxis[i].addr,
                           sizeof(taxis[i].addr));
        if(bytes != cur_packed)
        {
            output("Error sending taxi list to taxi [%.*s] at [%s] near [%lg:%lg]\n",
                   taxis[i].id_len, taxis[i].id, inet_ntoa(taxis[i].addr.sin_addr),
                   taxis[i].latitude, taxis[i].longitude);
        }
    }
    free(buf);
    return 0;
}

/*
 * pack a fetch request.
 */
static int send_taxi_fetch_cmd(double latitude, double longitude,
                               struct taxi **p_taxis, int *p_num_taxis,
                               int fd, struct sockaddr_in *dest, socklen_t dest_addrlen)
{
    struct taxi taxi = {0};
    taxi.latitude = latitude;
    taxi.longitude = longitude;
    int len = 1024, err = -1;
    unsigned char *buf = calloc(1, len);
    assert(buf);
    unsigned char *s = buf;
    *(unsigned int*)s = htonl(_TAXI_FETCH_CMD);
    s += sizeof(unsigned int);
    buf = taxi_pack_with_buf(&taxi, &buf, &len, s - buf);
    assert(buf);
    len += sizeof(unsigned int);
    int sd = socket(PF_INET, SOCK_DGRAM, 0);
    int nbytes = sendto(sd, buf, len, 0, (struct sockaddr*)dest, dest_addrlen);
    if(nbytes != len)
    {
        printf("Unable to send taxi fetch command to server at [%s]\n",
               inet_ntoa(dest->sin_addr));
        goto out_free;
    }
    len = __MAX_PACKET_LEN;
    buf = realloc(buf, len);
    assert(buf);
    struct sockaddr_in server_addr;
    socklen_t addrlen = sizeof(server_addr);
    struct pollfd pollfds;
    int status;
    memset(&pollfds, 0, sizeof(pollfds));
    pollfds.events = POLLIN | POLLRDNORM;
    pollfds.fd = sd;
    status = poll(&pollfds, 1, _TAXI_LIST_TIMEOUT);
    if(status <= 0 || !(pollfds.revents & (POLLIN | POLLRDNORM)))
    {
        printf("Unable to receive response from server at [%s] for TAXI LIST COMMAND\n",
               inet_ntoa(dest->sin_addr));
        goto out_free;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    nbytes = recvfrom(sd, buf, len, 0, (struct sockaddr*)&server_addr, &addrlen);
    if(nbytes < 0)
    {
        perror("recvfrom ERROR while trying to fetch taxi locations:");
        goto out_free;
    }
    struct taxi *taxis = NULL;
    int num_taxis = 0;
    err = taxi_list_unpack(buf, &nbytes, &taxis, &num_taxis);
#if 0
    printf("Received [%d] taxis near location [%lg:%lg]\n", num_taxis, latitude, longitude);
    for(int i = 0; i < num_taxis; ++i)
    {
        printf("Received Taxi [%.*s] at location [%lg:%lg]\n", 
               taxis[i].id_len, taxis[i].id, taxis[i].latitude, taxis[i].longitude);
    }
#endif
    if(p_taxis) 
    {
        *p_taxis = taxis;
        taxis = NULL;
    }
    if(p_num_taxis)
        *p_num_taxis = num_taxis;

    if(taxis) free(taxis);
    
    out_free:
    free(buf);
    close(sd);
    return err;
}

static int send_taxi_location_cmd(struct taxi *taxi, int sd, struct sockaddr_in *dest, socklen_t dest_addrlen)
{
    int err = -1;
    int len = 1024;
    unsigned char *buf = calloc(1, len);
    assert(buf);
    unsigned char *s = buf;
    *(unsigned int*)s = htonl(_TAXI_LOCATION_CMD);
    s += sizeof(unsigned int);
    buf = taxi_pack_with_buf(taxi, &buf, &len, s - buf);
    assert(buf);
    len += sizeof(unsigned int);
    int nbytes = sendto(sd, buf, len, 0, (struct sockaddr*)dest, dest_addrlen);
    if(nbytes  != len)
    {
        printf("Location command send to server [%s] didn't succeed\n", 
               inet_ntoa(dest->sin_addr));
        goto out_free;
    }
    printf("Location [%lg:%lg] successfully updated for taxi [%.*s]\n",
           taxi->latitude, taxi->longitude, taxi->id_len, taxi->id);
    err = 0;
    out_free:
    free(buf);
    return err;
}

static int send_taxi_delete_cmd(struct taxi *taxi, int sd, struct sockaddr_in *dest, socklen_t dest_addrlen)
{
    int len = 1024, err = -1;
    unsigned char *buf = calloc(1, len);
    assert(buf);
    unsigned char *s = buf;
    *(unsigned int*)s = htonl(_TAXI_DELETE_CMD);
    s += sizeof(unsigned int);
    buf = taxi_pack_with_buf(taxi, &buf, &len, s - buf);
    assert(buf);
    len += sizeof(unsigned int);
    int nbytes = sendto(sd, buf, len, 0, (struct sockaddr*)dest, dest_addrlen);
    if(nbytes != len)
    {
        printf("Unable to send delete taxi command to the server at [%s] for taxi [%.*s]\n",
               inet_ntoa(dest->sin_addr), taxi->id_len, taxi->id);
        goto out_free;
    }
    err = 0;
    out_free:
    free(buf);
    return err;
}

/*
 * Update the taxi location to the server.
 */
static int client_fd;
static struct sockaddr_in server_addr;
static int client_initialized;

int update_taxi_location(struct taxi *taxi)
{
    int err = -1;
    if(!client_initialized)
    {
        printf("Taxi client uninitialized\n");
        goto out;
    }
    memcpy(&taxi->addr, &client_addr, sizeof(taxi->addr));
    err = send_taxi_location_cmd(taxi, client_fd, &server_addr, sizeof(server_addr));
    out:
    return err;
}

int delete_taxi(struct taxi *taxi)
{
    int err = -1;
    if(!client_initialized) 
    {
        printf("Taxi client uninitialized\n");
        goto out;
    }
    memcpy(&taxi->addr, &client_addr, sizeof(taxi->addr));
    err = send_taxi_delete_cmd(taxi, client_fd, &server_addr, sizeof(server_addr));
    out:
    return err;
}

int get_nearest_taxis(double latitude, double longitude,
                      struct taxi **taxis, int *num_taxis)
{
    int err = -1;
    if(!client_initialized)
    {
        printf("Taxi client uninitialized\n");
        goto out;
    }
    err = send_taxi_fetch_cmd(latitude, longitude, taxis, num_taxis,
                              client_fd, &server_addr, sizeof(server_addr));
    out:
    return err;
}

int ping_nearby_taxis(struct taxi *customer, struct taxi *taxis, int num_taxis)
{
    int err = -1;
    if(!client_initialized)
    {
        printf("Taxi client uninitialized\n");
        goto out;
    }
    if(!taxis || !num_taxis || !customer)
    {
        printf("No taxis to send ping command\n");
        goto out;
    }
    /*
     * Create a customer taxi list for retrieval.
     */
    err = add_taxis_customer(customer, taxis, num_taxis); 
    if(err < 0)
    {
        printf("Error creating customer taxi list before ping command\n");
        goto out;
    }

    err = send_taxis_ping_cmd(customer, taxis, num_taxis, client_fd);

    out:
    return err;
}

static int taxi_client_dispatcher(int fd, void *arg)
{
#define _CHECK_SPACE(sp) do { len -= (sp); if(len < 0) goto out; } while(0)
    unsigned char buf[0xffff+1];
    int len, err = -1;
    struct sockaddr_in dest;
    socklen_t addrlen = sizeof(dest);
    memset(&dest, 0, sizeof(dest));
    retry:
    len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&dest, &addrlen);
    if(len <= 0)
    {
        if(len < 0)
        {
            if(errno == EINTR)
                goto retry;
            fprintf(stderr, "recvfrom failed with [%s]\n", strerror(errno));
            goto out;
        }
        goto out;
    }
    output("Got [%d] bytes on the client\n", len);
    unsigned char *s = buf;
    _CHECK_SPACE(sizeof(unsigned int));
    int cmd = ntohl(*(unsigned int*)s);
    s += sizeof(unsigned int);

    switch(cmd)
    {
    case _TAXI_PING_CMD:
        {
            struct taxi *taxis = NULL;
            int num_taxis = 0;
            struct taxi customer = {0};
            err = taxis_ping_unpack(s, &len, &customer, &taxis, &num_taxis);
            if(err < 0)
            {
                fprintf(stderr, "Taxi ping command unpack failed\n");
                goto out;
            }
            printf("Got ping command from customer [%.*s] at [%lg:%lg] for [%d] taxis\n",
                   customer.id_len, customer.id, customer.latitude, customer.longitude,
                   num_taxis);
            for(int i = 0; i < num_taxis; ++i)
            {
                printf("Ping command with taxi [%.*s] traced at location [%lg:%lg]\n",
                       taxis[i].id_len, taxis[i].id, taxis[i].latitude, taxis[i].longitude);
            }
            free(taxis);
        }
        break;
    default:
        printf("Unknown command [%d] received\n", cmd);
        break;
    }

    err = 0;
    
    out:
    return err;

#undef _CHECK_SPACE
}

int taxi_client_initialize(const char *ip, int port)
{
    int sd;
    int err = -1;
    if(client_initialized)
    {
        err = 0;
        goto out;
    }
    err = dispatcher_initialize();
    if(err < 0)
    {
        fprintf(stderr, "Dispatcher initialize failed\n");
        goto out;
    }
    sd = bind_server(NULL, 0);
    if(sd < 0)
        goto out_finalize;
    client_fd = sd;
    err = dispatcher_register(sd, 0, NULL, taxi_client_dispatcher);
    if(err < 0)
    {
        fprintf(stderr, "Taxi dispatcher register failed\n");
        goto out_close;
    }
    server_addr.sin_port = htons(port);
    server_addr.sin_family = PF_INET;
    get_server_addr(ip, &server_addr);
    getsockname(client_fd, &client_addr, &client_addrlen);
    printf("Local client address [%s], port [%d]\n", inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));
    client_initialized = 1;
    err = 0;
    goto out;

    out_close:
    close(sd);

    out_finalize:
    dispatcher_finalize();

    out:
    return err;
}
 

#include "taxi_client.h"
#include "taxi_utils.h"
#include "taxi_pack.h"
#include <poll.h>

#define _TAXI_LIST_TIMEOUT (1000) /* 1 second response timeout from the server*/

static int unpack_taxi_list(unsigned char *buf, int len, 
                            struct taxi **p_taxis, int *p_num_taxis)
{
#define _CHECK_SPACE(sp) do { len -= (sp); if(len < 0) goto out; }while(0)
    int err = -1;
    unsigned char *s = buf;
    _CHECK_SPACE(sizeof(unsigned int));
    int cmd = ntohl(*(unsigned int*)s);
    if(cmd != _TAXI_LIST_CMD)
    {
        printf("Received unexpected response from server for taxi list\n");
        goto out;
    }
    s += sizeof(unsigned int);
    err = taxis_unpack(s, &len, p_taxis, p_num_taxis);
    if(err < 0)
        goto out;

    out:
    return err;
#undef _CHECK_SPACE
}

/*
 * pack a fetch request.
 */
static int send_taxi_fetch_cmd(double latitude, double longitude,
                               struct taxi **p_taxis, int *p_num_taxis,
                               int sd, struct sockaddr_in *dest, socklen_t dest_addrlen)
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
    err = unpack_taxi_list(buf, nbytes, &taxis, &num_taxis);
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

int taxi_client_initialize(const char *ip, int port)
{
    int sd;
    int err = -1;
    sd = bind_server(NULL, 0);
    if(sd < 0)
        goto out;
    client_fd = sd;
    server_addr.sin_port = htons(port);
    server_addr.sin_family = PF_INET;
    get_server_addr(ip, &server_addr);
    client_initialized = 1;
    err = 0;
    out:
    return err;
}
 

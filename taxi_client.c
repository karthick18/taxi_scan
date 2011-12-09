#include "taxi_client.h"
#include "taxi_utils.h"
#include "taxi_pack.h"
#include "taxi_customer.h"
#include "dispatcher.h"
#include <poll.h>

#define _TAXI_LIST_TIMEOUT (2000) /* 1 second response timeout from the server*/

static struct sockaddr_in client_addr;
static socklen_t client_addrlen = sizeof(client_addr);
static taxi_hook_t g_taxi_hook;

static int send_taxi_ping_reply_cmd(struct taxi *customer, struct taxi *me, int sd)
{
    int err = -1;
    int len = 1024;
    unsigned char *buf = calloc(1, len);
    assert(buf != NULL);
    *(unsigned int*)buf = htonl(_TAXI_PING_REPLY_CMD);
    buf = taxis_ping_pack_with_buf(customer, me, 1, &buf, &len, sizeof(unsigned int));
    assert(buf != NULL);
    len += sizeof(unsigned int);
    /*
     * Tell the customer that the taxi has accepted the request.
     */
    int bytes = sendto(sd, buf, len, 0, (struct sockaddr*)&customer->addr, sizeof(customer->addr));
    if(bytes != len)
    {
        output("Taxi [%.*s] cannot contact customer [%.*s] at address [%s:%d] with ping ack\n",
               me->id_len, me->id, 
               customer->id_len, customer->id,
               inet_ntoa(customer->addr.sin_addr), ntohs(customer->addr.sin_port));
        goto out_free;
    }
    if(!(me->state & (_TAXI_STATE_ACTIVE | _TAXI_STATE_PICKUP)))
    {
        err = update_taxi_state_customer(me->id, me->id_len, customer->id, customer->id_len, _TAXI_STATE_ACTIVE);
        if(err < 0)
        {
            output("Error activating state of taxi [%.*s] for customer [%.*s]\n", 
                   me->id_len, me->id, customer->id_len, customer->id);
            goto out_free;
        }
    }
    /*
     * Tell the remaining taxis for this customer about our pickup
     */
    struct taxi *not_mes = NULL;
    int num_notmes = 0;
    get_taxis_excluding_self_customer(customer->id, customer->id_len, client_addr.sin_port,
                                      &not_mes, &num_notmes);
    if(num_notmes > 0)
    {
        /*
         * Send intimation to the selected peers.
         */
        *(unsigned int*)buf = htonl(_TAXI_PING_INTIMATION_CMD);
        for(int i = 0; i < num_notmes; ++i)
        {
            if(sendto(sd, buf, len, 0, (struct sockaddr*)&not_mes[i].addr, sizeof(not_mes[i].addr)) != len)
            {
                output("Error sending ping intimation to peer taxi [%.*s]\n",
                       not_mes[i].id_len, not_mes[i].id);
            }
            else 
            {
                printf("Sent ping intimation to peer taxi [%.*s] at [%s:%d]\n",
                       not_mes[i].id_len, not_mes[i].id, 
                       inet_ntoa(not_mes[i].addr.sin_addr), ntohs(not_mes[i].addr.sin_port));
            }
        }
        free(not_mes);
    }
    err = 0;

    out_free:
    free(buf);
    return err;
}    

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

static int process_taxi_ping_request(struct taxi *customer, struct taxi *taxis, int num_taxis)
{
    int err = 0;
    if(err) goto out;
    struct taxi self, *p_self = NULL;
    err = find_taxi_by_hint(client_addr.sin_port, &self);
    if(err == 0)
    {
        p_self = &self;
        if(self.state & (_TAXI_STATE_ACTIVE | _TAXI_STATE_PICKUP))
        {
            /*
             * Check if this customer is part of the taxi list.
             */
            if(find_customer_taxi(self.id, self.id_len, customer->id, customer->id_len, NULL))
            {
                output("Ignoring customer request as taxi [%.*s] is already active serving another customer\n",
                       self.id_len, self.id);
                goto out;
            }
        }
    }
    err = add_taxis_customer(customer, taxis, num_taxis);
    if(err) goto out;

    if(!p_self)
    {
        err = get_taxi_matching_self_customer(customer->id, customer->id_len, 
                                              client_addr.sin_port, &self);
        if(err)
        {
            printf("No taxi matching self for customer [%.*s]\n", customer->id_len, customer->id);
            goto out;
        }

        printf("Our taxi id [%.*s], port [%d]\n", self.id_len, self.id, ntohs(client_addr.sin_port));

        /*
         * Now find the number of taxis approaching the customer.
         */
        int num_approaching = get_taxis_approaching_customer(customer->id, customer->id_len);
        if(num_approaching >= 2)
        {
            printf("Already [%d] approaching the customer [%.*s]. Backing out\n", num_approaching,
                   customer->id_len, customer->id);
            goto out;
        }
    }

    /*
     * If there are hooks registered, invoke them for the cmd.
     * These hooks could update the location information for the taxi from the mobile interface.
     */
    if(g_taxi_hook) 
    {
        g_taxi_hook(_TAXI_PING_CMD, customer, &self, 1);
    }
    err = send_taxi_ping_reply_cmd(customer, &self, client_fd);

    out:
    return err;
}

static int process_taxi_ping_reply_request(int cmd, struct taxi *customer, struct taxi *peer)
{
    int err;
    err = add_taxis_customer(customer, peer, 1);
    if(err < 0)
    {
        output("Unable to add peer taxi [%.*s] to taxi map for customer [%.*s]\n",
               peer->id_len, peer->id, customer->id_len, customer->id);
        goto out;
    }

    err = update_taxi_state_customer(peer->id, peer->id_len, 
                                     customer->id, customer->id_len, peer->state);

    if(g_taxi_hook)
    {
        g_taxi_hook(cmd, customer, peer, 1);
    }

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
                output("Taxi ping command unpack failed\n");
                goto out;
            }
            /*
             * Copy the customer address.
             */
            memcpy(&customer.addr, &dest, sizeof(customer.addr));
            printf("Got ping command from customer [%.*s] at [%lg:%lg] for [%d] taxis at [%s]\n",
                   customer.id_len, customer.id, customer.latitude, customer.longitude,
                   num_taxis, inet_ntoa(dest.sin_addr));
            for(int i = 0; i < num_taxis; ++i)
            {
                printf("Ping command with taxi [%.*s] traced at location [%lg:%lg]\n",
                       taxis[i].id_len, taxis[i].id, taxis[i].latitude, taxis[i].longitude);
            }
            /*
             * Add the list to the customer list.
             */
            process_taxi_ping_request(&customer, taxis, num_taxis);
            free(taxis);
        }
        break;
    case _TAXI_PING_INTIMATION_CMD:
    case _TAXI_PING_REPLY_CMD:
        {
            struct taxi *taxis = NULL;
            int num_taxis = 0;
            struct taxi customer = {0};
            err = taxis_ping_unpack(s, &len, &customer, &taxis, &num_taxis);
            if(err < 0)
            {
                output("Taxi ping intimation cmd unpack failed\n");
                goto out;
            }
            assert(num_taxis == 1);
            printf("Got ping [%s] from peer taxi [%.*s] for customer [%.*s]\n",
                   cmd == _TAXI_PING_REPLY_CMD ? "reply" : "intimation",
                   taxis->id_len, taxis->id, customer.id_len, customer.id);
            process_taxi_ping_reply_request(cmd, &customer, taxis);
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
    getsockname(client_fd, (struct sockaddr*)&client_addr, &client_addrlen);
    printf("Local client address [%s], port [%d]\n", inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));
    client_initialized = 1;

    /*
     * load the cache
     */
    get_if_addrs(NULL, NULL);
    
    err = 0;
    goto out;

    out_close:
    close(sd);

    out_finalize:
    dispatcher_finalize();

    out:
    return err;
}
 
int taxi_client_register_hook(taxi_hook_t hook)
{
    int err = -1;

    if(!hook) goto out;
    if(!g_taxi_hook) 
    {
        err = 0;
        g_taxi_hook = hook;
    }

    out:
    return err;
}

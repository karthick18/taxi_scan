#include "taxi_utils.h"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

static struct sockaddr *g_addresses;
static int g_num_addresses;

#ifdef __linux__

uint64_t forward_clock(void)
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000LL + (uint64_t)ts.tv_nsec/1000;
}

#else

uint64_t forward_clock(void)
{
    struct timeval ts = {0};
    gettimeofday(&ts, NULL);
    return (uint64_t)ts.tv_sec * 1000000LL + ts.tv_usec;
}

#endif

int get_if_addrs(struct sockaddr **p_addresses, int *p_num_addresses)
{
#define _CACHE_QUERY_TIME (60*1000000LL)
    struct ifreq *req;
    struct ifconf ifconf;
    static int sd = -1;
    static uint64_t last_time;
    uint64_t cur_time;
    int err = -1;

    if(p_addresses && !p_num_addresses)
        goto out;

    if(sd < 0)
    {
        sd = socket(PF_INET, SOCK_DGRAM, 0);
        if(sd < 0)
            goto out;
    }

    memset(&ifconf, 0, sizeof(ifconf));
    cur_time = forward_clock();
    if(g_addresses && last_time &&
       (cur_time - last_time) <= _CACHE_QUERY_TIME)
    {
        last_time = cur_time;
        err = 0;
        if(p_addresses)
        {
            goto out_copy;
        }
        goto out;
    }

    last_time = cur_time;
    if(g_addresses) 
    {
        free(g_addresses);
        g_addresses = NULL;
    }
    g_num_addresses = 0;
    int num_reqs = 0;
    do
    {
        num_reqs += 5;
        ifconf.ifc_len = sizeof(*req) * num_reqs;
        ifconf.ifc_buf = realloc(ifconf.ifc_buf, ifconf.ifc_len);
        assert(ifconf.ifc_buf != NULL);
        err = ioctl(sd, SIOCGIFCONF, &ifconf);
        if(err < 0)
        {
            perror("ioctl: SIOCGIFCONF");
            goto out_free;
        }
    } while(ifconf.ifc_len == sizeof(*req) * num_reqs);

    int i = 0;
    for(req = ifconf.ifc_req, i = 0;
        i < ifconf.ifc_len; 
        ++req, i += sizeof(*req))
    {
        char name[IFNAMSIZ];
        name[0] = 0;
        strncat(name, req->ifr_name, sizeof(name)-1);
        err = ioctl(sd, SIOCGIFADDR, req);
        if(err < 0)
        {
            perror("ioctl: SIOCGIFADDR");
            goto out_free;
        }
        g_addresses = realloc(g_addresses, sizeof(*g_addresses) * (g_num_addresses+1));
        assert(g_addresses != NULL);
        memcpy(g_addresses + g_num_addresses, &req->ifr_addr, sizeof(*g_addresses));
        g_num_addresses++;
    }
    err = 0;
    
    out_copy:
    if(p_addresses)
    {
        *p_addresses = calloc(g_num_addresses, sizeof(**p_addresses));
        assert(*p_addresses != NULL);
        memcpy(*p_addresses, g_addresses, sizeof(*g_addresses) * g_num_addresses);
        *p_num_addresses = g_num_addresses;
    }

    out_free:
    if(ifconf.ifc_buf) free(ifconf.ifc_buf);

    out:
    return err;

#undef _CACHE_QUERY_TIME
}

void get_server_addr(const char *ip, struct sockaddr_in *addr)
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

int bind_server(const char *ip, int port)
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

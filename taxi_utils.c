#include "taxi_utils.h"

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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <assert.h>
#include "taxi_pack.h"

#define _TAXI_TYPE_START (0x1)
#define _TAXI_TYPE_ID (0x2)
#define _TAXI_TYPE_LOCATION (0x3)
#define _TAXI_TYPE_STATE (0x4)
#define _TAXI_TYPE_CUSTOMERS (0x5)
#define _TAXI_TYPE_ADDR (0x6)

/*
 * Pack the result into a buffer for sending over the wire.
 */
static unsigned char *__taxis_pack(struct taxi *taxis, int num_taxis,
                                   unsigned char **r_buf,
                                   int *p_len, int offset)
{
#define _BUF_SPACE (1024)
#define _CHECK_SPACE(sp) do {                           \
        if( (space - (sp) ) < 0 )                       \
        {                                               \
            ++extents;                                  \
            int _len_span = s - buf;                    \
            int _marker_span = taxi_len_marker - buf;   \
            buf = realloc(buf, extents * len);          \
            assert(buf);                                \
            space = len;                                \
            s = buf + _len_span;                        \
            taxi_len_marker = buf + _marker_span;       \
        }                                               \
        space -= (sp);                                  \
    }while(0)

    int len = _BUF_SPACE;
    if(p_len && *p_len) len = *p_len;
    unsigned char *buf = r_buf ? *r_buf : NULL;
    if(!buf) buf = calloc(1, len);
    unsigned char *s = buf + offset;
    unsigned char *taxi_len_marker = s;
    int space = len - offset, extents = 1;
    assert(buf);

    for(int i = 0; i < num_taxis; ++i)
    {
        _CHECK_SPACE(sizeof(unsigned int)*2);
        *(unsigned int*)s = htonl(_TAXI_TYPE_START); /*start marker*/
        s += sizeof(unsigned int);
        taxi_len_marker = s;
        s += sizeof(unsigned int); /* step over len*/
        if(taxis[i].id && taxis[i].id_len > 0)
        {
            _CHECK_SPACE(sizeof(unsigned int)*2);
            *(unsigned int*)s = htonl(_TAXI_TYPE_ID);
            s += sizeof(unsigned int);
            int len = taxis[i].id_len;
            int alen = (len+sizeof(unsigned int)-1) & ~(sizeof(unsigned int)-1);
            *(unsigned int*)s = htonl(len);
            s += sizeof(unsigned int);
            _CHECK_SPACE(alen);
            memcpy(s, taxis[i].id, len);
            s += alen;
        }
        _CHECK_SPACE(sizeof(unsigned int));
        *(unsigned int*)s = htonl(_TAXI_TYPE_LOCATION);
        s += sizeof(unsigned int);
        double latitude =  taxis[i].latitude;
        double longitude = taxis[i].longitude;
        _CHECK_SPACE(2*sizeof(double));
        *(double *)s = latitude;
        s += sizeof(double);
        *(double *)s = longitude;
        s += sizeof(double);

        _CHECK_SPACE(4*sizeof(unsigned int));
        *(unsigned int*)s = htonl(_TAXI_TYPE_STATE);
        s += sizeof(unsigned int);
        *(int*)s = htonl(taxis[i].state);
        s += sizeof(int);
        *(unsigned int*)s = htonl(_TAXI_TYPE_CUSTOMERS);
        s += sizeof(unsigned int);
        *(int*)s = htonl(taxis[i].num_customers);
        s += sizeof(int);

        /*
         * Address already in network order
         */
        _CHECK_SPACE(3*sizeof(unsigned int));
        *(unsigned int*)s = htonl(_TAXI_TYPE_ADDR);
        s += sizeof(unsigned int);
        *(unsigned int *)s = taxis[i].addr.sin_addr.s_addr;
        s += sizeof(unsigned int);
        unsigned int port = (unsigned int)taxis[i].addr.sin_port;
        *(unsigned int *)s = port;
        s += sizeof(unsigned int);
        /*
         * Update entry len
         */
        *(unsigned int*)taxi_len_marker = 
            htonl((unsigned int)(s - (unsigned char*)taxi_len_marker - sizeof(unsigned int)));
    }

    if(r_buf)
        *r_buf = buf;
    if(p_len) *p_len = s - buf - offset; /* bytes packed */
    return buf;

#undef _CHECK_SPACE
#undef _BUF_SPACE
}

unsigned char *taxis_pack(struct taxi *taxis, int num_taxis)
{
    return __taxis_pack(taxis, num_taxis, NULL, NULL, 0);
}

unsigned char *taxi_pack(struct taxi *taxi)
{
    return __taxis_pack(taxi, 1, NULL, NULL, 0);
}

unsigned char *taxi_location_pack(double latitude, double longitude)
{
    struct taxi taxi = {.id = {0,}, .id_len = 0, .latitude = latitude, .longitude = longitude };
    return __taxis_pack(&taxi, 1, NULL, NULL, 0);
}

unsigned char *taxis_pack_with_buf(struct taxi *taxis, int num_taxis,
                                   unsigned char **r_buf, int *p_len, int offset)
{
    return __taxis_pack(taxis, num_taxis, r_buf, p_len, offset);
}

unsigned char *taxi_pack_with_buf(struct taxi *taxi, unsigned char **r_buf, 
                                  int *p_len, int offset)
{
    return __taxis_pack(taxi, 1, r_buf, p_len, offset);
}

unsigned char *taxi_location_pack_with_buf(double latitude, double longitude,
                                           unsigned char **r_buf, int *p_len, int offset)
{
    struct taxi taxi = {.id = {0,}, .id_len = 0, .latitude = latitude, .longitude = longitude };
    return __taxis_pack(&taxi, 1, r_buf, p_len, offset);
}

unsigned char *taxi_list_pack_with_buf(struct taxi *taxis, int num_taxis,
                                       unsigned char **r_buf, int *p_len, int offset)
{
#define _CHECK_SPACE(sp) do { len -= (sp); if(len < 0) goto out_free; } while(0)

    int space = 1024, len = space;
    unsigned char *buf; 
    unsigned char *s;

    if(!r_buf || !(buf = *r_buf))
    {
        buf = calloc(1, space);
        assert(buf != NULL);
    }
    if(p_len) len = *p_len;

    s = buf + offset;
    
    _CHECK_SPACE(2 * sizeof(unsigned int));
    *(unsigned int *)s = htonl(_TAXI_LIST_CMD);
    s += sizeof(unsigned int);
    *(unsigned int*)s = htonl(num_taxis);
    int max_taxis =  __MAX_PACKET_LEN/(sizeof(struct taxi)  + _TAXI_OVERHEAD);
    if(num_taxis > max_taxis) 
    {
        num_taxis = max_taxis;
        *(unsigned int*)s = htonl(num_taxis);
    }
    s += sizeof(unsigned int);
    offset += 2 * sizeof(unsigned int);
    buf = taxis_pack_with_buf(taxis, num_taxis, &buf, &len, offset);
    assert(buf);
    len += 2 * sizeof(unsigned int);
    if(r_buf)
        *r_buf = buf;
    if(p_len)
        *p_len = len;
    goto out;

    out_free:
    if(buf && (!r_buf || !*r_buf))
    {
        free(buf);
        buf = NULL;
    }

    out:
    return buf;
#undef _CHECK_SPACE
}

unsigned char *taxi_list_pack(struct taxi *taxis, int num_taxis)
{
    return taxi_list_pack_with_buf(taxis, num_taxis, NULL, NULL, 0);
}

unsigned char *taxis_ping_pack_with_buf(struct taxi *customer, struct taxi *taxis, int num_taxis,
                                        unsigned char **r_buf, int *p_len, int offset)
{
    if(!taxis || !num_taxis || !customer) return NULL;
    unsigned char *buf = NULL;
    int space = 1024;
    if(!r_buf || !(buf = *r_buf))
    {
        buf = calloc(1, space);
        assert(buf);
    }
    if(p_len) space = *p_len;
    int len = space;
    buf = taxi_pack_with_buf(customer, &buf, &len, offset);
    assert(buf != NULL);
    int cur_packed = len;
    len = space;
    buf = taxi_list_pack_with_buf(taxis, num_taxis, &buf, &len, cur_packed + offset);
    assert(buf != NULL);
    cur_packed += len;
    if(r_buf) *r_buf = buf;
    if(p_len) *p_len = cur_packed; /* total packed by this routine */
    return buf;
}

unsigned char *taxis_ping_pack(struct taxi *customer, struct taxi *taxis, int num_taxis)
{
    return taxis_ping_pack_with_buf(customer, taxis, num_taxis, NULL, NULL, 0);
}

static int __taxi_unpack(unsigned char *buf, int len, struct taxi *taxi)
{
#define _CHECK_SPACE(sp) do { len -= (sp); if(len < 0) goto out; } while(0)
    unsigned char *s = buf;
    int err = -1;
    while(len > 0)
    {
        _CHECK_SPACE(sizeof(unsigned int));
        unsigned int id = ntohl(*(unsigned int*)s);
        s += sizeof(unsigned int);
        switch(id)
        {
        case _TAXI_TYPE_ID:
            {
                _CHECK_SPACE(sizeof(unsigned int));
                int id_len = ntohl(*(unsigned int*)s);
                int alen = (id_len + sizeof(unsigned int)-1) & ~(sizeof(unsigned int)-1);
                s += sizeof(unsigned int);
                _CHECK_SPACE(alen);
                memset(taxi->id, 0, sizeof(taxi->id));
                if(id_len > sizeof(taxi->id)) id_len = sizeof(taxi->id);
                memcpy(taxi->id, s, id_len);
                taxi->id_len = id_len;
                s += alen;
            }
            break;
            
        case _TAXI_TYPE_LOCATION:
            {
                _CHECK_SPACE(2*sizeof(taxi->latitude));
                taxi->latitude = *(double*)s;
                s += sizeof(taxi->latitude);
                taxi->longitude = *(double*)s;
                s += sizeof(taxi->longitude);
            }
            break;
            
        case _TAXI_TYPE_STATE:
            {
                _CHECK_SPACE(sizeof(unsigned int));
                taxi->state = ntohl(*(int*)s);
                s += sizeof(int);
            }
            break;

        case _TAXI_TYPE_CUSTOMERS:
            {
                _CHECK_SPACE(sizeof(unsigned int));
                taxi->num_customers = ntohl(*(int*)s);
                s += sizeof(int);
            }
            break;

        case _TAXI_TYPE_ADDR:
            {
                _CHECK_SPACE(2*sizeof(unsigned int));
                taxi->addr.sin_family = PF_INET;
                taxi->addr.sin_addr.s_addr = *(unsigned int*)s;
                s += sizeof(unsigned int);
                unsigned int port = *(unsigned int*)s;
                taxi->addr.sin_port = (unsigned short)port;
                s += sizeof(unsigned int);
            }
            break;

        default:
            goto out;
        }
    }
    assert(len == 0);
    err = 0;

    out:
    return err;

#undef _CHECK_SPACE
}

int taxi_unpack(unsigned char *buf, int *p_len, struct taxi *taxi)
{
#define _CHECK_SPACE(sp) do { len -= sp; if(len < 0) goto out; } while(0)
    int err = -1;
    unsigned char *s = buf;
    int len ;
    if(!p_len) goto out;
    len = *p_len;
    _CHECK_SPACE(sizeof(unsigned int)*2);
    unsigned int start_marker = ntohl(*(unsigned int *)s);
    s += sizeof(unsigned int);
    if(start_marker != _TAXI_TYPE_START) goto out;

    unsigned int entry_len = ntohl(*(unsigned int*)s);
    s += sizeof(unsigned int);
    if(entry_len > len) goto out;

    err = __taxi_unpack(s, entry_len, taxi);
    if(err == 0)
    {
        len -= entry_len;
        *p_len = len;
    }
    out:
    return err;
#undef _CHECK_SPACE
}

int taxis_unpack(unsigned char *buf, int *p_len, struct taxi **p_taxis, int *p_num_taxis)
{
#define _CHECK_SPACE(sp) do { len -= (sp); if(len < 0) goto out; }while(0)
    struct taxi *taxis = NULL;
    int num_taxis = 0;
    unsigned char *s = buf;
    int err = -1;

    if(!buf || !p_len || !p_taxis || !p_num_taxis)
        goto out;

    int len = *p_len;
    *p_taxis = NULL;
    *p_num_taxis = 0;
    
    _CHECK_SPACE(sizeof(unsigned int));
    num_taxis = ntohl(*(unsigned int*)s);
    s += sizeof(unsigned int);
    if(!num_taxis) 
    {
        *p_len = len;
        err = 0;
        goto out;
    }
    taxis = calloc(num_taxis, sizeof(*taxis));
    assert(taxis);
    for(int i = 0; i < num_taxis; ++i)
    {
        int save_len = len;
        err = taxi_unpack(s, &len, &taxis[i]);
        if(err < 0)
            goto out_free;
        s += save_len - len;
    }
    *p_len = len;
    *p_taxis = taxis;
    *p_num_taxis = num_taxis;
    err = 0;
    goto out;

    out_free:
    if(taxis) free(taxis);
    out:
    return err;

#undef _CHECK_SPACE
}

int taxi_list_unpack(unsigned char *buf, int *p_len, 
                     struct taxi **taxis, int *num_taxis)
{
#define _CHECK_SPACE(sp) do { len -= (sp); if(len < 0) goto out; } while(0)
    int err = -1;
    unsigned char *s = buf;
    int len;
    if(!buf || !p_len || !taxis || !num_taxis)
        goto out;
    len = *p_len;
    _CHECK_SPACE(sizeof(unsigned int));
    unsigned int cmd = ntohl(*(unsigned int*)s);
    if(cmd != _TAXI_LIST_CMD)
    {
        goto out;
    }
    s += sizeof(unsigned int);
    err = taxis_unpack(s, &len, taxis, num_taxis);
    if(err < 0)
        goto out;
    *p_len = len;
    out:
    return err;

#undef _CHECK_SPACE
}

int taxis_ping_unpack(unsigned char *buf, int *p_len, 
                      struct taxi *customer, struct taxi **p_taxis, int *p_num_taxis)
{
#define _CHECK_SPACE(sp) do { len -= sp; if(len < 0) goto out; } while(0)
    int err = -1;
    if(!buf || !p_len || !customer || !p_taxis || !p_num_taxis)
        goto out;
    unsigned char *s = buf;
    int len = *p_len;
    err = taxi_unpack(buf, &len, customer);
    if(err < 0)
        goto out;
    s += *p_len - len; /* skip bytes unpacked*/
    err = taxi_list_unpack(s, &len, p_taxis, p_num_taxis);
    if(err < 0)
        goto out;
    *p_len = len;

    out:
    return err;
}

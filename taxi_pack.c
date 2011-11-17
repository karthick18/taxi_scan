#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <assert.h>
#include "taxi_pack.h"

#define _TAXI_TYPE_ID (0x1)
#define _TAXI_TYPE_LOCATION (0x2)


/*
 * Pack the result into a buffer for sending over the wire.
 */
static unsigned char *__taxis_pack(struct taxi *taxis, int num_taxis,
                                   unsigned char **r_buf,
                                   int len, int offset)
{
#define _BUF_SPACE (1024)
#define _CHECK_SPACE(sp) do {                   \
        if( (space - (sp) ) < 0 )               \
        {                                       \
            ++extents;                          \
            int _cur_span = s - buf;            \
            buf = realloc(buf, extents * len);  \
            assert(buf);                        \
            space = len;                        \
            s = buf + _cur_span;                \
        }                                       \
        space -= (sp);                          \
    }while(0)

    if(!len) len = _BUF_SPACE;
    unsigned char *buf = r_buf ? *r_buf : NULL;
    if(!buf) buf = calloc(1, len);
    unsigned char *s = buf + offset;
    int space = len - offset, extents = 1;
    assert(buf);

    for(int i = 0; i < num_taxis; ++i)
    {
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
    }

    if(r_buf)
        *r_buf = buf;

    return buf;

#undef _CHECK_SPACE
#undef _BUF_SPACE
}

unsigned char *taxis_pack(struct taxi *taxis, int num_taxis)
{
    return __taxis_pack(taxis, num_taxis, NULL, 0, 0);
}

unsigned char *taxi_pack(struct taxi *taxi)
{
    return __taxis_pack(taxi, 1, NULL, 0, 0);
}

unsigned char *taxi_location_pack(double latitude, double longitude)
{
    struct taxi taxi = {.id = {0,}, .id_len = 0, .latitude = latitude, .longitude = longitude };
    return __taxis_pack(&taxi, 1, NULL, 0, 0);
}

unsigned char *taxis_pack_with_buf(struct taxi *taxis, int num_taxis,
                                   unsigned char **r_buf, int len, int offset)
{
    return __taxis_pack(taxis, num_taxis, r_buf, len, offset);
}

unsigned char *taxi_pack_with_buf(struct taxi *taxi, unsigned char **r_buf, 
                                  int len, int offset)
{
    return __taxis_pack(taxi, 1, r_buf, len, offset);
}

unsigned char *taxi_location_pack_with_buf(double latitude, double longitude,
                                           unsigned char **r_buf, int len, int offset)
{
    struct taxi taxi = {.id = {0,}, .id_len = 0, .latitude = latitude, .longitude = longitude };
    return __taxis_pack(&taxi, 1, r_buf, len, offset);
}

int taxi_unpack(unsigned char *buf, int len, struct taxi *taxi)
{
#define _CHECK_SPACE(sp) do { (len) -= sp; if((len) < 0) goto out; } while(0)
    int err = -1;
    unsigned char *s = buf;
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

        default:
            goto out;
        }
    }

    err = 0;

    out:
    return err;
}

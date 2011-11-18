#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <assert.h>
#include "taxi_pack.h"

#define _TAXI_TYPE_START (0x1)
#define _TAXI_TYPE_ID (0x2)
#define _TAXI_TYPE_LOCATION (0x3)

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
        /*
         * Update entry len
         */
        *(unsigned int*)taxi_len_marker = 
            htonl((unsigned int)(s - (unsigned char*)taxi_len_marker - sizeof(unsigned int)));
    }

    if(r_buf)
        *r_buf = buf;
    if(p_len) *p_len = s - buf; /* bytes packed */
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

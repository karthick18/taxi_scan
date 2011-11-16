#ifndef _HASH_H_
#define _HASH_H_

#ifdef __cplusplus
extern "C" {
#endif

struct hash_struct
{
    struct hash_struct *next;
    struct hash_struct **pprev;
};

static __inline__ void hash_add(struct hash_struct **table, unsigned int key, struct hash_struct *h)
{
    struct hash_struct **t = &table[key];
    if((h->next = *t))
    {
        h->next->pprev = &h->next;
    }
    *(h->pprev = t) = h;
}

static __inline__ void hash_del(struct hash_struct *h)
{
    if(!h->pprev) return ;
    if(h->next)
        h->next->pprev = h->pprev;
    *h->pprev = h->next;
    h->next = NULL;
    h->pprev = NULL;
}

#define hash_entry(ele, cast, f) \
    ( (cast*)( (unsigned char *)(ele) - (unsigned long) ( &((cast*)0)->f) ) )

#define hash_for_each(iter, h) \
    for( iter = (h); iter; iter = (iter)->next)

#define hash_for_each_safe(iter, h, n) \
    for( iter = h; iter && ( (n = (iter)->next) || 1 ); iter = n)

#ifdef __cplusplus
}
#endif

#endif

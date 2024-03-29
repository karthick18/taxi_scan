#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "taxi_customer.h"
#include "taxi_pack.h"
#include "taxi_utils.h"

#define __FIND_TAXI(id, len, list, field) do {                      \
    struct list_head *__iter ;                                      \
    list_for_each(__iter, list)                                     \
    {                                                               \
        struct taxi *taxi = list_entry(__iter, struct taxi, field); \
        int cmp = taxi->id_len - (len);                             \
        if(!cmp)                                                    \
            cmp = memcmp(taxi->id, (id), len);                      \
        if(!cmp) return taxi;                                       \
    }                                                               \
    return NULL;                                                    \
}while(0)

#define __FIND_CUSTOMER(ID, LEN, LIST, FIELD) do {                      \
    if(!g_num_customers) return NULL;                                   \
    if(!(ID) || !(LEN))                                                 \
        return g_customer;                                              \
    int cmp = g_customer->taxi.id_len - (LEN);                          \
    if(!cmp)                                                            \
        cmp = memcmp(g_customer->taxi.id, (ID), (LEN));                 \
    if(!cmp) return g_customer;                                         \
    struct list_head *__iter ;                                          \
    list_for_each(__iter, list)                                         \
    {                                                                   \
        struct taxi_customer *customer = list_entry(__iter, struct taxi_customer, FIELD); \
        cmp = customer->taxi.id_len - (LEN);                            \
        if(!cmp)                                                        \
            cmp = memcmp(customer->taxi.id, (ID), (LEN));               \
        if(!cmp) return customer;                                       \
    }                                                                   \
    return NULL;                                                        \
}while(0)

static DECLARE_LIST_HEAD(g_customer_list);
static DECLARE_LIST_HEAD(g_taxi_list);
static int g_num_customers;
static int g_num_taxis;
static struct taxi_customer *g_customer;
static unsigned char *g_customer_id;
static unsigned char *g_taxi_id;
static int g_customer_id_len;
static int g_taxi_id_len;

struct customer_handle
{
    struct taxi_customer *customer;
    struct list_head customer_list; /* taxi customer list */
};

struct taxi_handle
{
    struct taxi *taxi;
    struct list_head taxi_list; /* customers taxi list */
};

struct taxi *__find_taxi_customer(struct taxi_customer *customer, 
                                  unsigned char *id, int id_len,
                                  struct taxi_handle **r_taxi_handle)
{
    struct list_head *list = customer ? &customer->taxi_list : &g_taxi_list;
    if(!customer)
    {
        __FIND_TAXI(id, id_len, list, list);
    }
    struct list_head *iter;
    list_for_each(iter, list)
    {
        struct taxi_handle *taxi_handle = list_entry(iter, struct taxi_handle, taxi_list);
        int cmp = taxi_handle->taxi->id_len - id_len;
        if(!cmp)
            cmp = memcmp(taxi_handle->taxi->id, id, id_len);
        if(!cmp) 
        {
            if(r_taxi_handle) *r_taxi_handle = taxi_handle;
            return taxi_handle->taxi;
        }
    }
    return NULL;
}

static struct taxi_customer *__find_customer_taxi(struct taxi *taxi, unsigned char *id, int id_len,
                                                  struct customer_handle **r_customer_handle)
{
    struct list_head *list = taxi ? &taxi->customer_list : &g_customer_list;
    if(!taxi)
    {
        __FIND_CUSTOMER(id, id_len, list, list);
    }
    struct list_head *iter;
    list_for_each(iter, list)
    {
        struct customer_handle *customer_handle = list_entry(iter, struct customer_handle, customer_list);
        int cmp = customer_handle->customer->taxi.id_len - id_len;
        if(!cmp) 
            cmp = memcmp(customer_handle->customer->taxi.id, id, id_len);
        if(!cmp)
        {
            if(r_customer_handle) *r_customer_handle = customer_handle;
            return customer_handle->customer;
        }
    }
    return NULL;
}

static struct taxi_customer *__find_customer(unsigned char *id, int id_len)
{
    return __find_customer_taxi(NULL, id, id_len, NULL);
}

static struct taxi *__update_taxi(struct taxi *taxi)
{
    struct taxi *loc = __find_taxi_customer(NULL, taxi->id, taxi->id_len, NULL);
    if(!loc)
    {
        loc = calloc(1, sizeof(*loc));
        assert(loc != NULL);
        memcpy(loc, taxi, sizeof(*loc));
        LIST_HEAD_INIT(&loc->customer_list);
        loc->state = _TAXI_STATE_IDLE;
        list_add_tail(&loc->list, &g_taxi_list);
        ++g_num_taxis;
    }
    else
    {
        if(!taxi->state) 
        {
            taxi->state = _TAXI_STATE_IDLE;
        }
        loc->state = taxi->state;
        memcpy(&loc->addr, &taxi->addr, sizeof(loc->addr));
        loc->latitude = taxi->latitude;
        loc->longitude = taxi->longitude;
    }
    return loc;
}

static int __add_taxi(struct taxi *taxi, struct taxi *customer_taxi)
{
    struct taxi *loc = __update_taxi(taxi);
    struct taxi_customer *customer = __find_customer(customer_taxi->id, customer_taxi->id_len);
    assert(loc != NULL);
    if(!customer)
    {
        customer = calloc(1, sizeof(*customer));
        assert(customer != NULL);
        memcpy(&customer->taxi, customer_taxi, sizeof(customer->taxi));
        LIST_HEAD_INIT(&customer->taxi_list);
        list_add_tail(&customer->list, &g_customer_list);
        ++g_num_customers;
        struct taxi_handle *taxi_handle = calloc(1, sizeof(*taxi_handle));
        assert(taxi_handle);
        taxi_handle->taxi = loc;
        list_add_tail(&taxi_handle->taxi_list, &customer->taxi_list);
        ++customer->num_taxis;
        struct customer_handle *customer_handle = calloc(1, sizeof(*customer_handle));
        assert(customer_handle);
        customer_handle->customer = customer;
        list_add_tail(&customer_handle->customer_list, &loc->customer_list);
        ++loc->num_customers;
        if(!g_customer) g_customer = customer;
    }
    else
    {
        memcpy(&customer->taxi, customer_taxi, sizeof(customer->taxi));
        struct taxi_customer *customer_loc = 
            __find_customer_taxi(loc, customer_taxi->id, customer_taxi->id_len, NULL);
        if(!customer_loc)
        {
            struct taxi_handle *taxi_handle = calloc(1, sizeof(*taxi_handle));
            struct customer_handle *customer_handle = calloc(1, sizeof(*customer_handle));
            assert(taxi_handle && customer_handle);
            taxi_handle->taxi = loc;
            customer_handle->customer = customer;
            list_add_tail(&customer_handle->customer_list, &loc->customer_list);
            list_add_tail(&taxi_handle->taxi_list, &customer->taxi_list);
            ++customer->num_taxis;
            ++loc->num_customers;
        }
        
    }
    return 0;
}

int add_taxis_customer(struct taxi *taxi, struct taxi *taxis, int num_taxis)
{
    for(int i = 0; i < num_taxis; ++i)
    {
        __add_taxi(&taxis[i], taxi);
    }
    return 0;
}

static int __unlink_customer_taxi(struct taxi *taxi,
                                  struct taxi_customer *customer)
{
    struct customer_handle *customer_handle = NULL;
    struct taxi_customer *customer_loc = NULL;
    customer_loc = __find_customer_taxi(taxi, customer->taxi.id, customer->taxi.id_len, &customer_handle);
    if(!customer_loc) return -1;
    list_del(&customer_handle->customer_list);
    --taxi->num_customers;
    free(customer_handle);
    return 0;
}

static int __unlink_taxi_customer(struct taxi_customer *customer,
                                  struct taxi *taxi)
{
    struct taxi_handle *taxi_handle = NULL;
    struct taxi *taxi_loc;
    taxi_loc = __find_taxi_customer(customer, taxi->id, taxi->id_len, &taxi_handle);
    if(!taxi_loc) return -1;
    list_del(&taxi_handle->taxi_list);
    --customer->num_taxis;
    free(taxi_handle);
    return 0;
}

static int __unlink_taxi(struct taxi *taxi)
{
    struct list_head *iter;
    struct customer_handle *customer_handle = NULL;
    while(!LIST_EMPTY(&taxi->customer_list))
    {
        iter = taxi->customer_list.next;
        customer_handle = list_entry(iter, struct customer_handle, customer_list);
        __unlink_taxi_customer(customer_handle->customer, taxi);
        list_del(iter);
        free(customer_handle);
        --taxi->num_customers;
    }
    assert(taxi->num_customers == 0);
    return 0;
}

static int __unlink_customer(struct taxi_customer *customer)
{
    struct list_head *iter;
    struct taxi_handle *taxi_handle = NULL;
    while(!LIST_EMPTY(&customer->taxi_list))
    {
        iter = customer->taxi_list.next;
        taxi_handle = list_entry(iter, struct taxi_handle, taxi_list);
        __unlink_customer_taxi(taxi_handle->taxi, customer);
        list_del(iter);
        free(taxi_handle);
        --customer->num_taxis;
    }
    assert(customer->num_taxis == 0);
    return 0;
}

int del_taxi_customer(unsigned char *taxi_id, int taxi_id_len,
                      unsigned char *customer_id, int customer_id_len,
                      struct taxi *result)
{
    struct taxi_customer *customer = NULL;
    struct taxi_handle *taxi_handle = NULL;
    if(customer_id && customer_id_len)
    {
        customer = __find_customer(customer_id, customer_id_len);
        if(!customer) return -1;
    }
    struct taxi *taxi = __find_taxi_customer(customer, taxi_id, taxi_id_len, &taxi_handle);
    if(!taxi) return -1;

    if(result)
    {
        memcpy(result, taxi, sizeof(*result));
    }

    if(customer)
    {
        --customer->num_taxis;
        assert(taxi_handle != NULL && taxi_handle->taxi == taxi);
        list_del(&taxi_handle->taxi_list);
        free(taxi_handle);
        __unlink_customer_taxi(taxi, customer);
    }
    else
    {
        __unlink_taxi(taxi);
        list_del(&taxi->list);
        --g_num_taxis;
        free(taxi);
    }
    return 0;
}

int find_taxi_customer(unsigned char *taxi_id, int id_len,
                       unsigned char *customer_id, int customer_id_len, struct taxi *result)
{
    int err = -1;
    struct taxi *taxi = NULL;
    struct taxi_customer *customer = NULL;
    if(customer_id && customer_id_len > 0)
    {
        customer = __find_customer(customer_id, customer_id_len);
        if(!customer) goto out;
    }
    taxi = __find_taxi_customer(customer, taxi_id, id_len, NULL);
    if(!taxi) goto out;
    if(result)
        memcpy(result, taxi, sizeof(*result));
    err = 0;

    out:
    return err;
}

int find_taxi_by_hint(short port, struct taxi *res)
{
    int err = -1;
    if(!port || !res) goto out;
    struct sockaddr *addrs = NULL;
    int num_addrs = 0;
    get_if_addrs(&addrs, &num_addrs);
    struct list_head *iter;
    list_for_each(iter, &g_taxi_list)
    {
        struct taxi *taxi = list_entry(iter, struct taxi, list);
        if(taxi->addr.sin_port != port) continue;
        for(int i = 0; i < num_addrs; ++i)
        {
            struct sockaddr_in *ref_addr = (struct sockaddr_in*)&addrs[i];
            if(taxi->addr.sin_addr.s_addr == ref_addr->sin_addr.s_addr)
            {
                err = 0;
                memcpy(res, taxi, sizeof(*res));
                goto out_free;
            }
        }
    }

    out_free:
    if(addrs) free(addrs);
    out:
    return err;
}

int find_customer_taxi(unsigned char *taxi_id, int taxi_id_len,
                       unsigned char *customer_id, int customer_id_len,
                       struct taxi_customer *r_customer)
{
    int err = -1;
    if(!taxi_id || !taxi_id_len) goto out;
    if(!customer_id || !customer_id_len) goto out;
    struct taxi_customer *customer = __find_customer(customer_id, customer_id_len);
    if(!customer) goto out;
    struct taxi *taxi = __find_taxi_customer(customer, taxi_id, taxi_id_len, NULL);
    if(!taxi) goto out;
    if(r_customer)
        memcpy(r_customer, customer, sizeof(*r_customer));

    err = 0;
    out:
    return err;
}

int update_taxi_state_customer(unsigned char *taxi_id, int taxi_id_len, 
                               unsigned char *customer_id, int customer_id_len, int new_state)
{
    int err = -1;
    if(new_state & ~(_TAXI_STATE_IDLE | _TAXI_STATE_ACTIVE | _TAXI_STATE_PICKUP))
        goto out;
    struct taxi *taxi = NULL;
    struct taxi_customer *customer = NULL;
    if(customer_id && customer_id_len > 0)
    {
        customer = __find_customer(customer_id, customer_id_len);
        if(!customer) goto out;
    }
    taxi = __find_taxi_customer(customer, taxi_id, taxi_id_len, NULL);
    if(!taxi) goto out;
    int old_state = taxi->state;
    if(old_state == _TAXI_STATE_IDLE
       &&
       (new_state & (_TAXI_STATE_PICKUP | _TAXI_STATE_ACTIVE)))
    {
        if(customer) ++customer->num_approaching;
    }
    else if( (old_state & (_TAXI_STATE_PICKUP | _TAXI_STATE_ACTIVE) )
             &&
             (new_state & _TAXI_STATE_IDLE) )
    {
        if(customer && customer->num_approaching > 0)
            --customer->num_approaching;
    }
    taxi->state = new_state;
    err = 0;

    out:
    return err;
}

int get_taxi_state(unsigned char *id, int id_len, int *r_state)
{
    struct taxi *taxi = NULL;
    int err = -1;
    if(!r_state) goto out;
    taxi = __find_taxi_customer(NULL, id, id_len, NULL);
    if(!taxi) goto out;

    *r_state = taxi->state;
    err = 0;
    out:
    return err;
}

int del_customer(unsigned char *id, int id_len)
{
    int err = -1;
    struct taxi_customer *customer = __find_customer(id, id_len);
    if(!customer) goto out;
    __unlink_customer(customer);
    list_del(&customer->list);
    --g_num_customers;
    if(g_num_customers > 0)
    {
        if(customer == g_customer)
        {
            g_customer = list_entry(g_customer_list.next, struct taxi_customer, list);
        }
    }
    else g_customer = NULL;
    free(customer);
    err = 0;

    out:
    return err;
}

static int __set_id(unsigned char *id, int id_len,
                    unsigned char **r_id, int *r_id_len)
{
    unsigned char *cur_id = *r_id;
    int cur_id_len = *r_id_len;
    if(cur_id && cur_id_len > 0)
    {
        free(cur_id);
        *r_id = NULL;
        *r_id_len = 0;
    }
    cur_id = calloc(id_len, sizeof(*id));
    assert(cur_id);
    cur_id_len = id_len;
    memcpy(cur_id, id, id_len);
    *r_id = cur_id;
    *r_id_len = cur_id_len;
    return 0;
}

/*
 * Not expected to change 
 */
int set_customer_id(unsigned char *customer_id, int customer_id_len)
{
    if(customer_id && customer_id_len > 0)
        return __set_id(customer_id, customer_id_len, &g_customer_id, &g_customer_id_len);
    return -1;
}

/*
 * Not expected to change. 
 */
int set_taxi_id(unsigned char *taxi_id, int taxi_id_len)
{
    if(taxi_id && taxi_id_len > 0)
        return __set_id(taxi_id, taxi_id_len, &g_taxi_id, &g_taxi_id_len);
    return -1;
}

/*
 * Filter is 0 for match and -1 for no match based on the hint.
 */
static int get_taxis_customer_filter(unsigned char *id, int id_len,
                                     short port,
                                     int filter,
                                     struct taxi **p_taxis, int *p_num_taxis)
{
    int err = -1;
    if(!id || !id_len || !p_taxis || !p_num_taxis) goto out;
    struct taxi_customer *customer = NULL;
    struct taxi *taxis = NULL;
    int num_taxis = 0;
    customer = __find_customer(id, id_len);
    if(!customer) goto out;
    struct list_head *iter;
    struct sockaddr *addrs = NULL;
    int num_addrs = 0;

    list_for_each(iter, &customer->taxi_list)
    {
        struct taxi_handle *handle = list_entry(iter, struct taxi_handle, taxi_list);
        struct taxi *target = handle->taxi;
        int cmp = -1;

        if(!port)
            goto do_copy;

        if(!g_taxi_id  || !g_taxi_id_len)
        {
            if(!addrs)
            {
                get_if_addrs(&addrs, &num_addrs);
            }
            cmp = (int) (target->addr.sin_port - port);
            if(cmp == 0)
            {
                /*
                 * Slow path, compare addresses.
                 */
                cmp = -1;
                for(int j = 0; j < num_addrs; ++j)
                {
                    struct sockaddr_in *my_addr = (struct sockaddr_in*)(addrs+j);
                    if(my_addr->sin_addr.s_addr == target->addr.sin_addr.s_addr);
                    {
                        cmp = 0; /* matched self */
                        break;
                    }
                }
            }
        }
        else
        {
            cmp = g_taxi_id_len - target->id_len;
            if(!cmp)
                cmp = memcmp(g_taxi_id, target->id, g_taxi_id_len);
            if(cmp) cmp = -1;
        }

        if(cmp == filter)
        {
            do_copy:
            taxis = realloc(taxis, sizeof(*taxis) * (num_taxis+1));
            assert(taxis);
            memcpy(taxis+num_taxis, target, sizeof(*target));
            ++num_taxis;
            if(!filter) break;
        }
    }
    err = 0;
    *p_taxis = taxis;
    *p_num_taxis = num_taxis;
    if(addrs) free(addrs);
    
    out:
    return err;
}

int get_taxis_excluding_self_customer(unsigned char *id, int id_len, short hint,
                                      struct taxi **p_taxis, int *p_num_taxis)
{
    return get_taxis_customer_filter(id, id_len, hint, -1, p_taxis, p_num_taxis);
}

int get_taxis_customer(unsigned char *id, int id_len, 
                       struct taxi **p_taxis, int *p_num_taxis)
{
    return get_taxis_customer_filter(id, id_len, 0, -1, p_taxis, p_num_taxis);
}

int get_taxi_matching_self_customer(unsigned char *id, int id_len, 
                                    short hint, struct taxi *self)
{
    int err = -1;
    if(!self) goto out;
    struct taxi *taxis = NULL;
    int num_taxis = 0;
    err = get_taxis_customer_filter(id, id_len, hint, 0, &taxis, &num_taxis);
    if(err < 0) goto out;
    if(!num_taxis) 
    {
        err = -1;
        goto out;
    }
    memcpy(self, taxis, sizeof(*self)); /* copy 1 taxi */
    free(taxis);

    out:
    return err;
}

int get_taxis_approaching_customer(unsigned char *id, int id_len)
{
    int num_approaching = 0;
    struct taxi_customer *customer;
    if(!id || !id_len) goto out;
    customer = __find_customer(id, id_len);
    if(!customer) goto out;
    num_approaching = customer->num_approaching;
    out:
    return num_approaching;
}

/*
 * We store taxis inside a interval tree node within (+-) distance or location.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "rbtree.h"
#include "taxi.h"

struct taxi_location
{
    double latitude;
    double longitude;
    unsigned char *id;
    int id_len;
    struct taxi_location **taxi_near_locations;
    int num_taxis;
    int location_index;
    struct taxi_location *taxi_parent;
    struct rbtree map;
    struct rbtree id_map; 
};

struct taxi_db
{
    struct rbtree_root taxi_map;
    struct rbtree_root taxi_id_map;
    int num_taxis;
};

static struct taxi_db taxi_db;

static int taxi_near_locations_cmp(const void *a, const void *b)
{
    struct taxi_location *l1 = *(struct taxi_location**)a;
    struct taxi_location *l2 = *(struct taxi_location**)b;
    if(l1->latitude < l2->latitude) return -1;
    if(l1->latitude > l2->latitude) return 1;
    if(l1->longitude < l2->longitude) return -1;
    if(l1->longitude > l2->longitude) return 1;
    return 0;
}

static int __add_taxi_by_location(struct taxi_location *taxi)
{
    struct rbtree **link = &taxi_db.taxi_map.root;
    struct taxi_location *taxi_location = NULL;
    struct rbtree *parent = NULL;
    struct taxi_location *taxi_parent = taxi;
    while(*link)
    {
        parent = *link;
        taxi_location = rbtree_entry(parent, struct taxi_location, map);
        /*
         * If it falls within the distance, add within the parent.
         */
        if(fabs(taxi->latitude - taxi_location->latitude) <= TAXI_DISTANCE_BIAS
           &&
           fabs(taxi->longitude - taxi_location->longitude) <= TAXI_DISTANCE_BIAS)
        {
            taxi_parent = taxi_location;
            goto add_near_location;
        }
        if(taxi->latitude <= taxi_location->latitude)
            link = &parent->left;
        else link = &parent->right;
    }

    output("New taxi interval added for taxi [%.*s] [%lg:%lg]\n",
           taxi->id_len, taxi->id, taxi->latitude, taxi->longitude);
    /*
     * Add a taxi interval
     */
    __rbtree_insert(&taxi->map, parent, link);
    rbtree_insert_colour(&taxi_db.taxi_map, &taxi->map);
    taxi->num_taxis = 0;

    add_near_location:
    taxi->taxi_parent = taxi_parent;
    taxi->location_index = taxi_parent->num_taxis;
    taxi_parent->taxi_near_locations = realloc(taxi_parent->taxi_near_locations,
                                                 sizeof(*taxi_parent->taxi_near_locations)
                                                 * (taxi_parent->num_taxis+1));
    assert(taxi_parent->taxi_near_locations != NULL);
    taxi_parent->taxi_near_locations[taxi_parent->num_taxis++] = taxi;
    qsort(taxi_parent->taxi_near_locations, taxi_parent->num_taxis,
          sizeof(*taxi_parent->taxi_near_locations), taxi_near_locations_cmp);
    return 0;
}

/*
 * Each taxi has a unique id to locate
 */
static struct taxi_location *find_taxi_by_id(struct taxi_location *taxi, int add)
{
    struct rbtree **link = &taxi_db.taxi_id_map.root;
    struct rbtree *parent = NULL;
    struct taxi_location *entry = NULL;
    while(*link)
    {
        parent = *link;
        entry = rbtree_entry(parent, struct taxi_location, id_map);
        int len = taxi->id_len > entry->id_len ? entry->id_len : taxi->id_len;
        int cmp = memcmp(taxi->id, entry->id, len);
        if(cmp < 0)
            link = &parent->left;
        else if(cmp > 0)
            link = &parent->right;
        else 
            return entry;
    }

    if(add)
    {
        __rbtree_insert(&taxi->id_map, parent, link);
        rbtree_insert_colour(&taxi_db.taxi_id_map, &taxi->id_map);
        taxi_db.num_taxis++;
        return taxi;
    }
    return NULL;
}

static int reparent_taxi(struct taxi_location *taxi, int add_back_parent)
{
    struct taxi_location **children = taxi->taxi_near_locations;
    int num_childs = taxi->num_taxis;

    if(taxi->taxi_parent != taxi) return -1; /*not a parent entity*/

    taxi->taxi_parent = NULL;
    taxi->taxi_near_locations = NULL;
    taxi->num_taxis = 0;
    taxi->location_index = 0;
    /* 
     * first unlink from the location map and add back
     */
    rbtree_erase(&taxi_db.taxi_map, &taxi->map);
    if(add_back_parent)
        __add_taxi_by_location(taxi);
    /*
     * Next unlink children. Child 0 is reserved for itself
     */
    for(int i = 0; i < num_childs; ++i)
    {
        if(children[i] == taxi) continue;
        assert(children[i]->taxi_parent == taxi);
        children[i]->taxi_parent = NULL;
        children[i]->location_index = 0;
        children[i]->num_taxis = 0;
        __add_taxi_by_location(children[i]);
    }
    free(children);
    return 0;
}

/*
 * Check if the taxi location is being updated and if yes, reparent or readjust the
 * taxi location.
 */

static int __add_taxi(struct taxi_location *taxi)
{
    struct taxi_location *entry = find_taxi_by_id(taxi, 1);
    /*
     * Check if its a new taxi or an update to an existing one.
     */
    if(entry != taxi)
    {
        struct taxi_location *parent  = entry->taxi_parent;
        if(taxi->latitude == entry->latitude
           &&
           taxi->longitude == entry->longitude)
        {
            return -1; /*match*/
        }
        /*
         * IF this is a contained taxi within a parent taxi, then 
         * move or update its current location based on the distance relocated.
         */
        if(parent != entry)
        {
            assert(parent->num_taxis > 1);
            if(fabs(taxi->latitude - parent->latitude) <= TAXI_DISTANCE_BIAS
               &&
               fabs(taxi->longitude - parent->longitude) <= TAXI_DISTANCE_BIAS)
            {
                entry->latitude = taxi->latitude;
                entry->longitude = taxi->longitude;
                return -1; /*distance match and updated*/
            }
            /*
             * If the taxi has moved outside the radius of the parent,
             * then unlink and re-add
             */
            parent->num_taxis--;
            memmove(parent->taxi_near_locations + entry->location_index,
                    parent->taxi_near_locations + entry->location_index+1,
                    sizeof(*parent->taxi_near_locations) * (parent->num_taxis - entry->location_index));
            entry->taxi_parent = NULL;
            /*
             * New latitude and longitude
             */
            entry->latitude = taxi->latitude;
            entry->longitude = taxi->longitude;
            /*
             * Now add into the location map
             */
            entry->location_index = 0;
            entry->num_taxis = 0;
            __add_taxi_by_location(entry);
            return -1;  /*match*/
        }
        /*
         * This is a parent entry that is relocating. Unlink from the location map
         * and re-add the children back as well.
         */
        entry->latitude = taxi->latitude;
        entry->longitude = taxi->longitude;
        reparent_taxi(entry, 1);
        return -1;
    }

    /*
     * A new entry was added into the id map. Add this guy to the location map as well.
     */
    return __add_taxi_by_location(taxi);
}

static int __del_taxi(struct taxi_location *taxi)
{
    int err = -1;
    struct taxi_location *entry = find_taxi_by_id(taxi, 0);
    if(!entry)
        goto out;
    /*
     * Found the taxi. Remove and reparent children if its a parent entry,
     * else unlink from parent.
     */
    struct taxi_location *parent = entry->taxi_parent;
    if(entry == parent)
    {
        reparent_taxi(parent, 0);
    }
    else
    {
        assert(parent->num_taxis > 1);
        parent->num_taxis--;
        memmove(parent->taxi_near_locations + entry->location_index,
                parent->taxi_near_locations + entry->location_index + 1,
                sizeof(*parent->taxi_near_locations) * (parent->num_taxis - entry->location_index));
        entry->taxi_parent = NULL;
        entry->location_index = 0;
    }
    /*
     * Delete entry from the id map
     */
    rbtree_erase(&taxi_db.taxi_id_map, &entry->id_map);
    --taxi_db.num_taxis;
    free(entry->id);
    free(entry);
    err = 0;

    out:
    return err;
}

int add_taxi(double latitude, double longitude, unsigned char *id, int id_len)
{
    struct taxi_location *taxi = calloc(1, sizeof(*taxi));
    int err;
    assert(taxi);
    taxi->latitude = latitude;
    taxi->longitude = longitude;
    taxi->id = calloc(1, id_len);
    assert(taxi->id);
    taxi->id_len = id_len;
    memcpy(taxi->id, id, id_len);
    if((err = __add_taxi(taxi))< 0)
    {
        free(taxi->id);
        free(taxi); /* existing entry was updated*/
    }
    return err;
}

int del_taxi(unsigned char *id, int id_len)
{
    int err = 0;
    struct taxi_location taxi = {.latitude = 0, .longitude = 0, .id = id, .id_len = id_len };
    err = __del_taxi(&taxi);
    if(err < 0)
    {
        output("Unable to delete taxi with id [%.*s]\n", id_len, id);
    }
    return err;
}

static int __find_taxis_by_location(struct taxi_location *taxi_location, 
                                    struct taxi_location **taxi_parent,
                                    struct taxi_location ***taxis,
                                    int *num_taxis)
{
    struct taxi_location **results = NULL;
    int num_results = 0, err = -1;
    *taxis = NULL;
    *num_taxis = 0;
    struct taxi_location *entry = NULL;
    struct rbtree *iter = taxi_db.taxi_map.root;
    while(iter)
    {
        entry = rbtree_entry(iter, struct taxi_location, map);
        if(fabs(taxi_location->latitude - entry->latitude) <= TAXI_DISTANCE_BIAS
           &&
           fabs(taxi_location->longitude - entry->longitude) <= TAXI_DISTANCE_BIAS)
        {
            /*
             * match
             */
            results = realloc(results, sizeof(*results) * (num_results + entry->num_taxis));
            assert(results);
            memcpy(results + num_results, entry->taxi_near_locations,
                   sizeof(*entry->taxi_near_locations) * entry->num_taxis);
            num_results += entry->num_taxis;
            if(taxi_parent) *taxi_parent = entry;
            break;
        }
        /*
         * Now check if its within twice the distance of this entry as we could
         * be inside a intersecting within a child
         */
        else if(fabs(taxi_location->latitude - entry->latitude) <= TAXI_DISTANCE_BIAS*2
                &&
                fabs(taxi_location->longitude - entry->longitude) <= TAXI_DISTANCE_BIAS*2)
        {
            /*
             * Now check if its at an acceptable distance from the nearest taxi for this parent/group
             */
            if(fabs(taxi_location->latitude - entry->taxi_near_locations[0]->latitude) <= TAXI_DISTANCE_BIAS
               &&
               fabs(taxi_location->longitude - entry->taxi_near_locations[0]->longitude) <= TAXI_DISTANCE_BIAS)
            {
                /*
                 * If the highest is also within the range, grab all.
                 */
                int num_taxis = entry->num_taxis >> 1; /* take 50% to start with*/
                if(fabs(taxi_location->latitude - entry->taxi_near_locations[entry->num_taxis-1]->latitude) <=
                   TAXI_DISTANCE_BIAS
                   &&
                   fabs(taxi_location->longitude - entry->taxi_near_locations[entry->num_taxis-1]->longitude) <=
                   TAXI_DISTANCE_BIAS)
                {
                    num_taxis = entry->num_taxis;
                }
                results = realloc(results, sizeof(*results) * (num_results + num_taxis));
                assert(results);
                memcpy(results + num_results, entry->taxi_near_locations,
                       sizeof(*entry->taxi_near_locations) * num_taxis);
                num_results += num_taxis;
            }
            /*
             * else check if its from the farthest taxi in the group.
             * If yes, take 50% of it from the center.
             */
            else if(fabs(taxi_location->latitude - entry->taxi_near_locations[entry->num_taxis-1]->latitude)
                    <= TAXI_DISTANCE_BIAS
                    &&
                    fabs(taxi_location->longitude - entry->taxi_near_locations[entry->num_taxis-1]->longitude)
                    <= TAXI_DISTANCE_BIAS)
            {
                int nearest_taxi = entry->num_taxis >> 1;
                int num_taxis = entry->num_taxis - nearest_taxi;
                results = realloc(results, sizeof(*results) * (num_results + num_taxis));
                assert(results);
                memcpy(results + num_results, entry->taxi_near_locations + nearest_taxi,
                       sizeof(*entry->taxi_near_locations) * num_taxis);
                num_results += num_taxis;
            }
        }
        if(taxi_location->latitude <= entry->latitude)
            iter = iter->left;
        else iter = iter->right;
    }

    if(num_results > 0)
    {
        *taxis = results;
        *num_taxis = num_results;
        err = 0;
    }

    /*
     * no taxis found within that radius
     */
    return err;
}

/*
 * Find taxis that are near latitude/longitude
 */

int find_taxis_by_location(double latitude, double longitude, 
                           struct taxi **matched_taxis, int *num_matches)
{
    struct taxi_location taxi_location = {.id = NULL, .id_len = 0,
                                          .latitude = latitude, .longitude = longitude };
    struct taxi_location **taxis = NULL;
    struct taxi *result = NULL;
    int num_taxis = 0;

    if(!matched_taxis || !num_matches) return -1;

    int err = __find_taxis_by_location(&taxi_location, NULL, &taxis, &num_taxis);
    if(err < 0)
    {
        output("No taxis found near location [%G:%G]\n", latitude, longitude);
        return err;
    }
    result = calloc(num_taxis, sizeof(*result));
    assert(result);
    for(int i = 0; i < num_taxis; ++i)
    {
        int len = taxis[i]->id_len > sizeof(result[i].id) ? sizeof(result[i].id) : taxis[i]->id_len;
        result[i].id_len = len;
        memcpy(result[i].id, taxis[i]->id, len);
        result[i].latitude = taxis[i]->latitude;
        result[i].longitude = taxis[i]->longitude;
    }
    *matched_taxis = result;
    *num_matches = num_taxis;
    if(taxis) free(taxis);
    return err;
}

#include "dlist.h"
#include "rbtree.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define output(fmt, arg...) do { fprintf(stdout, fmt, ##arg); } while(0)

struct segment_sort_list
{
    struct segment **segments;
    int nr_segments;
};

struct segment
{
    int start;
    int end;
    struct rbtree tree;
    struct segment_sort_list start_list;
    struct segment_sort_list end_list;
};

static struct rbtree_root segment_tree = { .root = NULL } ;

static __inline__ void segment_sort_list_init(struct segment_sort_list *list)
{
    list->nr_segments = 0;
    list->segments = NULL;
}

static int segment_startkey_cmp(const void *ref, const void *entry)
{
    struct segment *ref_segment = *(struct segment**)ref;
    struct segment *entry_segment = *(struct segment**)entry;
    return ref_segment->start - entry_segment->start;
}

static int segment_endkey_cmp(const void *ref, const void *entry)
{
    struct segment *ref_segment = *(struct segment **)ref;
    struct segment *entry_segment = *(struct segment **)entry;
    return ref_segment->end - entry_segment->end;
}

static __inline__ void segment_sort_list_insert(struct segment_sort_list *list, struct segment *segment,
                                                int (*key_cmp)(const void *, const void *))
{
    list->segments = realloc(list->segments, sizeof(*list->segments) * (list->nr_segments + 1));
    assert(list->segments);
    list->segments[list->nr_segments++] = segment;
    qsort(list->segments, list->nr_segments, sizeof(*list->segments), key_cmp);
}

static int segment_midkey_cmp(const void *ref, const void *entry)
{
    struct segment *ref_segment =   rbtree_entry( (struct rbtree*)ref, struct segment, tree);
    struct segment *entry_segment = rbtree_entry( (struct rbtree*)entry, struct segment, tree);
    int refkey = (ref_segment->start + ref_segment->end) >> 1;
    int entrykey = (entry_segment->start - entry_segment->end) >> 1;
    return refkey - entrykey;
}

static void segment_tree_intersect(struct rbtree_root *root, int point, struct segment ***segments, int *nr_segments)
{
    struct rbtree *iter;
    struct segment **results = NULL;
    int nr_results = 0;
    register int i;
    rbtree_for_each(iter, root)
    {
        struct segment *entry = rbtree_entry(iter, struct segment, tree);
        int key = entry->start + entry->end;
        key >>= 1;
        if(point < key)
        {
            for(i = 0; i < entry->start_list.nr_segments; ++i)
            {
                if(entry->start_list.segments[i]->start <= point)
                {
                    results = realloc(results, (nr_results+1)*sizeof(*results));
                    assert(results);
                    results[nr_results++] = entry->start_list.segments[i];
                }
                else break;
            }
        }
        else if(point > key)
        {
            for(i = 0; i < entry->end_list.nr_segments; ++i)
            {
                if(entry->end_list.segments[i]->end < point) continue;
                results = realloc(results, sizeof(*results) * ( entry->end_list.nr_segments - i + nr_results));
                assert(results);
                memcpy(&results[nr_results], &entry->end_list.segments[i], 
                       (entry->end_list.nr_segments - i) * sizeof(*entry->end_list.segments));
                nr_results += entry->end_list.nr_segments - i;
                break;
            }
        }
        else
        {
            results = realloc(results, sizeof(*results) * (entry->start_list.nr_segments + nr_results));
            assert(results);
            memcpy(&results[nr_results], entry->start_list.segments, 
                   sizeof(*entry->start_list.segments) * entry->start_list.nr_segments);
            nr_results += entry->start_list.nr_segments;
        }
    }
    *segments = results;
    *nr_segments = nr_results;
}

static struct rbtree *segment_tree_insert(struct rbtree_root *root, struct rbtree *tree,
                                          int (*key_cmp)(const void *, const void *))
{
    struct rbtree *parent = NULL;
    struct rbtree **node = &root->root;
    int cmp = 0;
    while(*node)
    {
        parent = *node;
        cmp = key_cmp((const void *)tree, (const void *)*node);
        if(cmp < 0)
            node = &(*node)->left;
        else if(cmp > 0)
            node = &(*node)->right;
        else return parent;
    }
    __rbtree_insert(tree, parent, node);
    rbtree_insert_colour(root, tree);
    return tree;
}

static void __segment_range_insert(struct segment *segment)
{
    struct rbtree *index;
    struct segment *parent = segment;
    index = segment_tree_insert(&segment_tree, &segment->tree, segment_midkey_cmp);
    if(index != &segment->tree)
    {
        parent = rbtree_entry(index, struct segment, tree);
        output("Segment [%d:%d] mid point overlap with existing segment [%d:%d]. "
               "Adding start and end ranges to the existing segment index range tree\n",
               segment->start, segment->end, parent->start, parent->end);
    }
    else output("Segment [%d:%d] added to the range tree\n", segment->start, segment->end);
    segment_sort_list_insert(&parent->start_list, segment, segment_startkey_cmp);
    segment_sort_list_insert(&parent->end_list, segment, segment_endkey_cmp);
}

/*
 * Add a segment to the rbtree.
 */
static void segment_add(int start, int end)
{
    struct segment *segment = calloc(1, sizeof(*segment));
    assert(segment);
    segment->start = start;
    segment->end = end;
    segment_sort_list_init(&segment->start_list);
    segment_sort_list_init(&segment->end_list);
    __segment_range_insert(segment);
}

static void read_query_segments(const char *filename, int search_index)
{
    FILE *fptr;
    char buf[512];
    int lineno = 0;
    struct segment **overlap_segments = NULL;
    int nr_segments = 0;
    fptr = fopen(filename, "r");
    if(!fptr) 
    {
        perror("fopen:");
        return;
    }
    while(fgets(buf, sizeof(buf), fptr))
    {
        int bytes = strlen(buf);
        char *s = buf;
        int start = 0;
        int end  = 0;
        char *endp = NULL;
        if(buf[bytes-1] == '\n')
            buf[bytes-1] = 0;
        ++lineno;
        s += strspn(s, " \t");
        if(*s == '#' || !*s) continue; /*comment*/
        start = (int)strtol(s, &endp, 10);
        if(endp == s) 
        {
            output("Ignoring line [%d] because of invalid input: start [%s]\n", lineno, s);
            continue;
        }
        s = endp;
        s += strspn(s, " \t");
        end = (int)strtol(s, &endp, 10);
        if(endp == s)
        {
            output("Ignoring line [%d] because of invalid input: end [%s]\n", lineno, s);
            continue;
        }
        output("Adding line segment [%d:%d]\n", start, end);
        segment_add(start, end);
    }
    fclose(fptr);
    printf("Querying segments that overlap index [%d]\n", search_index);
    segment_tree_intersect(&segment_tree, search_index, &overlap_segments, &nr_segments);
    if(nr_segments > 0)
    {
        register int i;
        printf("Found [%d] overlapping segments\n", nr_segments);
        for(i = 0; i < nr_segments; ++i)
            printf("Overlapping segment [%d:%d]\n", overlap_segments[i]->start, overlap_segments[i]->end); 
        free(overlap_segments);
    }

}

int main(int argc, char **argv)
{
    char filename[0xff+1];
    int search_index = 0;
    filename[0] = 0;
    if(argc != 2 && argc != 3)
        exit(127);
    if(argc > 1)
        strncat(filename, argv[1], sizeof(filename)-1);
    if(argc > 2)
        search_index = atoi(argv[2]);
    read_query_segments(filename, search_index);
    return 0;
}

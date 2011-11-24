#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include "dispatcher.h"

#define DISPATCHER_LOCK() do { pthread_mutex_lock(&g_dispatcher_lock); }while(0)
#define DISPATCHER_UNLOCK() do { pthread_mutex_unlock(&g_dispatcher_lock); }while(0)
#define DISPATCHER_WAKEUP() do { pthread_cond_signal(&g_dispatcher_cond); }while(0)
#define DISPATCHER_WAIT()   do { pthread_cond_wait(&g_dispatcher_cond, &g_dispatcher_lock); }while(0)
struct dispatcher
{
    int fd;
    int events;
    void *arg;
    int (*callback)(int fd, void *arg);
};
static struct dispatcher *g_dispatcher_fds;
static int g_num_fds;
static int g_dispatcher_pipe[2];
static int g_dispatcher_running;
static pthread_mutex_t g_dispatcher_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_dispatcher_cond = PTHREAD_COND_INITIALIZER;

static int get_dispatcher(int index, int fd)
{
    if(index >= 0 && index < g_num_fds && g_dispatcher_fds[index].fd == fd)
        return index;

    for(int i = 0; i < g_num_fds; ++i)
    {
        if(g_dispatcher_fds[i].fd == fd)
            return i;
    }
    return -1;
}

int dispatcher_breaker(void)
{
    int c = 'w';
    return write(g_dispatcher_pipe[1], &c, 1) == 1 ? 0 : -1;
}

int dispatcher_register(int fd, int events, void *arg, int (*cb)(int fd, void *arg))
{
    int err = -1;
    if(fd < 0 || !cb) goto out;
    DISPATCHER_LOCK();
    g_dispatcher_fds = realloc(g_dispatcher_fds, sizeof(*g_dispatcher_fds) * (g_num_fds+1));
    assert(g_dispatcher_fds != NULL);
    g_dispatcher_fds[g_num_fds].fd = fd;
    g_dispatcher_fds[g_num_fds].callback = cb;
    g_dispatcher_fds[g_num_fds].arg = arg;
    if(!events)
        events = POLLIN | POLLRDNORM;
    g_dispatcher_fds[g_num_fds].events = events;
    ++g_num_fds;
    if(fd != g_dispatcher_pipe[0])
    {
        dispatcher_breaker();
    }
    if(g_dispatcher_running)
    {
        DISPATCHER_WAKEUP();
    }
    DISPATCHER_UNLOCK();
    err = 0;
    out:
    return err;
}

int dispatcher_deregister(int fd)
{
    int err = -1;
    int index;
    DISPATCHER_LOCK();
    index = get_dispatcher(-1, fd);
    if(index < 0)
    {
        goto out_unlock;
    }
    --g_num_fds;
    memmove(g_dispatcher_fds + index, g_dispatcher_fds + index + 1, 
            sizeof(*g_dispatcher_fds) * (g_num_fds - index));
    dispatcher_breaker();
    DISPATCHER_WAKEUP();
    err = 0;
    out_unlock:
    DISPATCHER_UNLOCK();
    return err;
}

static int get_pollfds(int fds, struct pollfd *cur_pollfds)
{
    if(fds > g_num_fds) return -1;
    for(int i = 0; i < fds; ++i)
    {
        memset(cur_pollfds+i, 0, sizeof(*cur_pollfds));
        cur_pollfds[i].fd = g_dispatcher_fds[i].fd;
        cur_pollfds[i].events = g_dispatcher_fds[i].events;
        cur_pollfds[i].revents = 0;
    }
    return 0;
}

static int dispatcher_invoke(struct pollfd *cur_pollfds, int cur_fds, int max_fds)
{
    int err = 0;
    int c = 0;
    struct dispatcher *dispatcher_list = calloc(max_fds, sizeof(*dispatcher_list));
    assert(dispatcher_list);
    for(int i = 0; i < cur_fds && c < max_fds; ++i)
    {
        int index = get_dispatcher(i, cur_pollfds[i].fd);
        if(index < 0)
        {
            fprintf(stderr, "Unable to find dispatcher for fd [%d]\n", cur_pollfds[i].fd);
            continue;
        }
        struct dispatcher *dispatcher = g_dispatcher_fds + index;
        if( (dispatcher->events & cur_pollfds[i].revents) )
        {
            memcpy(&dispatcher_list[c++], dispatcher, sizeof(*dispatcher));
        }
    }
    DISPATCHER_UNLOCK();
    for(int i = 0; i < c; ++i)
    {
        err |= dispatcher_list[i].callback(dispatcher_list[i].fd, dispatcher_list[i].arg);
    }
    free(dispatcher_list);
    DISPATCHER_LOCK();
    return err;
}

static void *dispatcher_thread(void *arg)
{
    struct pollfd *cur_pollfds = NULL;
    int cur_fds = 0;
    int cur_max_fds = 0;
    DISPATCHER_LOCK();
    while(g_dispatcher_running)
    {
        while(!g_num_fds && g_dispatcher_running)
        {
            DISPATCHER_WAIT();
        }
        if(!g_dispatcher_running) break;
        if(cur_max_fds < g_num_fds)
        {
            cur_max_fds = g_num_fds;
            cur_pollfds = realloc(cur_pollfds, sizeof(*cur_pollfds) * cur_max_fds);
            assert(cur_pollfds);
        }
        cur_fds = g_num_fds;
        if(get_pollfds(cur_fds, cur_pollfds) < 0)
        {
            break;
        }
        DISPATCHER_UNLOCK();
        int num_fds;
        retry:
        num_fds = poll(cur_pollfds, cur_fds, -1);
        if(num_fds < 0)
        {
            if(errno == EINTR)
                goto retry;
            fprintf(stderr, "Dispatcher poll error [%s]\n", strerror(errno));
            DISPATCHER_LOCK();
            g_dispatcher_running = 0;
            break;
        }
        DISPATCHER_LOCK();
        if(num_fds > 0)
        {
            dispatcher_invoke(cur_pollfds, cur_fds, num_fds);
        }
    }
    DISPATCHER_WAKEUP();
    DISPATCHER_UNLOCK();
    if(cur_pollfds)
        free(cur_pollfds);
    return NULL;
}

static int breaker_callback(int fd, void *arg)
{
    int dummy;
    read(fd, &dummy, 1);
    return 0;
}

int dispatcher_initialize(void)
{
    int err = -1;
    if(pipe(g_dispatcher_pipe) < 0)
    {
        fprintf(stderr, "Error creating dispatcher breaker pipe: [%s]\n", strerror(errno));
        goto out;
    }
    err = dispatcher_register(g_dispatcher_pipe[0], POLLIN|POLLRDNORM, NULL, breaker_callback);
    if(err < 0)
        goto out_close;
    g_dispatcher_running = 1;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    err = pthread_create(&tid, &attr, dispatcher_thread, NULL);
    if(err < 0)
    {
        g_dispatcher_running = 0;
        fprintf(stderr, "Error creating dispatcher thread: [%s]\n", strerror(errno));
        goto out_close;
    }
    goto out;

    out_close:
    close(g_dispatcher_pipe[0]);
    close(g_dispatcher_pipe[1]);
    out:
    return err;
}

int dispatcher_finalize(void)
{
    int err = -1;
    DISPATCHER_LOCK();
    if(!g_dispatcher_running)
    {
        DISPATCHER_UNLOCK();
        goto out;
    }
    g_dispatcher_running = 0;
    dispatcher_breaker();
    DISPATCHER_WAKEUP();
    DISPATCHER_WAIT();
    DISPATCHER_UNLOCK();
    if(g_num_fds > 0)
    {
        g_num_fds = 0;
    }
    if(g_dispatcher_fds)
    {
        free(g_dispatcher_fds);
        g_dispatcher_fds = NULL;
    }
    close(g_dispatcher_pipe[0]);
    close(g_dispatcher_pipe[1]);
    err = 0;

    out:
    return err;
}

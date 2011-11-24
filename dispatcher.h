#ifndef _DISPATCHER_H_
#define _DISPATCHER_H_

#ifdef __cplusplus
extern "C" {
#endif

extern int dispatcher_initialize(void);
extern int dispatcher_finalize(void);
extern int dispatcher_register(int fd, int events, void *arg, int (*cb)(int fd, void *arg));
extern int dispatcher_deregister(int fd);

#ifdef __cplusplus
}
#endif

#endif

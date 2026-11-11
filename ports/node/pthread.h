/* pthread.h - just enough for QuickJS, which includes it for Atomics.wait.
 * MyOS programs have one thread, so locks never wait and nothing blocks
 * (QuickJS only blocks in Atomics.wait, which it refuses on the main thread). */
#ifndef MYOS_PTHREAD_H
#define MYOS_PTHREAD_H
#include <errno.h>
#include <time.h>

typedef int pthread_mutex_t;
typedef int pthread_cond_t;
typedef int pthread_mutexattr_t;
typedef int pthread_condattr_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER  0

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; *m = 0; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_condattr_init(pthread_condattr_t *a) { *a = 0; return 0; }
static inline int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c) { (void)a; (void)c; return 0; }
static inline int pthread_condattr_destroy(pthread_condattr_t *a) { (void)a; return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; *c = 0; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) {
    (void)c; (void)m; (void)t;
    return ETIMEDOUT;
}

#endif

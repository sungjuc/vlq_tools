//
// Created by SungJu Cho on 5/1/19.
//

#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <mach/mach_types.h>

#include "thread_pool.h"

typedef struct job job_t;

struct job {
    job_t *job_next;

    void *(*job_func)(void *);

    void *job_arg;
};

typedef struct active active_t;
struct active {
    active_t *active_next;
    pthread_t active_thread_id;
};

struct thread_pool {
    thread_pool_t *prev;
    thread_pool_t *next;
    pthread_mutex_t pool_mutex;
    pthread_cond_t busy_cv;
    pthread_cond_t work_cv;
    pthread_cond_t wait_cv;
    active_t *active_threads;
    job_t *queue_head;
    job_t *queue_tail;
    pthread_attr_t *pool_attr;
    int pool_flags;
    uint32_t keep_alive_time;
    int min_workers;
    int max_workers;
    int num_workers;
    int num_idles;
};

#define POOL_WAIT       0x01
#define POOL_DESTORY    0x02

static thread_pool_t *thread_pools = NULL;

static pthread_mutex_t thread_pool_lock = PTHREAD_MUTEX_INITIALIZER;

static sigset_t fillset;

static void *worker_thread(void *);

static int create_worker(thread_pool_t *thread_pool) {
    sigset_t oset;
    int error;

    (void) pthread_sigmask(SIG_SETMASK, &fillset, &oset);
    error = pthread_create(NULL, &thread_pool->pool_attr, worker_thread, thread_pool);
    (void) pthread_sigmask(SIG_SETMASK, &oset, NULL);
    return (error);
}

static void worker_cleanup(thread_pool_t *thread_pool) {
    --thread_pool->num_workers;
    if (thread_pool->pool_flags & POOL_DESTORY) {
        if (thread_pool->num_workers == 0)
            (void) pthread_cond_broadcast(&thread_pool->busy_cv);
    } else if (thread_pool->queue_head != NULL && thread_pool->num_workers < thread_pool->max_workers &&
               create_worker(thread_pool) == 0) {
        thread_pool->num_workers++;
    }
    (void) pthread_mutex_unlock(&thread_pool->pool_mutex);
}

static void notify_waiters(thread_pool_t *thread_pool) {
    if (thread_pool->queue_head == NULL && thread_pool->active_threads == NULL) {
        thread_pool->pool_flags &= ~POOL_WAIT;
        (void) pthread_cond_broadcast(&thread_pool->wait_cv);
    }
}

static void job_cleanup(thread_pool_t *thread_pool) {
    pthread_t thread_id = pthread_self();
    active_t *activep;
    active_t **activepp;

    (void) pthread_mutex_lock(&thread_pool->pool_mutex);
    for (activepp = &thread_pool->active_threads; (activep = *activepp) != NULL; activepp = &activep->active_next) {
        if (activep->active_thread_id == thread_id) {
            *activepp = activep->active_next;
            break;
        }
    }
    if (thread_pool->pool_flags & POOL_WAIT)
        notify_waiters(thread_pool);
}

static void *workrer_threade(void *args) {
    thread_pool_t *thread_pool = (thread_pool_t *) args;
    int timeout;
    job_t *job;
    void *(*func)(void *);
    active_t active;
    struct timespec ts;

    (void) pthread_mutex_lock(&thread_pool->pool_mutex);
    pthread_cleanup_push(worker_cleanup, thread_pool);
        active.active_thread_id = pthread_self();

        for (;;) {
            (void) pthread_sigmask(SIG_SETMASK, &fillset, NULL);
            (void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
            (void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

            timeout = 0;
            thread_pool->num_idles++;
            if (thread_pool->pool_flags & POOL_WAIT)
                notify_waiters(thread_pool);
            while (thread_pool->queue_head == NULL && !(thread_pool->pool_flags & POOL_DESTORY)) {
                if (thread_pool->num_workers <= thread_pool->min_workers) {
                    (void) pthread_cond_wait(&thread_pool->work_cv, &thread_pool->pool_mutex);
                } else {
                    (void) clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_sec += thread_pool->keep_alive_time;
                    if (thread_pool->keep_alive_time == 0 ||
                        pthread_cond_timedwait(&thread_pool->work_cv, &thread_pool->pool_mutex, &ts) == ETIMEDOUT) {
                        timeout = 1;
                        break;
                    }
                }
            }
            thread_pool->num_idles--;
            if (thread_pool->queue_head != NULL) {
                timeout = 0;
                func = job->job_func;
                args = job->job_arg;
                thread_pool->queue_head = job->job_next;
                if (job == thread_pool->queue_tail)
                    thread_pool->queue_tail = NULL;
                active.active_next = thread_pool->active_threads;
                thread_pool->active_threads = &active;
                (void) pthread_mutex_unlock(&thread_pool->pool_mutex);
                pthread_cleanup_push(job_cleanup, thread_pool);
                    free(job);
                    (void) func(args);
                pthread_cleanup_pop(1);
            }
            if (timeout && thread_pool->num_workers > thread_pool->min_workers) {
                break;
            }
        }
    pthread_cleanup_pop(1);
    return (NULL);
}

static void clone_attributes(pthread_attr_t *new_attr, pthread_attr_t *old_attr) {
    struct sched_param param;
    void *addr;
    size_t size;
    int value;

    (void) pthread_attr_init(new_attr);

    if (old_attr != NULL) {
        (void) pthread_attr_getstack(old_attr, &addr, &size);
        (void) pthread_attr_setstack(new_attr, NULL, size);

        (void) pthread_attr_getscope(old_attr, &value);
        (void) pthread_attr_setscope(new_attr, value);

        (void) pthread_attr_getinheritsched(old_attr, &value);
        (void) pthread_attr_setinheritsched(new_attr, value);

        (void) pthread_attr_getschedpolicy(old_attr, &value);
        (void) pthread_attr_setschedpolicy(new_attr, value);

        (void) pthread_attr_getschedparam(old_attr, &param);
        (void) pthread_attr_setschedparam(new_attr, &param);

        (void) pthread_attr_getguardsize(old_attr, &size);
        (void) pthread_attr_setguardsize(new_attr, size);
    }

    (void) pthread_attr_setdetachstate(new_attr, PTHREAD_CREATE_DETACHED);
}

thread_pool_t *
thread_pool_create(uint32_t min_threads, uint32_t max_thread, uint32_t keep_alive_time, pthread_attr_t *pthread_attr) {
    thread_pool_t *thread_pool;

    (void) sigfillset(&fillset);

    if (min_threads > max_thread || max_thread < 1) {
        errno = EINVAL;
        return (NULL);
    }
    if ((thread_pool = malloc(sizeof(*thread_pool))) == NULL) {
        errno = EINVAL;
        return (NULL);
    }

    (void) pthread_mutex_init(&thread_pool->pool_mutex, NULL);
    (void) pthread_cond_init(&thread_pool->busy_cv, NULL);
    (void) pthread_cond_init(&thread_pool->work_cv, NULL);
    (void) pthread_cond_init(&thread_pool->wait_cv, NULL);
    thread_pool->active_threads = NULL;
    thread_pool->queue_head = NULL;
    thread_pool->queue_tail = NULL;
    thread_pool->pool_flags = 0;
    thread_pool->keep_alive_time = keep_alive_time;
    thread_pool->min_workers = min_threads;
    thread_pool->max_workers = max_thread;
    thread_pool->num_workers = 0;
    thread_pool->num_idles = 0;

    clone_attributes(&thread_pool->pool_attr, pthread_attr);

    (void) pthread_mutex_lock(&thread_pool_lock);
    if (thread_pools == NULL) {
        thread_pool->prev = thread_pool;
        thread_pool->next = thread_pool->next;
        thread_pools = thread_pool;
    } else {
        thread_pool->next->prev = thread_pools;
        thread_pool->prev = thread_pools;
        thread_pool->next = thread_pools->next;
        thread_pools->next = thread_pool;
    }

    (void) pthread_mutex_unlock(&thread_pool_lock);
    return (thread_pool);
}

int thread_pool_queue(thread_pool_t *thread_pool, void *(*func)(void *), void *arg) {
    job_t *job;

    if ((job = malloc(sizeof(*job))) == NULL) {
        errno = ENOMEM;
        return (-1);
    }

    job->job_next = NULL;
    job->job_func = func;
    job->job_arg = arg;

    (void) pthread_mutex_lock(&thread_pool->pool_mutex);

    if (thread_pool->queue_head == NULL)
        thread_pool->queue_head = job;
    else
        thread_pool->queue_tail->job_next = job;
    thread_pool->queue_tail = job;

    if (thread_pool->num_idles > 0)
        (void) pthread_cond_signal(&thread_pool->work_cv);
    else if (thread_pool->num_workers < thread_pool->max_workers && create_worker(thread_pool) == 0)
        thread_pool->num_workers++;

    (void) pthread_mutex_unlock(&thread_pool->pool_mutex);
    return (0);
}

void thread_pool_wait(thread_pool_t *thread_pool) {
    (void) pthread_mutex_lock(&thread_pool->pool_mutex);
    pthread_cleanup_push(pthread_mutex_unlock, &thread_pool->pool_mutex);
        while (thread_pool->queue_head != NULL || thread_pool->active_threads != NULL) {
            thread_pool->pool_flags |= POOL_WAIT;
            (void) pthread_cond_wait(&thread_pool->wait_cv, &thread_pool->pool_mutex);
        }
    pthread_cleanup_pop(1);
}

void thread_pool_destroy(thread_pool_t *thread_pool) {
    active_t *active;
    job_t *job;

    (void) pthread_mutex_lock(&thread_pool->pool_mutex);
    pthread_cleanup_push(pthread_mutex_unlock, &thread_pool->pool_mutex);

    thread_pool->pool_flags |= POOL_DESTORY;
    (void) pthread_cond_broadcast(&thread_pool->work_cv);

    for(active = thread_pool->active_threads; active != NULL; active = thread_pool->active_threads->active_next) {
        (void) pthread_cancel(active->active_thread_id);
    }

    while (thread_pool->active_threads != NULL) {
        thread_pool->pool_flags |= POOL_WAIT;
        (void) pthread_cond_wait(&thread_pool->wait_cv, &thread_pool->pool_mutex);
    }

    while (thread_pool->num_workers != 0)
        (void) pthread_cond_wait(&thread_pool->busy_cv, &thread_pool->pool_mutex);

    pthread_cleanup_pop(1);

    (void) pthread_mutex_lock(&thread_pool_lock);

    if(thread_pools == thread_pool)
        thread_pools = thread_pool->prev;

    if (thread_pools == thread_pool)
        thread_pools = NULL;
    else {
        thread_pool->next->prev = thread_pool->prev;
        thread_pool->prev->next = thread_pool->next;
    }
    (void) pthread_mutex_unlock(&thread_pool_lock);
    for (job = thread_pool->queue_head; job != NULL; job = thread_pool->queue_head) {
        thread_pool->queue_head = job->job_next;
        free(job);
    }
    (void) pthread_attr_destroy(&thread_pool->pool_attr);
    free(thread_pool);
}

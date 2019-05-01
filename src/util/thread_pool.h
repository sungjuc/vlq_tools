//
// Created by SungJu Cho on 5/1/19.
//
#include <pthread.h>
#ifndef TRAFFIC_GENERATOR_THREAD_POOL_H
#define TRAFFIC_GENERATOR_THREAD_POOL_H

typedef struct thread_pool thread_pool_t;

extern thread_pool_t *thread_pool_create(uint32_t min_threads, uint32_t max_threads, uint32_t keep_alive_time,
                                         pthread_attr_t *pthread_attr);

extern int thread_pool_queue(thread_pool_t *thread_pool, void *(*func)(void *), void *arg);

extern void thread_pool_wait(thread_pool_t *thread_pool);

extern void thread_pool_destroy(thread_pool_t *thread_pool);

#endif //TRAFFIC_GENERATOR_THREAD_POOL_H


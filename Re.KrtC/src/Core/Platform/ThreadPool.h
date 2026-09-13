#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "../Utils/KrtCommon.h"
#include <pthread.h>

typedef struct ThreadTask {
    void (*function)(void* arg);
    void* arg;
    struct ThreadTask* next;
} ThreadTask;

typedef struct ThreadPool {
    pthread_t* threads;
    ThreadTask* task_queue;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    int thread_count;
    int shutdown;
    int active_tasks;
} ThreadPool;

ThreadPool* thread_pool_create(int thread_count);
void thread_pool_destroy(ThreadPool* pool);
void thread_pool_submit(ThreadPool* pool, void (*function)(void* arg), void* arg);
void thread_pool_wait(ThreadPool* pool);
int thread_pool_get_active_tasks(ThreadPool* pool);

#endif

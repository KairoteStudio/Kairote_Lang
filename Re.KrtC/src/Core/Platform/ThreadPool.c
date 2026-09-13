#include "ThreadPool.h"
#include <stdlib.h>
#include <stdio.h>

static void* thread_pool_worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;

    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);

        while (pool->task_queue == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        ThreadTask* task = pool->task_queue;
        pool->task_queue = task->next;
        pool->active_tasks++;

        pthread_mutex_unlock(&pool->queue_mutex);

        task->function(task->arg);

        pthread_mutex_lock(&pool->queue_mutex);
        pool->active_tasks--;
        pthread_cond_broadcast(&pool->queue_cond);
        pthread_mutex_unlock(&pool->queue_mutex);

        KRT_FREE(task);
    }

    return NULL;
}

ThreadPool* thread_pool_create(int thread_count) {
    if (thread_count <= 0) {
        thread_count = 4;
    }

    ThreadPool* pool = (ThreadPool*)KRT_MALLOC(sizeof(ThreadPool));
    if (!pool) {
        return NULL;
    }

    pool->threads = (pthread_t*)KRT_MALLOC(sizeof(pthread_t) * thread_count);
    if (!pool->threads) {
        KRT_FREE(pool);
        return NULL;
    }

    pool->task_queue = NULL;
    pool->thread_count = thread_count;
    pool->shutdown = 0;
    pool->active_tasks = 0;

    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        KRT_FREE(pool->threads);
        KRT_FREE(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->queue_mutex);
        KRT_FREE(pool->threads);
        KRT_FREE(pool);
        return NULL;
    }

    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_pool_worker, pool) != 0) {

            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->queue_cond);

            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }

            pthread_mutex_destroy(&pool->queue_mutex);
            pthread_cond_destroy(&pool->queue_cond);
            KRT_FREE(pool->threads);
            KRT_FREE(pool);
            return NULL;
        }
    }

    return pool;
}

void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    ThreadTask* task = pool->task_queue;
    while (task) {
        ThreadTask* next = task->next;
        KRT_FREE(task);
        task = next;
    }

    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_cond);
    KRT_FREE(pool->threads);
    KRT_FREE(pool);
}

void thread_pool_submit(ThreadPool* pool, void (*function)(void* arg), void* arg) {
    if (!pool || !function) {
        return;
    }

    ThreadTask* task = (ThreadTask*)KRT_MALLOC(sizeof(ThreadTask));
    if (!task) {
        return;
    }

    task->function = function;
    task->arg = arg;
    task->next = NULL;

    pthread_mutex_lock(&pool->queue_mutex);

    if (pool->task_queue == NULL) {
        pool->task_queue = task;
    } else {
        ThreadTask* last = pool->task_queue;
        while (last->next) {
            last = last->next;
        }
        last->next = task;
    }

    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
}

void thread_pool_wait(ThreadPool* pool) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->queue_mutex);
    while (pool->task_queue != NULL || pool->active_tasks > 0) {
        pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
    }
    pthread_mutex_unlock(&pool->queue_mutex);
}

int thread_pool_get_active_tasks(ThreadPool* pool) {
    if (!pool) {
        return 0;
    }

    pthread_mutex_lock(&pool->queue_mutex);
    int active = pool->active_tasks;
    pthread_mutex_unlock(&pool->queue_mutex);

    return active;
}

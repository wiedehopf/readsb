/*
Copyright (c) 2021, Youssef Touil <youssef@airspy.com>
Copyright (c) 2021, Matthias Wirth <matthias.wirth@gmail.com>

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following
conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include "threadpool.h"
#include <pthread.h>
#include <stdatomic.h>
//#include <stdio.h>
//#include <sys/types.h>
#include <unistd.h>
#include <string.h>


#define ATOMIC_WORKER_LOCK (-1)

typedef struct {
    int index;
    threadpool_t* pool;
    pthread_t pthread;
    struct timespec thread_time;

    threadpool_threadbuffers_t user_buffers;
} thread_t;

struct threadpool_t
{
    pthread_mutex_t worker_lock;
    pthread_cond_t notify_worker;

    pthread_mutex_t master_lock;
    pthread_cond_t notify_master;

    thread_t* threads;
    uint32_t thread_count;
    uint32_t terminate;
    atomic_intptr_t tasks;
    atomic_int task_count;
    atomic_int pending_count;
};

static void *threadpool_threadproc(void *threadpool);

static void normalize_timespec(struct timespec *ts) {
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec += ts->tv_nsec / 1000000000;
        ts->tv_nsec = ts->tv_nsec % 1000000000;
    } else if (ts->tv_nsec < 0) {
        long adjust = ts->tv_nsec / 1000000000 + 1;
        ts->tv_sec -= adjust;
        ts->tv_nsec = (ts->tv_nsec + 1000000000 * adjust) % 1000000000;
    }
}

struct timespec threadpool_get_cumulative_thread_time(threadpool_t* pool) {
    struct timespec sum = { 0, 0 };
    for (uint32_t i = 0; i < pool->thread_count; i++) {
        struct timespec ts = pool->threads[i].thread_time;
        sum.tv_sec += ts.tv_sec;
        sum.tv_nsec += ts.tv_nsec;
    }
    normalize_timespec(&sum);
    return sum;
}

threadpool_t *threadpool_create(uint32_t thread_count, uint32_t buffer_count)
{
    threadpool_t *pool = (threadpool_t *) malloc(sizeof(threadpool_t));

    pool->terminate = 0;
    atomic_store(&pool->task_count, 0);
    atomic_store(&pool->pending_count, 0);
    atomic_store(&pool->tasks, (intptr_t) NULL);
    pool->thread_count = thread_count;
    pool->threads = (thread_t *) malloc(sizeof(thread_t) * thread_count);

    pthread_mutex_init(&pool->worker_lock, NULL);
    pthread_cond_init(&pool->notify_worker, NULL);

    pthread_mutex_init(&pool->master_lock, NULL);
    pthread_cond_init(&pool->notify_master, NULL);

    for (uint32_t i = 0; i < thread_count; i++)
    {
        thread_t *thread = &pool->threads[i];
        thread->index = i;
        thread->pool = pool;
        thread->thread_time.tv_sec = 0;
        thread->thread_time.tv_nsec = 0;

        thread->user_buffers.buffer_count = buffer_count;
        thread->user_buffers.buffers = malloc(buffer_count * sizeof(threadpool_buffer_t));
        memset(thread->user_buffers.buffers, 0x0, buffer_count * sizeof(threadpool_buffer_t));

        pthread_create(&thread->pthread, NULL, threadpool_threadproc, thread);
    }

    return pool;
}

void threadpool_reset_buffers(threadpool_t *pool)
{
    for (uint32_t i = 0; i < pool->thread_count; i++)
    {
        thread_t *thread = &pool->threads[i];
        for (uint32_t k = 0; k < thread->user_buffers.buffer_count; k++) {
            free(thread->user_buffers.buffers[k].buf);
            thread->user_buffers.buffers[k].buf = NULL;
            thread->user_buffers.buffers[k].size = 0;
        }
    }
}

void threadpool_destroy(threadpool_t *pool)
{
    pool->terminate = 1;

    pthread_mutex_lock(&pool->worker_lock);
    atomic_store(&pool->task_count, 0);
    pthread_cond_broadcast(&pool->notify_worker);
    pthread_mutex_unlock(&pool->worker_lock);

    pthread_mutex_lock(&pool->master_lock);
    atomic_store(&pool->pending_count, 0);
    pthread_cond_broadcast(&pool->notify_master);
    pthread_mutex_unlock(&pool->master_lock);


    for (uint32_t i = 0; i < pool->thread_count; i++)
    {
        thread_t *thread = &pool->threads[i];
        pthread_join(thread->pthread, NULL);

        for (uint32_t k = 0; k < thread->user_buffers.buffer_count; k++) {
            free_threadpool_buffer(&thread->user_buffers.buffers[k]);
        }
        free(thread->user_buffers.buffers);
    }

    pthread_mutex_destroy(&pool->worker_lock);
    pthread_cond_destroy(&pool->notify_worker);

    pthread_mutex_destroy(&pool->master_lock);
    pthread_cond_destroy(&pool->notify_master);

    free(pool->threads);
    free(pool);
}

void threadpool_run(threadpool_t *pool, threadpool_task_t* tasks, uint32_t count)
{

    atomic_store(&pool->pending_count, count);
    atomic_store(&pool->tasks, (intptr_t) tasks);
    // incrementing task count means a thread could start doing work already
    // pending_count / tasks need to be in place, so this order is important
    atomic_store(&pool->task_count, count);

    pthread_mutex_lock(&pool->worker_lock);
    pthread_cond_broadcast(&pool->notify_worker); // wake up sleeping worker threads after task_count has been set
    pthread_mutex_unlock(&pool->worker_lock);

    pthread_mutex_lock(&pool->master_lock);
    while (atomic_load(&pool->pending_count) > 0 && !pool->terminate)
    {
        pthread_cond_wait(&pool->notify_master, &pool->master_lock);
    }
    pthread_mutex_unlock(&pool->master_lock);
}

static unsigned get_seed() {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return (time.tv_sec ^ time.tv_nsec ^ (getpid() << 16) ^ (uintptr_t) pthread_self());
}

static void *threadpool_threadproc(void *arg)
{
    srandom(get_seed());

    thread_t *thread = (thread_t *) arg;
    threadpool_t *pool = thread->pool;
    int task_count;

    while (1)
    {
        task_count = atomic_load(&pool->task_count);

        //fprintf(stderr, "%d %4d\n", thread->index, task_count);

        if (task_count == 0)
        {
            pthread_mutex_lock(&pool->worker_lock);

            if (pool->terminate)
            {
                pthread_mutex_unlock(&pool->worker_lock);
                return NULL;
            }
            // re-check task_count inside worker_lock before sleeping
            // this makes lost wakeup impossible
            // (task count is incremented BEFORE taking worker_lock to wake the workers)
            if (atomic_load(&pool->task_count) == 0)
            {
                // update thread_time
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread->thread_time);

                // wait until we have more work
                pthread_cond_wait(&pool->notify_worker, &pool->worker_lock);
            }

            pthread_mutex_unlock(&pool->worker_lock);

            continue;
        }

        int expected = task_count;
        task_count--;
        if (!atomic_compare_exchange_weak(&pool->task_count, &expected, task_count))
        {
            continue;
        }

        threadpool_task_t* task = (threadpool_task_t*) atomic_load(&pool->tasks) + task_count;

        task->function(task->argument, &thread->user_buffers);

        int pending_count = atomic_fetch_sub(&pool->pending_count, 1) - 1;

        if (pending_count == 0)
        {
            pthread_mutex_lock(&pool->master_lock);
            pthread_cond_broadcast(&pool->notify_master);
            pthread_mutex_unlock(&pool->master_lock);
        }
    }

    //pthread_exit(NULL);

    return NULL;
}

void free_threadpool_buffer(threadpool_buffer_t *buffer) {
    if (buffer->buf) {
        free(buffer->buf);
        buffer->buf = NULL;
        buffer->size = 0;
    }
#ifdef _THREADPOOL_WITH_ZSTD
    if (buffer->cctx) {
        ZSTD_freeCCtx(buffer->cctx);
        buffer->cctx = NULL;
    }
    if (buffer->dctx) {
        ZSTD_freeDCtx(buffer->dctx);
        buffer->dctx = NULL;
    }
#endif
}

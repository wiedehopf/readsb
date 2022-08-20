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

#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

// Minimal thread pool implementation using pthread.h and stdatomic.h
// with option per thread pointers (for readsb used for per thread buffers)

#if 1
    #define _THREADPOOL_WITH_ZSTD
    #include <zstd.h>
#endif

typedef struct {
    void *buf;
    ssize_t size;
#ifdef _THREADPOOL_WITH_ZSTD
    ZSTD_CCtx* cctx;
    ZSTD_DCtx* dctx;
#endif
} threadpool_buffer_t;

// this function is called when destroying a threadpool with buffers
// in case you're using the threadpool_buffer_t yourself, you can use this to free the buffers in it
void free_threadpool_buffer(threadpool_buffer_t *buffer);

typedef struct {
    uint32_t buffer_count;
    threadpool_buffer_t *buffers;
} threadpool_threadbuffers_t;

typedef struct threadpool_t threadpool_t;

// create a thread pool (number of threads, number of usable buffer_t structs in threadpool_threadbuffers_t)
threadpool_t *threadpool_create(uint32_t thread_count, uint32_t buffer_count);

// destroy the thread pool
void threadpool_destroy(threadpool_t *pool);

typedef void (* threadpool_function_t)(void *, threadpool_threadbuffers_t *);

typedef struct
{
    threadpool_function_t function;
    void *argument;
} threadpool_task_t;

// run count tasks defined in tasks using a function pointer and and argument each
// the count is not constrained by thread_count in any way
// the user is responsible to not call threadpool_run until the previous run has completed
// threadpool_run will block until all tasks have finished
void threadpool_run(threadpool_t *pool, threadpool_task_t *tasks, uint32_t count);

// get the cumulative thread time used by all threads in the threadpool
// note that the underlying counters are updated only when a thread finishes a
// task and around 1 second has elapsed since the last update
// this time is best effort to achieve optimal performance
struct timespec threadpool_get_cumulative_thread_time(threadpool_t* threadpool);

// only use this after rading the code for it
void threadpool_reset_buffers(threadpool_t *pool);

#endif /* _THREADPOOL_H_ */

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

// Minimal thread pool implementation using pthread.h and stdatomic.h

typedef struct threadpool_t threadpool_t;

typedef struct
{
    void (* function)(void *);
    void *argument;
} threadpool_task_t;

// create a thread pool
threadpool_t *threadpool_create(uint32_t thread_count);

// destroy the thread pool
void threadpool_destroy(threadpool_t *pool);

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


#endif /* _THREADPOOL_H_ */


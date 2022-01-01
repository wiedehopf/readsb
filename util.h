// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// track.h: aircraft state tracking prototypes
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2015 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef DUMP1090_UTIL_H
#define DUMP1090_UTIL_H

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define GZBUFFER_BIG (1 * 1024 * 1024)

#include <stdint.h>

#define sfree(x) do { free(x); x = NULL; } while (0)

int tryJoinThread(pthread_t *thread, int64_t timeout);
typedef struct {
    pthread_t pthread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char *name;
    int8_t joined;
    int8_t joinFailed;
} threadT;
void threadDestroyAll();
void threadInit(threadT *thread, char *name);
void threadCreate(threadT *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
void threadTimedWait(threadT *thread, struct timespec *ts, int64_t increment);
void threadSignalJoin(threadT *thread);

struct char_buffer
{
    char *buffer;
    size_t len;
    size_t alloc;
};
struct char_buffer readWholeFile(int fd, char *errorContext);
struct char_buffer readWholeGz(gzFile gzfp, char *errorContext);
int writeGz(gzFile gzfp, void *source, int toWrite, char *errorContext);

static inline void msleep(int64_t ms) {
    struct timespec slp = {ms / 1000, (ms % 1000) * 1000 * 1000};
    nanosleep(&slp, NULL);
}

/* Returns system time in milliseconds */
int64_t mstime (void);

int snprintHMS(char *buf, size_t bufsize, int64_t now);

int64_t msThreadTime(void);

/* Returns the time elapsed, in nanoseconds, from t1 to t2,
 * where t1 and t2 are 12MHz counters.
 */
int64_t receiveclock_ns_elapsed (int64_t t1, int64_t t2);

/* Same, in milliseconds */
int64_t receiveclock_ms_elapsed (int64_t t1, int64_t t2);

/* Normalize the value in ts so that ts->nsec lies in
 * [0,999999999]
 */
void normalize_timespec (struct timespec *ts);

struct timespec msToTimespec(int64_t ms);

/* record current CPU time in start_time */
void start_cpu_timing (struct timespec *start_time);

/* add difference between start_time and the current CPU time to add_to */
void end_cpu_timing (const struct timespec *start_time, struct timespec *add_to);

void start_monotonic_timing(struct timespec *start_time);
void end_monotonic_timing (const struct timespec *start_time, struct timespec *add_to);

// start watch for stopWatch
void startWatch(struct timespec *start_time);
// return elapsed time and set start_time to current time
int64_t stopWatch(struct timespec *start_time);
int64_t lapWatch(struct timespec *start_time);

// get nanoseconds and some other stuff for use with srand
unsigned int get_seed();

// increment target by increment in ms, if result is in the past, set target to now.
// specialized function for scheduling threads using pthreadcondtimedwait
void incTimedwait(struct timespec *target, int64_t increment);
void log_with_timestamp(const char *format, ...) __attribute__ ((format(printf, 1, 2)));

// based on a give epoch time in ms, calculate the nearest offset interval step
// offset must be smaller than interval, at offset seconds after the full minute
// is the first possible value, all additional return values differ by a multiple
// of interval
int64_t roundSeconds(int interval, int offset, int64_t epoch_ms);
ssize_t check_write(int fd, const void *buf, size_t count, const char *error_context);

int my_epoll_create();
void epollAllocEvents(struct epoll_event **events, int *maxEvents);

char *sprint_uuid(uint64_t id1, uint64_t id2, char *p);
char *sprint_uuid1_partial(uint64_t id1, char *p);
char *sprint_uuid1(uint64_t id1, char *p);
char *sprint_uuid2(uint64_t id2, char *p);

static inline int64_t imin(int64_t a, int64_t b) {
  if (a < b)
    return a;
  else
    return b;
}

static inline int64_t imax(int64_t a, int64_t b) {
  if (a > b)
    return a;
  else
    return b;
}

static inline double
norm_diff (double a, double pi)
{
    if (a < -pi)
        a +=  2 * pi;
    if (a > pi)
        a -=  2 * pi;

    return a;
}
static inline double
norm_angle (double a, double pi)
{
    if (a < 0)
        a +=  2 * pi;
    if (a >= 2 * pi)
        a -=  2 * pi;

    return a;
}

#endif

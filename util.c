// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// util.c: misc utilities
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
//
// This file incorporates work covered by the following copyright and
// license:
//
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  *  Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//
//  *  Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "readsb.h"
#include <sys/resource.h>


int64_t mstime(void) {
    if (Modes.synthetic_now)
        return Modes.synthetic_now;

    struct timeval tv;
    int64_t mst;

    gettimeofday(&tv, NULL);
    mst = ((int64_t) tv.tv_sec)*1000;
    mst += tv.tv_usec / 1000;
    return mst;
}

int64_t microtime(void) {
    if (Modes.synthetic_now)
        return 1000 * Modes.synthetic_now;

    struct timeval tv;
    int64_t mst;

    gettimeofday(&tv, NULL);
    mst = ((int64_t) tv.tv_sec) * 1000LL * 1000LL;
    mst += tv.tv_usec;
    return mst;
}

void milli_micro_seconds(int64_t *milli, int64_t *micro) {
    if (Modes.synthetic_now) {
        *milli = Modes.synthetic_now;
        *micro = 1000 * Modes.synthetic_now;
        return;
    }

    struct timeval tv;

    gettimeofday(&tv, NULL);
    *milli = ((int64_t) tv.tv_sec) * 1000 + ((int64_t) tv.tv_usec) / 1000;
    *micro = ((int64_t) tv.tv_sec) * (1000 * 1000) + ((int64_t) tv.tv_usec);
}

int64_t mono_micro_seconds() {
    if (Modes.synthetic_now) {
        return 1000 * Modes.synthetic_now;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t micro = ((int64_t) ts.tv_sec) * (1000 * 1000) + ((int64_t) ts.tv_nsec) / 1000;
    return micro;
}

int64_t mono_milli_seconds() {
    if (Modes.synthetic_now) {
        return Modes.synthetic_now;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t milli = ((int64_t) ts.tv_sec) * 1000 + ((int64_t) ts.tv_nsec) / (1000 * 1000);
    return milli;
}

int snprintHMS(char *buf, size_t bufsize, int64_t now) {
    time_t nowTime = nearbyint(now / 1000.0);
    struct tm local;
    localtime_r(&nowTime, &local);
    char timebuf[128];
    strftime(timebuf, 128, "%T", &local);
    return snprintf(buf, bufsize, "%s.%03d", timebuf, (int) (now % 1000));
}

int64_t msThreadTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ((int64_t) ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000));
}

int64_t nsThreadTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ((int64_t) ts.tv_sec * (1000LL * 1000LL * 1000LL) + ts.tv_nsec);
}

int64_t receiveclock_ns_elapsed(int64_t t1, int64_t t2) {
    return (t2 - t1) * 1000U / 12U;
}

int64_t receiveclock_ms_elapsed(int64_t t1, int64_t t2) {
    return (t2 - t1) / 12000U;
}

/* record current CPU time in start_time */
void start_cpu_timing(struct timespec *start_time) {
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, start_time);
}

/* add difference between start_time and the current CPU time to add_to */
void end_cpu_timing(const struct timespec *start_time, struct timespec *add_to) {
    struct timespec end_time;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_time);
    add_to->tv_sec += end_time.tv_sec - start_time->tv_sec;
    add_to->tv_nsec += end_time.tv_nsec - start_time->tv_nsec;
    normalize_timespec(add_to);
}

void timespec_add_elapsed(const struct timespec *start_time, const struct timespec *end_time, struct timespec *add_to) {
    add_to->tv_sec += end_time->tv_sec - start_time->tv_sec;
    add_to->tv_nsec += end_time->tv_nsec - start_time->tv_nsec;
    normalize_timespec(add_to);
}

void start_monotonic_timing(struct timespec *start_time) {
    clock_gettime(CLOCK_MONOTONIC, start_time);
}

void end_monotonic_timing(const struct timespec *start_time, struct timespec *add_to) {
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    add_to->tv_sec += end_time.tv_sec - start_time->tv_sec;
    add_to->tv_nsec += end_time.tv_nsec - start_time->tv_nsec;
    normalize_timespec(add_to);
}

/* record current monotonic time in start_time */
void startWatch(struct timespec *start_time) {
    clock_gettime(CLOCK_MONOTONIC, start_time);
}

// return elapsed time
int64_t stopWatch(struct timespec *start_time) {
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    int64_t res = ((int64_t) end_time.tv_sec * 1000UL + end_time.tv_nsec / 1000000UL)
        - ((int64_t) start_time->tv_sec * 1000UL + start_time->tv_nsec / 1000000UL);

    return res;
}

// return elapsed time and set start_time to current time
int64_t lapWatch(struct timespec *start_time) {
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    int64_t res = ((int64_t) end_time.tv_sec * 1000UL + end_time.tv_nsec / 1000000UL)
        - ((int64_t) start_time->tv_sec * 1000UL + start_time->tv_nsec / 1000000UL);

    if (start_time->tv_sec == 0 && start_time->tv_nsec == 0) {
        res = 0;
    }

    *start_time = end_time;
    return res;
}

// this is not cryptographic but much better than mstime() as a seed
unsigned int get_seed() {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    unsigned int seed = (uint64_t) time.tv_sec ^ (uint64_t) time.tv_nsec ^ (((uint64_t) getpid()) << 16) ^ (((uint64_t) (uintptr_t) pthread_self()) << 10);
    return seed;
}

// increment target by increment in ms, if result is in the past, set target to now.
// specialized function for scheduling threads using pthreadcondtimedwait
static void incTimedwait(struct timespec *target, int64_t increment) {
    struct timespec inc = msToTimespec(increment);
    target->tv_sec += inc.tv_sec;
    target->tv_nsec += inc.tv_nsec;
    normalize_timespec(target);
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t min_sleep = 50 * 1000; // always wait a bit (50 us) to yield (i hope)
    if (target->tv_sec < now.tv_sec || (target->tv_sec == now.tv_sec && target->tv_nsec <= now.tv_nsec + min_sleep)) {
        target->tv_sec = now.tv_sec;
        target->tv_nsec = now.tv_nsec + min_sleep;
        normalize_timespec(target);
    }
}

#define uThreadMax (32)
static threadT *uThreads[uThreadMax];
static int uThreadCount = 0;

void threadInit(threadT *thread, char *name) {
    if (uThreadCount >= uThreadMax) {
        fprintf(stderr, "util.c: increase uThreadmax!\n");
        exit(1);
    }
    if (uThreadCount == 0) {
        memset(uThreads, 0, sizeof (uThreads));
    }
    memset(thread, 0, sizeof (threadT));
    pthread_mutex_init(&thread->mutex, NULL);
    pthread_cond_init(&thread->cond, NULL);
    thread->name = strdup(name);
    uThreads[uThreadCount++] = thread;
    thread->joined = 1;
}
void threadCreate(threadT *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg) {
    if (!thread->joined) {
        fprintf(stderr, "<3>FATAL: threadCreate() thread %s failed: already running?\n", thread->name);
        setExit(2);
    }
    int res = pthread_create(&thread->pthread, attr, start_routine, arg);
    if (res != 0) {
        fprintf(stderr, "<3>FATAL: threadCreate() pthread_create() failed: %s\n", strerror(res));
        setExit(2);
    }
    thread->joined = 0;
    thread->joinFailed = 0;
}
static void threadDestroy(threadT *thread) {
    // if the join didn't work, don't clean up
    if (!thread->joined || thread->joinFailed) {
        fprintf(stderr, "<3>FATAL: thread %s could not be joined, calling abort()!\n", thread->name);
        abort();
    }

    pthread_mutex_destroy(&thread->mutex);
    pthread_cond_destroy(&thread->cond);
    sfree(thread->name);
}
void threadDestroyAll() {
    for (int i = 0; i < uThreadCount; i++) {
        threadDestroy(uThreads[i]);
    }
    uThreadCount = 0;
}
void threadTimedWait(threadT *thread, struct timespec *ts, int64_t increment) {
    // don't wait when we want to exit
    if (Modes.exit)
        return;
    incTimedwait(ts, increment);
    int err = pthread_cond_timedwait(&thread->cond, &thread->mutex, ts);
    if (err && err != ETIMEDOUT)
        fprintf(stderr, "%s thread: pthread_cond_timedwait unexpected error: %s\n", thread->name, strerror(err));
}

void threadSignalJoin(threadT *thread) {
    if (thread->joined)
        return;
    int64_t timeout = Modes.joinTimeout;
    int err = 0;
    while ((err = pthread_tryjoin_np(thread->pthread, NULL)) && timeout-- > 0) {
        pthread_cond_signal(&thread->cond);
        msleep(1);
    }
    if (err == 0) {
        thread->joined = 1;
    } else {
        thread->joinFailed = 1;
        fprintf(stderr, "%s thread: threadSignalJoin timed out after %.1f seconds, undefined behaviour may result!\n", thread->name, (float) Modes.joinTimeout / (float) SECONDS);
        Modes.joinTimeout /= 2;
        Modes.joinTimeout = imax(Modes.joinTimeout, 2 * SECONDS);
    }
}

int threadAffinity(int core_id) {
    int num_cores = Modes.num_procs;
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

struct char_buffer readWholeFile(int fd, char *errorContext) {
    struct char_buffer cb = {0};
    struct stat fileinfo = {0};
    if (fstat(fd, &fileinfo)) {
        fprintf(stderr, "%s: readWholeFile: fstat failed, wat?!\n", errorContext);
        return cb;
    }
    size_t fsize = fileinfo.st_size;

    int extra = 128 * 1024;
    cb.buffer = cmalloc(fsize + extra);
    memset(cb.buffer, 0x0, fsize + extra); // zero entire buffer
    if (!cb.buffer) {
        fprintf(stderr, "%s: readWholeFile couldn't allocate buffer!\n", errorContext);
        return cb;
    }
    int64_t res = 0;
    int toRead = fsize;
    cb.len = 0;
    while (toRead >= 0) {
        res = read(fd, cb.buffer + cb.len, toRead);
        if (res <= 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        cb.len += res;
        toRead -= res;
    }
    if (fstat(fd, &fileinfo)) {
        fprintf(stderr, "%s: readWholeFile: fstat failed, wat?!\n", errorContext);

        sfree(cb.buffer);
        cb.len = 0;
    }

    if (toRead < 0 || res < 0 || cb.len != fsize || (size_t) fileinfo.st_size != fsize) {
        fprintf(stderr, "%s: readWholeFile size mismatch! toRead %ld res %ld %s cb.len %ld fsize %ld fileinfo.st_size %ld\n",
                errorContext, (long) toRead, (long) res, strerror(res), (long) cb.len, (long) fsize, (long) fileinfo.st_size);

        sfree(cb.buffer);
        cb.len = 0;
    }
    return cb;
}
struct char_buffer readWholeGz(gzFile gzfp, char *errorContext) {
    struct char_buffer cb = {0};
    if (gzbuffer(gzfp, GZBUFFER_BIG) < 0) {
        fprintf(stderr, "reading %s: gzbuffer fail!\n", errorContext);
        return cb;
    }
    int alloc = 8 * 1024 * 1024;
    cb.buffer = cmalloc(alloc);
    if (!cb.buffer) {
        fprintf(stderr, "reading %s: readWholeGz alloc fail!\n", errorContext);
        return cb;
    }
    int res;
    int toRead = alloc;
    while (true) {
        res = gzread(gzfp, cb.buffer + cb.len, toRead);
        if (res <= 0)
            break;
        cb.len += res;
        toRead -= res;
        if (toRead == 0) {
            toRead = alloc;
            alloc += toRead;
            char *oldBuffer = cb.buffer;
            cb.buffer = realloc(cb.buffer, alloc);
            if (!cb.buffer) {
                sfree(oldBuffer);
                fprintf(stderr, "reading %s: readWholeGz alloc fail!\n", errorContext);
                return (struct char_buffer) {0};
            }
        }
    }
    if (res < 0) {
        sfree(cb.buffer);
        int error;
        fprintf(stderr, "readWholeGz: gzread failed: %s (res == %d)\n", gzerror(gzfp, &error), res);
        if (error == Z_ERRNO)
            perror(errorContext);
        return (struct char_buffer) {0};
    }

    return cb;
}

// wrapper to write to an opened gzFile
int writeGz(gzFile gzfp, void *source, int toWrite, char *errorContext) {
    int res, error;
    int nwritten = 0;
    char *p = source;
    if (!gzfp) {
        fprintf(stderr, "writeGz: gzfp was NULL .............\n");
        return -1;
    }
    while (toWrite > 0) {
        int len = toWrite;
        //if (len > 8 * 1024 * 1024)
        //    len = 8 * 1024 * 1024;
        res = gzwrite(gzfp, p, len);
        if (res <= 0) {
            fprintf(stderr, "gzwrite of length %d failed: %s (res == %d)\n", toWrite, gzerror(gzfp, &error), res);
            if (error == Z_ERRNO)
                perror(errorContext);
            return -1;
        }
        p += res;
        nwritten += res;
        toWrite -= res;
    }
    return nwritten;
}

void printTimestamp(FILE *stream, int64_t time_ms) {
    char timebuf[128];
    char timebuf2[128];
    time_t now;
    struct tm local;

    now = floor(time_ms / 1000.0);
    localtime_r(&now, &local);
    strftime(timebuf, 128, "%Y-%m-%d %T", &local);
    timebuf[127] = 0;
    strftime(timebuf2, 128, "%Z", &local);
    timebuf2[127] = 0;
    fprintf(stream, "[%s.%03d %s] ", timebuf, (int) (time_ms % 1000), timebuf2);
}

void log_with_timestamp(const char *format, ...) {
    char msg[1024];
    va_list ap;

    va_start(ap, format);
    vsnprintf(msg, 1024, format, ap);
    va_end(ap);
    msg[1023] = 0;

    printTimestamp(stderr, mstime());
    fprintf(stderr, "%s\n", msg);
}

int64_t roundSeconds(int interval, int offset, int64_t epoch_ms) {
    if (offset >= interval)
        fprintf(stderr, "roundSeconds was used wrong, interval must be greater than offset\n");
    time_t epoch = epoch_ms / SECONDS + (epoch_ms % SECONDS >= SECONDS / 2);
    struct tm utc;
    gmtime_r(&epoch, &utc);
    int sec = utc.tm_sec;
    int step = nearbyint((sec - offset) / (float) interval);
    int calc = offset + step * interval;
    //fprintf(stderr, "%d %d\n", sec, calc);
    return (epoch + (calc - sec)) * SECONDS;
}
ssize_t check_write(int fd, const void *buf, size_t count, const char *error_context) {
    ssize_t res = write(fd, buf, count);
    if (res < 0)
        perror(error_context);
    else if (res != (ssize_t) count)
        fprintf(stderr, "%s: Only %zd of %zd bytes written!\n", error_context, res, count);
    return res;
}

int my_epoll_create(int *event_fd_ptr) {
    int fd = epoll_create(32); // argument positive, ignored
    if (fd == -1) {
        perror("FATAL: epoll_create() failed:");
        exit(1);
    }
    // add exit signaling eventfd, we want that for all our epoll fds
    struct epoll_event epollEvent = { .events = EPOLLIN, .data = { .ptr = event_fd_ptr }};
    if (epoll_ctl(fd, EPOLL_CTL_ADD, *event_fd_ptr, &epollEvent)) {
        perror("epoll_ctl fail:");
        exit(1);
    }
    return fd;
}
void epollAllocEvents(struct epoll_event **events, int *maxEvents) {
    if (!*events) {
        *maxEvents = 32;
    } else if (*maxEvents > 9000) {
        return;
    } else {
        *maxEvents *= 2;
    }

    sfree(*events);
    *events = cmalloc(*maxEvents * sizeof(struct epoll_event));

    if (!*events) {
        fprintf(stderr, "Fatal: epollAllocEvents malloc\n");
        exit(1);
    }
}
char *sprint_uuid1_partial(uint64_t id1, char *p) {
    for (int i = 7; i >= 0; i--) {
        //int j = 7 - i;
        //if (j == 4)
        //*p++ = '-';
        uint64_t val = (id1 >> (4 * i)) & 15;
        if (val > 9)
            *p++ = val - 10 + 'a';
        else
            *p++ = val + '0';
    }
    *p = '\0';
    return p;
}
char *sprint_uuid1(uint64_t id1, char *p) {
    for (int i = 15; i >= 0; i--) {
        int j = 15 - i;
        if (j == 8 || j == 12)
            *p++ = '-';
        uint64_t val = (id1 >> (4 * i)) & 15;
        if (val > 9)
            *p++ = val - 10 + 'a';
        else
            *p++ = val + '0';
    }
    *p = '\0';
    return p;
}
char *sprint_uuid2(uint64_t id2, char *p) {
    for (int i = 15; i >= 0; i--) {
        int j = 15 - i;
        if (j == 0 || j == 4)
            *p++ = '-';
        uint64_t val = (id2 >> (4 * i)) & 15;
        if (val > 9)
            *p++ = val - 10 + 'a';
        else
            *p++ = val + '0';
    }
    *p = '\0';
    return p;
}
char *sprint_uuid(uint64_t id1, uint64_t id2, char *p) {
    p = sprint_uuid1(id1, p);
    p = sprint_uuid2(id2, p);
    *p = '\0';
    return p;
}

int mkdir_error(const char *path, mode_t mode, FILE *err_stream) {
    int err = mkdir(path, mode);
    if (err != 0 && errno != EEXIST && err_stream) {
        fprintf(err_stream, "mkdir: %s (%s)\n", strerror(errno), path);
    }
    return err;
}

// Distance between points on a spherical earth.
// This has up to 0.5% error because the earth isn't actually spherical
// (but we don't use it in situations where that matters)

// define for testing some approximations:
#define DEGR (0.017453292519943295) // 1 degree in radian
double greatcircle(double lat0, double lon0, double lat1, double lon1, int approx) {
    if (lat0 == lat1 && lon0 == lon1) {
        return 0;
    }

    // toRad converts degrees to radians
    lat0 = toRad(lat0);
    lon0 = toRad(lon0);
    lat1 = toRad(lat1);
    lon1 = toRad(lon1);

    double dlat = fabs(lat1 - lat0);
    double dlon = fabs(lon1 - lon0);

    double hav = 0;
    if (CHECK_APPROXIMATIONS) {
        double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat0) * cos(lat1) * sin(dlon / 2) * sin(dlon / 2);
        hav = 6371e3 * 2 * atan2(sqrt(a), sqrt(1.0 - a));
    }
    // after checking this isn't necessary with doubles
    // anyhow for small distance we can do a much cheaper approximation:
    // anyhow, nice formular let's leave it in the code for reference

    // for small distances the earth is flat enough that we can use this approximation
    // don't use this approximation near the poles, would probably behave poorly
    //
    // in our particular case many calls of this function are by speed_check which usually is small distances
    // thus having less trigonometric functions used should be a performance gain
    //
    // difference to haversine is less than 0.04 percent for up to 3 degrees of lat/lon difference
    // this isn't an issue for us and due to the oblateness and this calculation taking it into account, this calculation might actually be more accurate for small distances but i can't be bothered to check.
    //
    if (approx || (dlat < 3 * DEGR && dlon < 3 * DEGR && fabs(lat1) < 80 * DEGR)) {
        // calculate the equivalent length of the latitude and longitude difference
        // use pythagoras to get the distance

        // Equatorial radius: e = (6378.1370 km) -> circumference: 2 * pi * e = 40 075.016 km
        // Polar radius: p = (6356.7523 km) -> quarter meridian from wiki: 10 001.965 km
        // float ec = 40075016; // equatorial circumerence
        // float mc = 4 * 10001965; // meridial circumference


        // to have consistency to other calculations, use a circular earth
        float ec = 2 * M_PI * 6371e3; // equatorial circumference
        float mc = 2 * M_PI * 6371e3; // meridial circumference

        float avglat = lat0 + (lat1 - lat0) / 2;
        float dmer = (float) dlat / (2 * (float) M_PI) * mc;
        float dequ = (float) dlon / (2 * (float) M_PI) * ec * cosf(avglat);
        float pyth = sqrtf(dmer * dmer + dequ * dequ);

        if (!approx && CHECK_APPROXIMATIONS) {
            double errorPercent = fabs(hav - pyth) / hav * 100;
            if (errorPercent > 0.03) {
                fprintf(stderr, "pos: %.1f, %.1f dlat: %.5f dlon %.5f hav: %.1f errorPercent: %.3f\n", toDeg(lat0), toDeg(lon0), toDeg(dlat), toDeg(dlon), hav, errorPercent);
            }
        }

        return pyth;
    }

    // spherical law of cosines
    // use float calculations if latitudes differ sufficiently
    if (dlat > 1 * DEGR && dlon > 1 * DEGR) {
        // error
        double slocf =  6371e3f * acosf(sinf(lat0) * sinf(lat1) + cosf(lat0) * cosf(lat1) * cosf(dlon));
        if (CHECK_APPROXIMATIONS) {
            double errorPercent = fabs(hav - slocf) / hav * 100;
            if (errorPercent > 0.025) {
                fprintf(stderr, "pos: %.1f, %.1f dlat: %.5f dlon %.5f hav: %.1f errorPercent: %.3f\n", toDeg(lat0), toDeg(lon0), toDeg(dlat), toDeg(dlon), hav, errorPercent);
            }
        }
        return slocf;
    }

    double sloc =  6371e3 * acos(sin(lat0) * sin(lat1) + cos(lat0) * cos(lat1) * cos(dlon));

    if (CHECK_APPROXIMATIONS) {
        double errorPercent = fabs(hav - sloc) / hav * 100;
        if (errorPercent > 0.025) {
            fprintf(stderr, "pos: %.1f, %.1f dlat: %.5f dlon %.5f sloc: %.1f errorPercent: %.3f\n", toDeg(lat0), toDeg(lon0), toDeg(dlat), toDeg(dlon), sloc, errorPercent);
        }
    }

    return sloc;
}

double bearing(double lat0, double lon0, double lat1, double lon1) {
    lat0 = toRad(lat0);
    lon0 = toRad(lon0);
    lat1 = toRad(lat1);
    lon1 = toRad(lon1);

    // using float variants except for sin close to zero

    float y = sinf(lon1-lon0)*cosf(lat1);
    float x = cosf(lat0)*sinf(lat1) - sinf(lat0)*cosf(lat1)*cosf(lon1-lon0);
    float res = atan2f(y, x) * (180 / (float) M_PI) + 360;

    if (CHECK_APPROXIMATIONS) {
        // check against using double trigonometric functions
        // errors greater than 0.5 are rare and only happen for small distances
        // bearings derived from small distances don't need to be accurate at all for our purposes
        double y = sin(lon1-lon0)*cos(lat1);
        double x = cos(lat0)*sin(lat1) - sin(lat0)*cos(lat1)*cos(lon1-lon0);
        double res2 = (atan2(y, x) * (180 / M_PI) + 360);
        double diff = fabs(res2 - res);
        double dist = greatcircle(toDeg(lat0), toDeg(lon0), toDeg(lat1), toDeg(lon1), 1);
        if ((diff > 0.2 && dist > 150) || diff > 2) {
            fprintf(stderr, "errorDeg: %.2f %.2f %.2f dist: %.2f km\n",
                    diff, res, res2, dist / 1000.0);
        }
    }
    while (res > 360)
        res -= 360;
    return res;
}

#undef DEGR



// allocate a group of task_info
task_group_t *allocate_task_group(uint32_t count) {
    task_group_t *group = cmalloc(sizeof(task_group_t));
    group->task_count = count;
    group->infos = cmalloc(count * sizeof(task_info_t));
    memset(group->infos, 0x0, count * sizeof(task_info_t));
    /*
    for (uint32_t k = 0; k < count; k++) {
        task_info_t *info = &group->infos[k];
        info->buffer_count = buffer_count;
        info->buffers = cmalloc(buffer_count * sizeof(buffer_t));
        memset(info->buffers, 0x0, buffer_count * sizeof(buffer_t));
    }
    */
    group->tasks = cmalloc(count * sizeof(threadpool_task_t));
    memset(group->tasks, 0x0, count * sizeof(threadpool_task_t));

    return group;
}

// destroy a group of task_info
void destroy_task_group(task_group_t *group) {
    /*
    for (uint32_t k = 0; k < group->task_count; k++) {
        task_info_t *info = &group->infos[k];
        for (uint32_t j = 0; j < info->buffer_count; j++) {
            free(info->buffers[j].buf);
        }
        free(info->buffers);
    }
    */

    free(group->infos);
    free(group->tasks);

    memset(group, 0x0, sizeof(task_group_t));
    free(group);
}

void threadpool_distribute_and_run(threadpool_t *pool, task_group_t *task_group, threadpool_function_t func, int totalRange, int taskCount, int64_t now) {

    if (taskCount == 0 || taskCount > (int) task_group->task_count) {
        taskCount = task_group->task_count;
    }

    threadpool_task_t *tasks = task_group->tasks;
    task_info_t *infos = task_group->infos;

    int section_len = totalRange / taskCount;
    int extra = totalRange % taskCount;

    int p = 0;

    int actualTaskCount = 0;
    // assign tasks
    for (int i = 0; i < taskCount; i++) {
        threadpool_task_t *task = &tasks[i];
        task_info_t *range = &infos[i];

        range->now = now;
        range->from = p;
        p += section_len;
        if (extra) {
            p++;
            extra--;
        }
        range->to = p;

        if (range->from == range->to) {
            break;
        }

        task->function = func;
        task->argument = range;

        actualTaskCount++;
        //fprintf(stderr, "%d %d\n", range->from, range->to);
    }
    if (p != totalRange) {
        fprintf(stderr, "threadpool_distribute_and_run: range distribution error: p: %d totalRange: %d\n", p, totalRange);
    }

    threadpool_run(pool, tasks, actualTaskCount);
}


void gzipFile(char *filename) {
    int fd;
    char fileGz[PATH_MAX];
    gzFile gzfp;

    // read uncompressed file into buffer
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return;
    }
    struct char_buffer cb = readWholeFile(fd, filename);
    close(fd);
    if (!cb.buffer) {
        fprintf(stderr, "gzipFile readWholeFile failed: %s\n", filename);
        return;
    }

    snprintf(fileGz, PATH_MAX, "%s.gz", filename);
    gzfp = gzopen(fileGz, "wb");
    if (!gzfp) {
        fprintf(stderr, "gzopen failed:");
        perror(fileGz);
        return;
    }
    int res = gzsetparams(gzfp, 9, Z_DEFAULT_STRATEGY);
    if (res < 0) {
        fprintf(stderr, "gzsetparams fail: %d", res);
    }

    if (cb.len > 0) {
        writeGz(gzfp, cb.buffer, cb.len, fileGz);
    }

    sfree(cb.buffer);
    cb.len = 0;

    if (gzclose(gzfp) != Z_OK) {
        fprintf(stderr, "compressACAS gzclose failed: %s\n", fileGz);
        unlink(fileGz);
        return;
    }
}

void check_grow_buffer_t(buffer_t *buffer, ssize_t newSize) {
    if (buffer->bufSize < newSize) {
        sfree(buffer->buf);
        buffer->buf = cmalloc(newSize);
    }
}

void *check_grow_threadpool_buffer_t(threadpool_buffer_t *buffer, ssize_t newSize) {
    if (buffer->size < newSize || !buffer->buf) {
        //fprintf(stderr, "check_grow_threadpool_buffer: buffer->size %ld requested size %ld\n", (long) buffer->size, (long) newSize);
        sfree(buffer->buf);
        newSize = newSize * 9 / 8; // avoid super many mallocs when the size of something grows slowly
        buffer->buf = cmalloc(newSize);
        if (!buffer->buf) {
            fprintf(stderr, "<3>FATAL: check_grow_threadpool_buffer_t no enough memory allocating %ld bytes!\n", (long) newSize);
            abort();
        }
        buffer->size = newSize;
    }
    return buffer->buf;
}


struct char_buffer generateZstd(ZSTD_CCtx* cctx, threadpool_buffer_t *pbuffer, struct char_buffer src, int level) {
    struct char_buffer cb;

    check_grow_threadpool_buffer_t(pbuffer, ZSTD_compressBound(src.len));

    //fprintf(stderr, "pbuffer->size: %ld src.len %ld\n", (long) pbuffer->size, (long) src.len);

    /*
     * size_t ZSTD_compressCCtx(ZSTD_CCtx* cctx,
                                void* dst, size_t dstCapacity,
                                const void* src, size_t srcSize,
                                int compressionLevel);
     */

    size_t compressedSize = ZSTD_compressCCtx(cctx,
            pbuffer->buf, pbuffer->size,
            src.buffer, src.len,
            level);

    if (ZSTD_isError(compressedSize)) {
        fprintf(stderr, "generateZstd() zstd error: %s\n", ZSTD_getErrorName(compressedSize));
        cb.buffer = NULL;
        cb.len = 0;
        return cb;
    }

    cb.len = compressedSize;
    cb.buffer = pbuffer->buf;
    return cb;
}


struct char_buffer ident(struct char_buffer target) {
    return target;
}

void setLowestPriorityPthread() {
#ifndef __linux__
    return;
#endif

    //fprintf(stderr, "priority before: %d\n", (int) getpriority(PRIO_PROCESS, 0));
    setpriority(PRIO_PROCESS, 0, 10 + getpriority(PRIO_PROCESS, 0));
    //fprintf(stderr, "priority after: %d\n", (int) getpriority(PRIO_PROCESS, 0));

    return;

    int policy;
    struct sched_param param = { 0 };

    pthread_getschedparam(pthread_self(), &policy, &param);
    fprintf(stderr, "priority before: %d\n", (int) param.sched_priority);

    policy=SCHED_FIFO;
    int priority_max = sched_get_priority_max(policy);
    int priority_min = sched_get_priority_min(policy);
    fprintf(stderr, "min prio: %d max prio: %d\n", priority_min, priority_max);

    param.sched_priority = priority_min;

    pthread_setschedparam(pthread_self(), policy, &param);

    pthread_getschedparam(pthread_self(), &policy, &param);
    fprintf(stderr, "priority after: %d\n", (int) param.sched_priority);
}

void setPriorityPthread() {
#ifndef __linux__
    return;
#endif

    setpriority(PRIO_PROCESS, 0, -5 + getpriority(PRIO_PROCESS, 0));

    int policy = SCHED_FIFO;
    struct sched_param param = { 0 };

    param.sched_priority = sched_get_priority_min(policy);

    pthread_setschedparam(pthread_self(), policy, &param);
}



zstd_fw_t *createZstdFw(size_t inBufSize) {
    zstd_fw_t *fw = cmalloc(sizeof(zstd_fw_t));
    memset(fw, 0x0, sizeof(zstd_fw_t));

    fw->in.src = cmalloc(inBufSize);
    fw->inAlloc = inBufSize;
    fw->in.size = 0;
    fw->in.pos = 0;

    int outBufSize = ZSTD_compressBound(inBufSize);
    fw->out.dst = cmalloc(outBufSize);
    fw->out.size = outBufSize;
    fw->out.pos = 0;

    //fw->cctx = ZSTD_createCCtx();
    fw->cstream = ZSTD_createCStream();
    fw->fd = -1;

    return fw;
}

void destroyZstdFw(zstd_fw_t *fw) {

    //ZSTD_freeCCtx(fw->cctx);
    ZSTD_freeCStream(fw->cstream);

    free((void *) fw->in.src);
    free((void *) fw->out.dst);
    free(fw);
}

static size_t zstdFwAvailable(zstd_fw_t *fw) {
    return fw->inAlloc - fw->in.size;
}
static void zstdFwWrite(zstd_fw_t *fw) {
    if (fw->fd < 0) {
        return;
    }
    check_write(fw->fd, fw->out.dst, fw->out.pos, fw->outFile);
    fw->out.pos = 0;
}

static void zstdFwCompress(zstd_fw_t *fw) {
    if (fw->in.size == 0) {
        return;
    }
    if (fw->fd < 0) {
        return;
    }
    size_t res;
    // fw->in buffer is full, let's compress it
    //res = ZSTD_compressStream2(fw->cctx, &fw->out, &fw->in, ZSTD_e_flush);
    res = ZSTD_compressStream(fw->cstream, &fw->out, &fw->in);
    if (ZSTD_isError(res)) {
        fprintf(stderr, "ZSTD_compressStream failed: %ld %s\n", (long) res, ZSTD_getErrorName(res));
    }
    /*
    res = ZSTD_flushStream(fw->cstream, &fw->out);
    if (ZSTD_isError(res)) {
        fprintf(stderr, "ZSTD_flushStream failed: %s\n", ZSTD_getErrorName(res));
    }
    */

    if (fw->in.size != fw->in.pos) {
        fprintf(stderr, "<3>BAD: ohB6ooVi %ld %ld %ld\n", (long) fw->in.size, (long) fw->in.pos, (long) res);
    }
    fw->in.size = 0;
    fw->in.pos = 0;

    zstdFwWrite(fw);
}

void zstdFwStartFile(zstd_fw_t *fw, const char *outFile, int compressionLvl) {
    fw->in.pos = 0;
    fw->in.size = 0;
    fw->out.pos = 0;

    size_t res;

    //ZSTD_CCtx_reset(fw->cctx, ZSTD_reset_session_and_parameters);
    //ZSTD_CCtx_setParameter(fw->cctx, ZSTD_c_compressionLevel, compressionLvl);

    res = ZSTD_initCStream(fw->cstream, compressionLvl);
    if (ZSTD_isError(res)) {
        fprintf(stderr, "ZSTD_initCStream failed: %s\n", ZSTD_getErrorName(res));
    }

    fw->outFile = outFile;
    if (!fw->outFile) {
        fprintf(stderr, "zstdFwStartFile(): outFile null!\n");
    }

    fw->fd = open(fw->outFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fw->fd < 0) {
        fprintf(stderr, "zstdFwStartFile(): open failed: %s\n", strerror(errno));
    }
}

void zstdFwFinishFile(zstd_fw_t *fw) {
    zstdFwCompress(fw);
    //size_t res = ZSTD_compressStream2(fw->cctx, &fw->out, &fw->in, ZSTD_e_end);
    size_t res = ZSTD_endStream(fw->cstream, &fw->out);
    if (res != 0) {
        fprintf(stderr, "ZSTD_endStream failed: %ld %s\n", (long) res, ZSTD_getErrorName(res));
    }
    zstdFwWrite(fw);
    close(fw->fd);
}

void zstdFwPutData(zstd_fw_t *fw, const uint8_t *data, size_t len) {
    if (fw->fd < 0) {
        return;
    }
    size_t remaining = len;
    const uint8_t *p = data;
    while (remaining > 0) {
        if (zstdFwAvailable(fw) == 0) {
            zstdFwCompress(fw);
        }
        size_t bytes = imin(zstdFwAvailable(fw), remaining);
        memcpy((char *)(fw->in.src + fw->in.size), p, bytes);
        fw->in.size += bytes;
        remaining -= bytes;
        p += bytes;
    }
}


void dump_beast_check(int64_t now) {
    if (!Modes.dump_fw) {
        return;
    }
    int32_t index = now / (Modes.dump_interval * SECONDS);

    if (Modes.dump_beast_index == index) {
        return;
    }

    // finish old file
    zstd_fw_t *fw = Modes.dump_fw;

    if (fw->fd >= 0) {
        zstdFwFinishFile(fw);
    }


    int startup = (Modes.dump_beast_index < 0);
    Modes.dump_beast_index = index;

    time_t nowish = index * Modes.dump_interval;
    struct tm utc;
    gmtime_r(&nowish, &utc);

    char tstring[100];
    strftime (tstring, 100, "%H%M%S", &utc);


    char pathbuf[PATH_MAX];
    snprintf(pathbuf, PATH_MAX, "%s/%sZ.zst", Modes.dump_beast_dir, tstring);

    // unless we just restarted, delete the file
    if (!startup) {
        unlink(pathbuf);
    }

    // start new file
    zstdFwStartFile(fw, pathbuf, 4);

    //fprintf(stderr, "dump_beast started file: %s\n", pathbuf);
}



// get the first <maxTokens> tokens from a string separated by any bytes in <delim> and place them in the provided char pointer array tokens
// the array of token pointers is set to NULL before populating it
// stringp / delim work just like strsep(3), this is just a wrapper to easily extract multiple tokens from a string
// the pointer pointed at by stringp WILL NOT be modified
// the string pointed at by stringp WILL be modified
int32_t tokenize(char **restrict stringp, char *restrict delim, char **restrict tokens, int maxTokens) {
    memset(tokens, 0x0, sizeof(char *) * maxTokens);
    int32_t k = 0;
    char *p = *stringp;
    while (k < maxTokens) {
        tokens[k] = strsep(&p, delim);
        if (!tokens[k]) {
            break;
        }
        k++;
    }
    return k;
}

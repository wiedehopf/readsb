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

#include <stdlib.h>
#include <sys/time.h>

uint64_t mstime(void) {
    struct timeval tv;
    uint64_t mst;

    gettimeofday(&tv, NULL);
    mst = ((uint64_t) tv.tv_sec)*1000;
    mst += tv.tv_usec / 1000;
    return mst;
}

uint64_t msThreadTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ((uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000));
}

int64_t receiveclock_ns_elapsed(uint64_t t1, uint64_t t2) {
    return (t2 - t1) * 1000U / 12U;
}

int64_t receiveclock_ms_elapsed(uint64_t t1, uint64_t t2) {
    return (t2 - t1) / 12000U;
}

void normalize_timespec(struct timespec *ts) {
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec += ts->tv_nsec / 1000000000;
        ts->tv_nsec = ts->tv_nsec % 1000000000;
    } else if (ts->tv_nsec < 0) {
        long adjust = ts->tv_nsec / 1000000000 + 1;
        ts->tv_sec -= adjust;
        ts->tv_nsec = (ts->tv_nsec + 1000000000 * adjust) % 1000000000;
    }
}

// convert ms to timespec
struct timespec msToTimespec(uint64_t ms)  {
    struct timespec ts;
    ts.tv_sec =  (ms / 1000);
    ts.tv_nsec = (ms % 1000) * 1000 * 1000;
    return ts;
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
int64_t stopWatch(const struct timespec *start_time) {
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    return ((int64_t) end_time.tv_sec * 1000UL + end_time.tv_nsec / 1000000UL)
            - ((int64_t) start_time->tv_sec * 1000UL + start_time->tv_nsec / 1000000UL);
}

// this is not cryptographic but much better than mstime() as a seed
unsigned int get_seed() {
    struct timespec time;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return (time.tv_sec ^ time.tv_nsec ^ (getpid() << 16) ^ pthread_self());
}

// increment target by increment in ms, if result is in the past, set target to now.
// specialized function for scheduling threads using pthreadcondtimedwait
void incTimedwait(struct timespec *target, uint64_t increment) {
    struct timespec inc = msToTimespec(increment);
    target->tv_sec += inc.tv_sec;
    target->tv_nsec += inc.tv_nsec;
    normalize_timespec(target);
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    if (target->tv_sec < now.tv_sec || (target->tv_sec == now.tv_sec && target->tv_nsec < now.tv_nsec)) {
        target->tv_sec = now.tv_sec;
        target->tv_nsec = now.tv_nsec;
    }
}

struct char_buffer readWholeFile(int fd, char *errorContext) {
    struct char_buffer cb = {0};
    struct stat fileinfo = {0};
    if (fstat(fd, &fileinfo)) {
        fprintf(stderr, "%s: readWholeFile: fstat failed, wat?!\n", errorContext);
        return cb;
    }
    size_t fsize = fileinfo.st_size;
    if (fsize == 0)
        return cb;

    cb.buffer = malloc(fsize + 1);
    if (!cb.buffer) {
        fprintf(stderr, "%s: readWholeFile couldn't allocate buffer!\n", errorContext);
        return cb;
    }
    int res;
    int toRead = fsize;
    while (true) {
        res = read(fd, cb.buffer + cb.len, toRead);
        if (res == EINTR)
            continue;
        if (res <= 0)
            break;
        cb.len += res;
        toRead -= res;
    }
    cb.buffer[fsize] = '\0'; // for good measure put a null byte to terminate the string. (consumers should honor cb.len)
    if (res < 0 || cb.len != fsize) {
        free(cb.buffer);
        cb = (struct char_buffer) {0};
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
    cb.buffer = malloc(alloc);
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
                free(oldBuffer);
                fprintf(stderr, "reading %s: readWholeGz alloc fail!\n", errorContext);
                return (struct char_buffer) {0};
            }
        }
    }
    if (res < 0) {
        free(cb.buffer);
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

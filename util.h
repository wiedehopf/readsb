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

#define CHECK_APPROXIMATIONS (0)

#define GZBUFFER_BIG (1 * 1024 * 1024)

#include <stdint.h>

#define sfree(x) do { free(x); x = NULL; } while (0)

#define HOURS (60*60*1000LL)
#define MINUTES (60*1000LL)
#define SECONDS (1000LL)
#define MS (1LL)


#define litLen(literal) (sizeof(literal) - 1)
// return true for byte match between string and string literal. string IS allowed to be longer than literal
#define byteMatchStart(s1, literal) (memcmp(s1, literal, litLen(literal)) == 0)
// return true for byte match between string and string literal. string IS NOT allowed to be longer than literal
#define byteMatchStrict(s1, literal) (memcmp(s1, literal, sizeof(literal)) == 0)

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
int threadAffinity(int core_id);

struct char_buffer {
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
// microseconds
int64_t microtime(void);

void milli_micro_seconds(int64_t *milli, int64_t *micro);
int64_t mono_micro_seconds();
int64_t mono_milli_seconds();
int64_t getUptime();

int snprintHMS(char *buf, size_t bufsize, int64_t now);

int64_t msThreadTime(void);
int64_t nsThreadTime(void);

/* Returns the time elapsed, in nanoseconds, from t1 to t2,
 * where t1 and t2 are 12MHz counters.
 */
int64_t receiveclock_ns_elapsed (int64_t t1, int64_t t2);

/* Same, in milliseconds */
int64_t receiveclock_ms_elapsed (int64_t t1, int64_t t2);

/* Normalize the value in ts so that ts->nsec lies in
 * [0,999999999]
 */

static inline void normalize_timespec(struct timespec *ts) {
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
static inline struct timespec msToTimespec(int64_t ms)  {
    struct timespec ts;
    ts.tv_sec =  (ms / 1000);
    ts.tv_nsec = (ms % 1000) * 1000 * 1000;
    return ts;
}

/* record current CPU time in start_time */
void start_cpu_timing (struct timespec *start_time);

/* add difference between start_time and the current CPU time to add_to */
void end_cpu_timing (const struct timespec *start_time, struct timespec *add_to);

// given a start and end time, add the difference to the third timespec
void timespec_add_elapsed(const struct timespec *start_time, const struct timespec *end_time, struct timespec *add_to);

void start_monotonic_timing(struct timespec *start_time);
void end_monotonic_timing (const struct timespec *start_time, struct timespec *add_to);

// start watch for stopWatch
void startWatch(struct timespec *start_time);
// return elapsed time
int64_t stopWatch(struct timespec *start_time);
// return elapsed time and set start_time to current time
int64_t lapWatch(struct timespec *start_time);

// get nanoseconds and some other stuff for use with srand
unsigned int get_seed();

void log_with_timestamp(const char *format, ...) __attribute__ ((format(printf, 1, 2)));

// based on a give epoch time in ms, calculate the nearest offset interval step
// offset must be smaller than interval, at offset seconds after the full minute
// is the first possible value, all additional return values differ by a multiple
// of interval
int64_t roundSeconds(int interval, int offset, int64_t epoch_ms);
ssize_t check_write(int fd, const void *buf, size_t count, const char *error_context);

int my_epoll_create(int *event_fd_ptr);
void epollAllocEvents(struct epoll_event **events, int *maxEvents);

char *sprint_uuid(uint64_t id1, uint64_t id2, char *p);
char *sprint_uuid1_partial(uint64_t id1, char *p);
char *sprint_uuid1(uint64_t id1, char *p);
char *sprint_uuid2(uint64_t id2, char *p);

int mkdir_error(const char *path, mode_t mode, FILE *err_stream);

double greatcircle(double lat0, double lon0, double lat1, double lon1, int approx);
double bearing(double lat0, double lon0, double lat1, double lon1);

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

static inline void fprintTimePrecise(FILE *stream, int64_t now) {
    fprintf(stream, "%02d:%02d:%06.3f",
            (int) ((now / (3600 * SECONDS)) % 24),
            (int) ((now / (60 * SECONDS)) % 60),
            (now % (60 * SECONDS)) / 1000.0);
}
static inline void fprintTime(FILE *stream, int64_t now) {
    fprintf(stream, "%02d:%02d:%04.1f",
            (int) ((now / (3600 * SECONDS)) % 24),
            (int) ((now / (60 * SECONDS)) % 60),
            (now % (60 * SECONDS)) / 1000.0);
}

typedef struct {
    void *buf;
    ssize_t bufSize;
} buffer_t;

typedef struct {
    int64_t now;
    int32_t from;
    int32_t to;

} task_info_t;


typedef struct {
    uint32_t task_count;
    task_info_t *infos;
    threadpool_task_t *tasks;
} task_group_t;

// allocate a group of tasks
task_group_t *allocate_task_group(uint32_t count);
// destroy a group of tasks
void destroy_task_group(task_group_t *group);

void threadpool_distribute_and_run(threadpool_t *pool, task_group_t *task_group, threadpool_function_t func, int totalRange, int taskCount, int64_t now);

void check_grow_buffer_t(buffer_t *buffer, ssize_t newSize);
void *check_grow_threadpool_buffer_t(threadpool_buffer_t *buffer, ssize_t newSize);

void gzipFile(char *file);

struct char_buffer generateZstd(ZSTD_CCtx* cctx, threadpool_buffer_t *pbuffer, struct char_buffer src, int level);
struct char_buffer ident(struct char_buffer target);

void setLowestPriorityPthread();
void setPriorityPthread();

typedef struct {
    //ZSTD_CCtx *cctx;
    ZSTD_CStream *cstream;
    ZSTD_inBuffer in;
    size_t inAlloc;
    ZSTD_outBuffer out;
    const char *outFile;
    int fd;
} zstd_fw_t;


zstd_fw_t *createZstdFw(size_t inBufSize);
void destroyZstdFw(zstd_fw_t *fw);

void zstdFwStartFile(zstd_fw_t *fw, const char *outFile, int compressionLvl);
void zstdFwFinishFile(zstd_fw_t *fw);

void zstdFwPutData(zstd_fw_t *fw, const uint8_t *data, size_t len);

void dump_beast_check(int64_t now);

int32_t tokenize(char **restrict stringp, char *restrict delim, char **restrict tokens, int maxTokens);

#endif

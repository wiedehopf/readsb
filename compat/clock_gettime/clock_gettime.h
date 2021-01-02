#ifndef CLOCK_GETTIME_H
#define CLOCK_GETTIME_H

#include <mach/mach_time.h> // Apple-only, but this isn't inclued on other BSDs

#ifdef _CLOCKID_T_DEFINED_
#define CLOCKID_T
#endif

#ifndef CLOCKID_T
#define CLOCKID_T
typedef enum
{
    CLOCK_REALTIME,
    CLOCK_MONOTONIC,
    CLOCK_PROCESS_CPUTIME_ID,
    CLOCK_THREAD_CPUTIME_ID
} clockid_t;
#endif // ifndef CLOCKID_T

struct timespec;

static mach_timebase_info_data_t __clock_gettime_inf;

int clock_gettime(clockid_t clk_id, struct timespec *tp);

#endif // CLOCK_GETTIME_H

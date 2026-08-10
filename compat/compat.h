#ifndef COMPAT_UTIL_H
#define COMPAT_UTIL_H

/*
 * Platform-specific bits
 */

#if defined(__APPLE__)

#define NO_EVENT_FD

/*
 * Mach endian conversion
 */
# include <libkern/OSByteOrder.h>
# define bswap_16 OSSwapInt16
# define bswap_32 OSSwapInt32
# define bswap_64 OSSwapInt64
# include <machine/endian.h>
# define le16toh(x) OSSwapLittleToHostInt16(x)
# define le32toh(x) OSSwapLittleToHostInt32(x)
# define htole16(x) OSSwapHostToLittleInt16(x)
# define htole32(x) OSSwapHostToLittleInt32(x)

#include "apple/clock_compat.h"
#include "apple/compat.h"
#include "apple/cpu_compat.h"
#include "apple/epoll_shim.h"
#include "apple/net_compat.h"
#include "apple/sendfile_compat.h"
#include "apple/serial_compat.h"
#include "apple/stat_compat.h"
#include "apple/thread_compat.h"

#else // other platforms

# include <endian.h>

#endif

#ifdef MISSING_NANOSLEEP
#include "clock_nanosleep/clock_nanosleep.h"
#endif

#ifdef MISSING_GETTIME
#include "clock_gettime/clock_gettime.h"
#endif

#endif //COMPAT_UTIL_H

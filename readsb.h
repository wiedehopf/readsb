// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// readsb.h: main program header
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
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

#ifndef __DUMP1090_H
#define __DUMP1090_H

#ifndef ALL_JSON
#define ALL_JSON 0
#endif

// Default version number, if not overriden by the Makefile
#ifndef MODES_READSB_VERSION
#define MODES_READSB_VERSION     "Unknown"
#endif

#ifndef MODES_READSB_VARIANT
#define MODES_READSB_VARIANT     "readsb"
#endif

#define VERSION_STRING MODES_READSB_VARIANT " version: " MODES_READSB_VERSION

// ============================= Include files ==========================

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>
#include <zlib.h>
#include <inttypes.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "minilzo/minilzo.h"
#include "threadpool.h"
#include <stdatomic.h>
#include <zstd.h>
#include <sys/mman.h>


#include "compat/compat.h"

// ============================= #defines ===============================

#define MODES_DEFAULT_FREQ      1090000000
#define MODES_RTL_BUFFERS       16                         // Number of RTL buffers
#define MODES_MAG_BUFFERS       12                         // Number of magnitude buffers (should be smaller than RTL_BUFFERS for flowcontrol to work)
#define MODES_AUTO_GAIN         -100                       // Use automatic gain
#define MODES_MAX_GAIN          999999                     // Use max available gain
#define MODEAC_MSG_BYTES        2

#define MODES_PREAMBLE_US       8   // microseconds = bits
#define MODES_PREAMBLE_SAMPLES  (MODES_PREAMBLE_US       * 2)
#define MODES_PREAMBLE_SIZE     (MODES_PREAMBLE_SAMPLES  * sizeof(uint16_t))
#define MODES_LONG_MSG_BYTES    14
#define MODES_SHORT_MSG_BYTES   7
#define MODES_LONG_MSG_BITS     (MODES_LONG_MSG_BYTES    * 8)
#define MODES_SHORT_MSG_BITS    (MODES_SHORT_MSG_BYTES   * 8)
#define MODES_LONG_MSG_SAMPLES  (MODES_LONG_MSG_BITS     * 2)
#define MODES_SHORT_MSG_SAMPLES (MODES_SHORT_MSG_BITS    * 2)
#define MODES_LONG_MSG_SIZE     (MODES_LONG_MSG_SAMPLES  * sizeof(uint16_t))
#define MODES_SHORT_MSG_SIZE    (MODES_SHORT_MSG_SAMPLES * sizeof(uint16_t))

#define MODES_OS_PREAMBLE_SAMPLES  (20)
#define MODES_OS_PREAMBLE_SIZE     (MODES_OS_PREAMBLE_SAMPLES  * sizeof(uint16_t))
#define MODES_OS_LONG_MSG_SAMPLES  (268)
#define MODES_OS_SHORT_MSG_SAMPLES (135)
#define MODES_OS_LONG_MSG_SIZE     (MODES_LONG_MSG_SAMPLES  * sizeof(uint16_t))
#define MODES_OS_SHORT_MSG_SIZE    (MODES_SHORT_MSG_SAMPLES * sizeof(uint16_t))

#define MODES_OUT_BUF_SIZE         (48*1024)
#define MODES_OUT_FLUSH_INTERVAL   (500) // max flush interval

// needs to be larger than OUT_BUF_SIZE above
#define MODES_NET_SNDBUF_SIZE (64*1024)
#define MODES_NET_SNDBUF_MAX  (7)

#define HEX_UNKNOWN (0xDEADBEEF)

#define DFTYPE_MODEAC 77

#define INVALID_ALTITUDE (-9999)

#define CANARY (0x665225ca79e653a3)


// size of various on stack buffers used across the code, let's just be conservative and assume 1 MB of stack
// without heavy recursion 3 of those stack buffers can be in use at the same time, at most we expect to to be in use
#define QUARTER_STACK (256 * 1024)

/* Where did a bit of data arrive from? In order of increasing priority */
typedef enum
{
    SOURCE_INVALID, /* data is not valid */
    SOURCE_INDIRECT, /* data is of unknown quality */
    SOURCE_MODE_AC, /* A/C message */
    SOURCE_SBS, /* data is of unknown quality */
    SOURCE_MLAT, /* derived from mlat */
    SOURCE_MODE_S, /* data from a Mode S message, no full CRC */
    SOURCE_JAERO, /* data is from satellite ADS-C */
    SOURCE_MODE_S_CHECKED, /* data from a Mode S message with full CRC */
    SOURCE_TISB, /* data from a TIS-B extended squitter message */
    SOURCE_ADSR, /* data from a ADS-R extended squitter message */
    SOURCE_ADSB, /* data from a ADS-B extended squitter message */
    SOURCE_PRIO, /* priority input */
} datasource_t;

/* What sort of address is this and who sent it?
 * (Earlier values are higher priority)
 */
typedef enum
{
    ADDR_ADSB_ICAO = 0, /* ADS-B, ICAO address, transponder sourced */
    ADDR_ADSB_ICAO_NT = 1, /* ADS-B, ICAO address, non-transponder */
    ADDR_ADSR_ICAO = 2, /* ADS-R, ICAO address */
    ADDR_TISB_ICAO = 3, /* TIS-B, ICAO address */

    ADDR_JAERO = 4,
    ADDR_MLAT = 5,
    ADDR_OTHER = 6,
    ADDR_MODE_S = 7,

    ADDR_ADSB_OTHER = 8, /* ADS-B, other address format */
    ADDR_ADSR_OTHER = 9, /* ADS-R, other address format */
    ADDR_TISB_TRACKFILE = 10, /* TIS-B, Mode A code + track file number */
    ADDR_TISB_OTHER = 11, /* TIS-B, other address format */

    ADDR_MODE_A = 12, /* Mode A */

    ADDR_UNKNOWN = 15/* unknown address format */
} addrtype_t;

// number of types as defined above
#define NUM_TYPES 14


typedef enum
{
    UNIT_FEET,
    UNIT_METERS
} altitude_unit_t;

typedef enum
{
    ALTITUDE_BARO,
    ALTITUDE_GEOM
} altitude_source_t;

typedef enum
{
    AG_INVALID = 0,
    AG_GROUND = 1,
    AG_AIRBORNE = 2,
    AG_UNCERTAIN = 3
} airground_t;

typedef enum
{
    SIL_INVALID, SIL_UNKNOWN, SIL_PER_SAMPLE, SIL_PER_HOUR
} sil_type_t;

typedef enum
{
    CPR_INVALID, CPR_SURFACE, CPR_AIRBORNE, CPR_COARSE
} cpr_type_t;

typedef enum
{
   CPR_NONE, CPR_LOCAL, CPR_GLOBAL
} cpr_local_t;

typedef enum
{
    HEADING_INVALID, // Not set
    HEADING_GROUND_TRACK, // Direction of track over ground, degrees clockwise from true north
    HEADING_TRUE, // Heading, degrees clockwise from true north
    HEADING_MAGNETIC, // Heading, degrees clockwise from magnetic north
    HEADING_MAGNETIC_OR_TRUE, // HEADING_MAGNETIC or HEADING_TRUE depending on the HRD bit in opstatus
    HEADING_TRACK_OR_HEADING // GROUND_TRACK / MAGNETIC / TRUE depending on the TAH bit in opstatus
} heading_type_t;

typedef enum {
    COMMB_UNKNOWN,
    COMMB_AMBIGUOUS,
    COMMB_EMPTY_RESPONSE,
    COMMB_DATALINK_CAPS,
    COMMB_GICB_CAPS,
    COMMB_AIRCRAFT_IDENT,
    COMMB_ACAS_RA,
    COMMB_VERTICAL_INTENT,
    COMMB_TRACK_TURN,
    COMMB_HEADING_SPEED
} commb_format_t;

typedef enum
{
    NAV_MODE_AUTOPILOT = 1,
    NAV_MODE_VNAV = 2,
    NAV_MODE_ALT_HOLD = 4,
    NAV_MODE_APPROACH = 8,
    NAV_MODE_LNAV = 16,
    NAV_MODE_TCAS = 32
} nav_modes_t;

// Matches encoding of the ES type 28/1 emergency/priority status subfield

typedef enum
{
    EMERGENCY_NONE = 0,
    EMERGENCY_GENERAL = 1,
    EMERGENCY_LIFEGUARD = 2,
    EMERGENCY_MINFUEL = 3,
    EMERGENCY_NORDO = 4,
    EMERGENCY_UNLAWFUL = 5,
    EMERGENCY_DOWNED = 6,
    EMERGENCY_RESERVED = 7
} emergency_t;

typedef enum {
    NAV_ALT_INVALID,
    NAV_ALT_UNKNOWN,
    NAV_ALT_AIRCRAFT,
    NAV_ALT_MCP,
    NAV_ALT_FMS
} nav_altitude_source_t;

#define MODES_NON_ICAO_ADDRESS       (1<<24) // Set on addresses to indicate they are not ICAO addresses
#define BADDR (0xff123456) // invalid address used to set stuff like cpr_focus and show_only default value

#define MODES_INTERACTIVE_REFRESH_TIME 250      // Milliseconds
#define MODES_INTERACTIVE_DISPLAY_TTL 60000     // Delete from display after 60 seconds

#define MODES_NET_HEARTBEAT_INTERVAL 60000      // milliseconds

//#define ENABLE_DF24

#define HISTORY_SIZE 120
#define HISTORY_INTERVAL 30000

#define MODES_NOTUSED(V) ((void) V)

#ifndef AIRCRAFT_HASH_BITS
#define AIRCRAFT_HASH_BITS 20
#endif
#define AIRCRAFT_BUCKETS (1 << AIRCRAFT_HASH_BITS) // this is critical for hashing purposes

#define MODES_ICAO_FILTER_TTL 60000

#define DB_HASH_BITS 20
#define DB_BUCKETS (1 << DB_HASH_BITS) // this is critical for hashing purposes

#define STATE_BLOBS 256 // change naming scheme if increasing this
#define LOCK_THREADS_MAX 64
#define PERIODIC_UPDATE (1 * SECONDS)
#define REMOVE_STALE_INTERVAL (1 * SECONDS)

#define STAT_BUCKETS 90 // 90 * 10 seconds = 15 min (max interval in stats.json)

#define RANGEDIRS_BUCKETS 360
#define RANGEDIRS_IVALS 64

#define PING_REJECT (3 * SECONDS)
#define PING_DISCONNECT (15 * SECONDS)
#define PING_BUCKETS 20 // statistics on round trip time
#define PING_BUCKETBASE (24) // milliseconds of first bucket
#define PING_BUCKETMULT (1.2) // each bucket will grow by that factor

#define PING_REDUCE (1500) // 1.5 seconds
#define PING_REDUCE_DURATION (15 * SECONDS)

#define GARBAGE_THRESHOLD (512)

/* A timestamp that indicates the data is synthetic, created from a
 * multilateration result
 */
#define MAGIC_MLAT_TIMESTAMP 0xFF004D4C4154LL
#define MAGIC_UAT_TIMESTAMP  0xFF004D4C4155LL

#define MAGIC_ANY_TIMESTAMP  0xFFFFFFFFFFFFULL

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


#if defined(__llvm__)

#define _unroll_8 _Pragma ("unroll 8")
#define _unroll_16 _Pragma ("unroll 16")
#define _unroll_32 _Pragma ("unroll 32")

#elif defined(__GNUC__)

#if __GNUC__ >= 7
#define _unroll_8 _Pragma ("GCC unroll 8")
#define _unroll_16 _Pragma ("GCC unroll 16")
#define _unroll_32 _Pragma ("GCC unroll 32")
#else
#define _unroll_8
#define _unroll_16
#define _unroll_32
#endif

#else

#define _unroll_8
#define _unroll_16
#define _unroll_32

#endif

void setExit(int arg);
int priorityTasksPending();
void priorityTasksRun();

#define MemoryAlignment 32
#define ALIGNED __attribute__((aligned(MemoryAlignment)))

static inline void *malloc_or_exit(size_t alignment, size_t size, const char *file, int line) {
    void *buf = NULL;
    if (alignment) {
        size_t mod = size % alignment;
        if (mod != 0) {
            size += (alignment - mod);
        }
        buf = aligned_alloc(alignment, size);

        if (0) {
            mod = size % alignment;
            if (mod != 0) {
                fprintf(stderr, "aligned_alloc bad alignment: %ld\n", (long) mod);
            }
        }
    } else {
        buf = malloc(size);
    }
    if (unlikely(!buf)) {
        setExit(2); // irregular exit ... soon
        fprintf(stderr, "FATAL: malloc_or_exit() failed: %s:%d\n", file, line);
    }
    return buf;
}

// use memory alignment only for arm ....
#if defined(__arm__)
#define cmalloc(size) malloc_or_exit(MemoryAlignment, size, __FILE__, __LINE__)
#else
#define cmalloc(size) malloc_or_exit(0, size, __FILE__, __LINE__)
#endif

// Include subheaders after all the #defines are in place

#include "toString.h"
#include "util.h"
#include "fasthash.h"
#include "anet.h"
#include "net_io.h"
#include "crc.h"
#include "demod_2400.h"
#include "stats.h"
#include "cpr.h"
#include "icao_filter.h"
#include "convert.h"
#include "sdr.h"
#include "aircraft.h"
#include "globe_index.h"
#include "receiver.h"
#include "geomag.h"
#include "json_out.h"
#include "api.h"

//======================== structure declarations =========================

typedef enum
{
    SDR_NONE = 0, SDR_IFILE, SDR_RTLSDR, SDR_BLADERF, SDR_MICROBLADERF, SDR_MODESBEAST, SDR_PLUTOSDR, SDR_GNS
} sdr_type_t;

// Structure representing one magnitude buffer

struct mag_buf
{
    int64_t sampleTimestamp; // Clock timestamp of the start of this block, 12MHz clock
    double mean_level; // Mean of normalized (0..1) signal level
    double mean_power; // Mean of normalized (0..1) power level
    uint32_t dropped; // Number of dropped samples preceding this buffer
    unsigned length; // Number of valid samples _after_ overlap. Total buffer length is buf->length + Modes.trailing_samples.
    int64_t sysTimestamp; // Estimated system time at start of block
    int64_t sysMicroseconds; // sysTimestamp in microseconds
    uint16_t *data; // Magnitude data. Starts with Modes.trailing_samples worth of overlap from the previous block
#if defined(__arm__)
    /*padding 4 bytes*/
    uint32_t padding;
#endif
};

// Program global state

struct _Threads {
    threadT upkeep; // runs priorityTasksUpdate, locks most other threads when doing its thing
    threadT decode; // thread doing demodulation, decoding and networking

    threadT reader;

    threadT json; // thread writing json
    threadT globeJson; // thread writing json
    threadT globeBin; // thread writing binCraft
    threadT misc;
    threadT apiUpdate;
};
extern struct _Threads Threads;

struct modeMessage;

struct messageBuffer {
    struct modesMessage *msg;
    int len;
    int alloc;
    int id;
    struct client *activeClient;
};

struct _Modes
{ // Internal state
    pthread_mutex_t traceDebugMutex;

    int num_procs;
    int allPoolSize;
    threadpool_t *allPool;
    task_group_t *allTasks;

    uint32_t sdr_buf_size;
    uint32_t sdr_buf_samples;

    int64_t traceWriteTimelimit;
    int tracePoolSize;
    threadpool_t *tracePool;
    task_group_t *traceTasks;

    int triggerPastDayTraceWrite;

    int lockThreadsCount;
    threadT *lockThreads[LOCK_THREADS_MAX];

    struct timespec hungTimer1;
    struct timespec hungTimer2;
    pthread_mutex_t hungTimerMutex;
    char *currentTask;
    int64_t joinTimeout;

    unsigned first_free_buffer; // Entry in mag_buffers that will next be filled with input.
    unsigned first_filled_buffer; // Entry in mag_buffers that has valid data and will be demodulated next. If equal to next_free_buffer, there is no unprocessed data.
    unsigned trailing_samples; // extra trailing samples in magnitude buffers
    int volatile exit; // Exit from the main loop when true
    int volatile exitSoon;
    int fd; // --ifile option file descriptor
    input_format_t input_format; // --iformat option
    iq_convert_fn converter_function;
    char * dev_name;
    int gain;
    int dc_filter; // should we apply a DC filter?
    int enable_agc;
    sdr_type_t sdr_type; // where are we getting data from?
    int freq;
    int ppm_error;
    char aneterr[ANET_ERR_LEN];
    struct net_service_group services_in; // Active services which primarily receive data
    struct net_service_group services_out; // Active services which primarily send data
    int exitNowEventfd;
    int exitSoonEventfd;

    int net_epfd; // epoll fd used for most network stuff
    int net_event_count;
    int net_maxEvents;

    struct epoll_event *net_events;

    struct messageBuffer *netMessageBuffer;
    int decodeThreads;
    threadpool_t *decodePool;
    task_group_t *decodeTasks;
    pthread_mutex_t decodeLock;
    pthread_mutex_t trackLock;
    pthread_mutex_t outputLock;

    int max_fds;
    int modesClientCount;
    int api_fds_per_thread;
    int total_aircraft_count;
    float estimated_ppm;
    uint64_t trace_chunk_size;
    uint64_t trace_cache_size;
    uint64_t trace_current_size;

    ssize_t volatile state_chunk_size;
    ssize_t volatile state_chunk_size_read;

    ALIGNED struct aircraft * aircraft[AIRCRAFT_BUCKETS];
    ALIGNED struct craftArray globeLists[GLOBE_MAX_INDEX+1];
    int receiver_table_hash_bits;
    int receiver_table_size;
    struct receiver **receiverTable;
    struct craftArray aircraftActive;
    dbEntry *db;
    dbEntry **dbIndex;
    dbEntry *db2;
    dbEntry **db2Index;
    int64_t dbModificationTime;
    int64_t receiverCount;
    struct net_writer raw_out; // Raw output
    struct net_writer beast_out; // Beast-format output
    struct net_writer beast_reduce_out; // Reduced data Beast-format output
    struct net_writer beast_in; // for sending pings to clients sending us beast data
    struct net_writer garbage_out; // Beast-format output
    struct net_writer sbs_out; // SBS-format output
    struct net_writer sbs_out_replay; // SBS-format output
    struct net_writer sbs_out_mlat; // SBS-format output
    struct net_writer sbs_out_jaero; // SBS-format output
    struct net_writer sbs_out_prio; // SBS-format output
    struct net_writer json_out; // SBS-format output
    struct net_writer feedmap_out; // SBS-format output
    struct net_writer vrs_out; // SBS-format output
    struct net_writer fatsv_out; // FATSV-format output
    struct net_writer gpsd_in; // for sending 1 line to gpsd
    struct net_service *beast_in_service;
    struct net_service *uat_in_service;

    struct hexInterval* deleteTrace;

    int write_state_blob;
    int writeInternalState;
    char *replace_state_blob;
    int64_t replace_state_inhibit_traces_until;
    int64_t network_time_limit;
    uint32_t currentPing;

    int8_t apiUpdate; // creates json snippets also by non api stuff
    int8_t api; // enable api output
    int apiThreadCount;
    atomic_int apiWorkerCpuMicro;
    atomic_uint apiRequestCounter;
    atomic_int recentTraceWrites;
    atomic_int fullTraceWrites;
    atomic_int permTraceWrites;
    struct net_service apiService;
    struct apiCon **apiListeners;

    struct apiBuffer apiBuffer[2];
    atomic_int *apiFlip;
    struct apiThread *apiThread;
    pthread_mutex_t apiFlipMutex; // mutex to read apiFlip

    // Configuration
    int8_t nfix_crc; // Number of crc bit error(s) to correct
    int8_t fixDF; // fix message type single bit errors that become DF17
    int8_t check_crc; // Only display messages with good CRC
    int8_t raw; // Raw output format
    int8_t mode_ac; // Enable decoding of SSR Modes A & C
    int8_t mode_ac_auto; // allow toggling of A/C by Beast commands
    int8_t debug_net;
    int8_t debug_no_discard;
    int8_t debug_nextra;
    int8_t debug_cpr;
    int8_t debug_speed_check;
    int8_t debug_garbage;
    int8_t debug_receiver;
    int8_t debug_rough_receiver_location;
    int8_t debug_traceCount;
    int8_t debug_traceAlloc;
    int8_t debug_sampleCounter;
    int8_t debug_dbJson;
    int8_t debug_ACAS;
    int8_t debug_api;
    int8_t debug_recent;
    int8_t debug_squawk;
    int8_t debug_ping;
    int8_t debug_callsign;
    int8_t debug_nogps;
    int8_t debug_uuid;
    int8_t debug_bogus;
    int8_t decode_all;
    int8_t debug_maxRange;
    int8_t debug_removeStaleDuration;
    int8_t debug_receiverRangeLimit;
    int8_t debug_yeet;
    int8_t debug_7700;
    int8_t debug_send_uuid;
    int8_t debug_provoke_segfault;
    int8_t debug_position_timing;
    int8_t debug_lastStatus;
    int8_t incrementId;
    int8_t dump_accept_synthetic_now;
    int8_t syntethic_now_suppress_errors;
    int8_t tar1090_use_api;
    int8_t verbose;

    int8_t net_verbatim; // if true, send the original message, not the CRC-corrected one
    int8_t netReceiverId;
    int8_t ping;
    int8_t netReceiverIdPrint;
    int8_t netReceiverIdJson;
    int8_t netIngest;
    int8_t forward_mlat; // allow forwarding of mlat messages to output ports
    int8_t quiet; // Suppress stdout
    int8_t interactive; // Interactive mode
    int8_t stats_range_histo; // Collect/show a range histogram?
    int8_t outline_json; // write a range outline json file
    int8_t onlyaddr; // Print only ICAO addresses
    int8_t metric; // Use metric units
    int8_t use_gnss; // Use GNSS altitudes with H suffix ("HAE", though it isn't always) when available
    int8_t mlat; // Use Beast ascii format for raw data output, i.e. @...; iso *...;
    int8_t json_location_accuracy; // Accuracy of location metadata: 0=none, 1=approx, 2=exact

    int8_t net; // Enable networking
    int8_t net_only; // Enable just networking
    int8_t jsonLongtype;
    int8_t viewadsb;
    int8_t sbsReduce; // apply beast reduce logic to SBS messages

    int position_persistence; // Maximum number of consecutive implausible positions from global CPR to invalidate a known position
    int json_reliable;

    uint32_t filterDF; // Only show messages with certain DF types
    uint32_t filterDFbitset; // Bitset, Only show messages with these DF types

    uint32_t trackExpireJaero;
    uint32_t trackExpireMax;

    uint32_t cpr_focus;
    uint32_t trace_focus;
    uint32_t leg_focus;
    uint32_t show_only; // Only show messages from this ICAO
    uint64_t receiver_focus;

    uint32_t preambleThreshold;
    int net_output_flush_size; // Minimum Size of output data
    int32_t net_output_beast_reduce_interval; // Position update interval for data reduction
    int32_t ping_reduce;
    int32_t ping_reject;
    int64_t doubleBeastReduceIntervalUntil;
    float beast_reduce_filter_distance;
    float beast_reduce_filter_altitude;
    int32_t net_connector_delay;
    int32_t net_connector_delay_min;
    int64_t next_reconnect_callback;
    int64_t last_connector_fail;
    int32_t net_heartbeat_interval; // TCP heartbeat interval (milliseconds)
    int32_t net_output_flush_interval; // Maximum interval (in milliseconds) between outputwrites
    int64_t net_output_next_flush;
    double fUserLat; // Users receiver/antenna lat/lon needed for initial surface location
    double fUserLon; // Users receiver/antenna lat/lon needed for initial surface location
    double maxRange; // Absolute maximum decoding range, in *metres*
    double sample_rate; // actual sample rate in use (in hz)
    int64_t interactive_display_ttl; // Interactive mode: TTL display
    int64_t json_interval; // Interval between rewriting the json aircraft file, in milliseconds; also the advertised map refresh interval
    int64_t stats_display_interval; // Interval (millis) between stats dumps,
    int64_t range_outline_duration;
    int64_t writeTracesActualDuration; // how long the trace writing cycle took
    char *db_file;
    char *net_output_raw_ports; // List of raw output TCP ports
    char *net_input_raw_ports; // List of raw input TCP ports
    char *net_output_sbs_ports; // List of SBS output TCP ports
    char *net_input_sbs_ports; // List of SBS input TCP ports
    char *net_output_jaero_ports; // jaero SBS output ports
    char *net_input_jaero_ports; // jaero SBS input ports
    char *net_input_beast_ports; // List of Beast input TCP ports
    char *net_output_beast_ports; // List of Beast output TCP ports
    char *net_output_beast_reduce_ports; // List of Beast output TCP ports
    char *net_output_json_ports;
    char *net_output_api_ports;
    char *garbage_ports;
    char *net_output_vrs_ports; // List of VRS output TCP ports
    int64_t net_output_vrs_interval;
    int64_t net_output_json_interval;
    int net_output_json_include_nopos;
    struct net_connector *net_connectors; // client connectors
    int net_connectors_count;
    int net_connectors_size;
    int64_t synthetic_now;
    char *uuidFile;
    char *filename; // Input form file, --ifile option
    char *net_bind_address; // Bind address
    char *json_dir; // Path to json base directory, or NULL not to write json.
    int8_t aircraft_json_seen_by_list;
    int aircraft_json_seen_by_list_timeout; // Timeout in seconds
    char *globe_history_dir;
    char *state_dir;
    char *state_parent_dir;
    char *dump_beast_dir; // write raw beast with a timestamp every millisecond for low level replay
    zstd_fw_t *dump_fw;
    int64_t dump_next_ts; // last timestamp sent
    int32_t dump_interval;
    int32_t dump_beast_index;
    uint64_t dump_lastReceiverId;
    int8_t dump_reduce; // only dump beast that would be sent out according to reduce_interval
    int8_t state_only_on_exit;
    int8_t free_aircraft;
    char *prom_file;
    int64_t heatmap_current_interval;
    int64_t heatmap_interval; // don't change data type
    int heatmap;
    char *heatmap_dir;
    int64_t keep_traces; // how long traces are saved in internal memory
    int64_t json_trace_interval; // max time ignoring new positions for trace
    int32_t traceMax; // max trace length
    int32_t traceReserve; // grow trace allocation if we have less than traceReserve free spots
    int32_t traceRecentPoints;
    int32_t traceCachePoints;
    int32_t traceChunkPoints;
    int32_t traceChunkMaxBytes;
    int json_globe_index; // Enable extra globe indexed json files.
    int acasFD1; // file descriptor to write acasFDs to
    int acasFD2;
    struct tile *json_globe_special_tiles;
    int32_t *json_globe_indexes;
    int32_t json_globe_indexes_len;
    int specialTileCount;
    int json_gzip; // Enable extra globe indexed json files.

    int beast_fd; // Local Modes-S Beast handler
    int beast_baudrate; // Mode-S beast and similar baud rate
    char *beast_serial; // Modes-S Beast device path
    struct client *serial_client;

    int net_sndbuf_size; // TCP output buffer size (64Kb * 2^n)
    int json_aircraft_history_next;
    int json_aircraft_history_full;
    int trace_hist_only;
    float messageRateMult;
    uint32_t binCraftVersion; // never change the type for this variable
    int8_t userLocationValid;
    int8_t biastee;
    int8_t triggerPermWriteDay;
    int8_t acasDay;
    int8_t traceDay;
    int8_t onlyBin; // only write binCraft for globe (1) and also aircraft.json (2)
    int8_t enableBinGz;

    int8_t updateStats;
    int8_t staleStop;

    struct timespec reader_cpu_accumulator; // CPU time used by the reader thread, copied out and reset by the main thread under the mutex
    ALIGNED struct mag_buf mag_buffers[MODES_MAG_BUFFERS]; // Converted magnitude buffers from RTL or file input

    int64_t startup_time;
    int64_t next_stats_update;
    int64_t next_stats_display;
    int64_t next_api_update;
    int64_t next_remove_stale;
    int stats_bucket; // index that has just been writte to
    ALIGNED struct stats stats_10[STAT_BUCKETS];
    struct stats stats_current;
    struct stats stats_alltime;
    struct stats stats_periodic;
    struct stats stats_1min;
    struct stats stats_5min;
    struct stats stats_15min;

    struct statsCount globalStatsCount;

    // array for thread numbers
    ALIGNED int threadNumber[256];

    int lastRangeDirHour;
    ALIGNED struct distCoords rangeDirs[RANGEDIRS_IVALS][RANGEDIRS_BUCKETS];

    int64_t apiShutdownDelay;
};

extern struct _Modes Modes;

// The struct we use to store information about a decoded message.

struct modesMessage
{
    // Generic fields
    unsigned char msg[MODES_LONG_MSG_BYTES]; // Binary message.
    unsigned char verbatim[MODES_LONG_MSG_BYTES]; // Binary message, as originally received before correction
    double signalLevel; // RSSI, in the range [0..1], as a fraction of full-scale power
    struct client *client; // network client this message came from, NULL otherwise
    struct aircraft *aircraft; // tracked aircraft associated with this message or NULL
    struct messageBuffer *messageBuffer;

    int64_t timestamp; // Timestamp of the message (12MHz clock)
    int64_t sysTimestamp; // Timestamp of the message (system time)
    uint64_t receiverId; // zero if not transmitted
    int msgtype; // Downlink format #
    int msgbits; // Number of bits in message
    int score; // Scoring from scoreModesMessage, if used
    int receiverCountMlat; // number of receivers for MLAT messages
    int mlatEPU; // estimated position uncertainty
    int correctedbits; // No. of bits corrected
    int decodeResult;
    uint32_t crc; // Message CRC
    uint32_t addr; // Address Announced
    uint32_t maybe_addr; // probably the address, good chance to be wrong
    addrtype_t addrtype; // address format / source
    int8_t remote; // If set this message is from a remote station
    int8_t sbs_in; // Signifies this message is coming from basestation input
    int8_t sbsMsgType; // SBS message type
    int8_t reduce_forward; // forward this message for reduced beast output
    int8_t garbage; // from garbage receiver
    int8_t duplicate; // associated position is a duplicate
    int8_t duplicate_checked; // duplicate check done
    int8_t pos_bad; // speed_check failed
    int8_t pos_ignore; // associated position is old / delayed / misc error
    int8_t pos_old; // associated position is old / delayed / misc error
    int8_t pos_receiver_range_exceeded;
    int8_t trackUnreliable;
    int8_t speedUnreliable;
    int8_t in_disc_cache;
    int8_t jsonPositionOutputEmit;
    datasource_t source; // Characterizes the overall message source
    // Raw data, just extracted directly from the message
    // The names reflect the field names in Annex 4
    unsigned IID; // extracted from CRC of DF11s
    unsigned AA;
    unsigned AC;
    unsigned CA;
    unsigned CC;
    unsigned CF;
    unsigned DR;
    unsigned FS;
    unsigned ID;
    unsigned KE;
    unsigned ND;
    unsigned RI;
    unsigned SL;
    unsigned UM;
    unsigned VS;
    unsigned metype; // DF17/18 ME type
    unsigned mesub; // DF17/18 ME subtype

    unsigned char MB[7];
    unsigned char MD[10];
    unsigned char ME[7];
    unsigned char MV[7];

    // Decoded data
    bool baro_alt_valid;
    bool geom_alt_valid;
    bool track_valid;
    bool track_rate_valid;
    bool heading_valid;
    bool roll_valid;
    bool gs_valid;
    bool ias_valid;
    bool tas_valid;
    bool mach_valid;
    bool baro_rate_valid;
    bool geom_rate_valid;
    bool squawk_valid;
    bool callsign_valid;
    bool cpr_valid;
    bool cpr_odd;
    bool cpr_decoded;
    bool cpr_relative;
    bool category_valid;
    bool geom_delta_valid;
    bool from_mlat;
    bool from_tisb;
    bool spi_valid;
    bool spi;
    bool alert_valid;
    bool alert;
    bool emergency_valid;
    bool sbs_pos_valid;
    bool alt_q_bit;
    bool acas_ra_valid;
    bool geom_alt_derived;

    bool squawk_emergency_valid;
    bool squawk_emergency;

    // valid if baro_alt_valid:
    int baro_alt; // Altitude in either feet or meters
    altitude_unit_t baro_alt_unit; // the unit used for altitude

    // valid if geom_alt_valid:
    int geom_alt; // Altitude in either feet or meters
    altitude_unit_t geom_alt_unit; // the unit used for altitude

    // following fields are valid if the corresponding _valid field is set:
    int geom_delta; // Difference between geometric and baro alt
    float heading; // ground track or heading, degrees (0-359). Reported directly or computed from from EW and NS velocity
    heading_type_t heading_type; // how to interpret 'track_or_heading'
    float track_rate; // Rate of change of track, degrees/second
    float roll; // Roll, degrees, negative is left roll

    struct
    {
        // Groundspeed, kts, reported directly or computed from from EW and NS velocity
        // For surface movement, this has different interpretations for v0 and v2; both
        // fields are populated. The tracking layer will update "gs.selected".
        float v0;
        float v2;
        float selected;
    } gs;
    unsigned ias; // Indicated airspeed, kts
    unsigned tas; // True airspeed, kts
    double mach; // Mach number
    int baro_rate; // Rate of change of barometric altitude, feet/minute
    int geom_rate; // Rate of change of geometric (GNSS / INS) altitude, feet/minute
    char callsign[16]; // 8 chars flight number, NUL-terminated
    unsigned squawk; // 13 bits identity (Squawk), encoded as 4 hex digits
    unsigned category; // A0 - D7 encoded as a single hex byte
    emergency_t emergency; // emergency/priority status

    // valid if cpr_valid
    cpr_type_t cpr_type; // The encoding type used (surface, airborne, coarse TIS-B)
    uint32_t cpr_lat; // Non decoded latitude.
    uint32_t cpr_lon; // Non decoded longitude.
    uint32_t cpr_nucp; // NUCp/NIC value implied by message type

    airground_t airground; // air/ground state

    // valid if cpr_decoded:
    double decoded_lat;
    double decoded_lon;
    unsigned decoded_nic;
    unsigned decoded_rc;

    double distance_traveled; // set in speed_check, zero is invalid
    double receiver_distance; // distance to receiver
    float calculated_track; // set in speed_check, -1 is invalid

    commb_format_t commb_format; // Inferred format of a comm-b message

    // various integrity/accuracy things

    struct
    {
        bool nic_a_valid;
        bool nic_b_valid;
        bool nic_c_valid;
        bool nic_baro_valid;
        bool nac_p_valid;
        bool nac_v_valid;
        bool gva_valid;
        bool sda_valid;

        bool nic_a; // if nic_a_valid
        bool nic_b; // if nic_b_valid
        bool nic_c; // if nic_c_valid
        bool nic_baro; // if nic_baro_valid

        unsigned nac_p; // if nac_p_valid
        unsigned nac_v; // if nac_v_valid

        unsigned sil; // if sil_type != SIL_INVALID

        unsigned gva; // if gva_valid

        unsigned sda; // if sda_valid
        sil_type_t sil_type;
    } accuracy;

    // Operational Status

    struct
    {
        sil_type_t sil_type;
        heading_type_t tah;
        heading_type_t hrd;
        enum
        {
            ANGLE_HEADING, ANGLE_TRACK
        } track_angle;

        unsigned cc_lw;
        unsigned cc_antenna_offset;

        unsigned valid : 1;
        unsigned version : 3;

        unsigned om_acas_ra : 1;
        unsigned om_ident : 1;
        unsigned om_atc : 1;
        unsigned om_saf : 1;

        unsigned cc_acas : 1;
        unsigned cc_cdti : 1;
        unsigned cc_1090_in : 1;
        unsigned cc_arv : 1;
        unsigned cc_ts : 1;
        unsigned cc_tc : 2;
        unsigned cc_uat_in : 1;
        unsigned cc_poa : 1;
        unsigned cc_b2_low : 1;
        unsigned cc_lw_valid : 1;
    } opstatus;

    // combined:
    //   Target State & Status (ADS-B V2 only)
    //   Comm-B BDS4,0 Vertical Intent

    struct
    {
        unsigned fms_altitude; // FMS selected altitude
        unsigned mcp_altitude; // MCP/FCU selected altitude
        float qnh; // altimeter setting (QFE or QNH/QNE), millibars
        float heading; // heading, degrees (0-359) (could be magnetic or true heading; magnetic recommended)
        bool heading_valid;
        bool fms_altitude_valid;
        bool mcp_altitude_valid;
        bool qnh_valid;
        bool modes_valid;
        heading_type_t heading_type;

        nav_altitude_source_t altitude_source;

        nav_modes_t modes;
    } nav;
};

/* All the program options */
enum {
    OptDeviceType = 700,
    OptDevice,
    OptGain,
    OptFreq,
    OptInteractive,
    OptNoInteractive,
    OptInteractiveTTL,
    OptRaw,
    OptPreambleThreshold,
    OptModeAc,
    OptModeAcAuto,
    OptForwardMlat,
    OptLat,
    OptLon,
    OptMaxRange,
    OptFix,
    OptNoFix,
    OptNoFixDf,
    OptAggressive,
    OptMlat,
    OptStats,
    OptStatsRange,
    OptStatsEvery,
    OptRangeOutlineDuration,
    OptOnlyAddr,
    OptMetric,
    OptGnss,
    OptSnip,
    OptDebug,
    OptDevel,
    OptReceiverFocus,
    OptCprFocus,
    OptLegFocus,
    OptTraceFocus,
    OptQuiet,
    OptShowOnly,
    OptFilterDF,
    OptJsonDir,
    OptAircraftJsonSeenByList,
    OptAircraftJsonSeenByListTimeout,
    OptJsonGzip,
    OptJsonOnlyBin,
    OptEnableBinGz,
    OptJsonReliable,
    OptPositionPersistence,
    OptJaeroTimeout,
    OptDbFile,
    OptDbFileLongtype,
    OptPromFile,
    OptGlobeHistoryDir,
    OptStateDir,
    OptStateOnlyOnExit,
    OptHeatmap,
    OptHeatmapDir,
    OptDumpBeastDir,
    OptJsonTime,
    OptJsonLocAcc,
    OptJsonGlobeIndex,
    OptJsonTraceInt,
    OptJsonTraceHistOnly,
    OptDcFilter,
    OptBiasTee,
    OptNet,
    OptNetOnly,
    OptNetBindAddr,
    OptNetRiPorts,
    OptNetRoPorts,
    OptNetSbsPorts,
    OptNetSbsInPorts,
    OptNetJaeroPorts,
    OptNetJaeroInPorts,
    OptNetBiPorts,
    OptNetBoPorts,
    OptNetBeastReducePorts,
    OptNetBeastReduceInterval,
    OptNetBeastReduceFilterAlt,
    OptNetBeastReduceFilterDist,
    OptNetSbsReduce,
    OptNetVRSPorts,
    OptNetVRSInterval,
    OptNetJsonPorts,
    OptNetJsonPortInterval,
    OptNetJsonPortNoPos,
    OptNetApiPorts,
    OptApiShutdownDelay,
    OptTar1090UseApi,
    OptNetRoSize,
    OptNetRoRate,
    OptNetRoIntervall,
    OptNetConnector,
    OptNetConnectorDelay,
    OptNetHeartbeat,
    OptNetBuffer,
    OptNetVerbatim,
    OptNetReceiverId,
    OptNetReceiverIdJson,
    OptNetIngest,
    OptSdrBufSize,
    OptGarbage,
    OptDecodeThreads,
    OptUuidFile,
    OptRtlSdrEnableAgc,
    OptRtlSdrPpm,
    OptBeastSerial,
    OptBeastBaudrate,
    OptBeastDF1117,
    OptBeastDF045,
    OptBeastMlatTimeOff,
    OptBeastCrcOff,
    OptBeastFecOff,
    OptBeastModeAc,
    OptIfileName,
    OptIfileFormat,
    OptIfileThrottle,
    OptBladeFpgaDir,
    OptBladeDecim,
    OptBladeBw,
    OptPlutoUri,
    OptPlutoNetwork,
};


// This one needs modesMessage:
#include "track.h"
#include "mode_s.h"
#include "comm_b.h"

// ======================== function declarations =========================

#ifdef __cplusplus
extern "C"
{
#endif

    //
    // Functions exported from mode_ac.c
    //
    int detectModeA (uint16_t *m, struct modesMessage *mm);
    void decodeModeAMessage (struct modesMessage *mm, int ModeA);
    void modeACInit ();
    int modeAToModeC (unsigned int modeA);
    unsigned modeCToModeA (int modeC);

    //
    // Functions exported from interactive.c
    //
    void interactiveInit (void);
    void interactiveShowData (void);
    void interactiveCleanup (void);

    // Provided by readsb.c & viewadsb.c
    void receiverPositionChanged (float lat, float lon, float alt);

#ifdef __cplusplus
}
#endif

#endif // __DUMP1090_H


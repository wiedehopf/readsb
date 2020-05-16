// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// readsb.c: main program & miscellany
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

#define READSB
#include "readsb.h"
#include "help.h"
#include "geomag.h"

#include <stdarg.h>

static void writeHeatmap();
static void backgroundTasks(void);
//
// ============================= Program options help ==========================
//
// This is a little silly, but that's how the preprocessor works..
#define _stringize(x) #x

static error_t parse_opt(int key, char *arg, struct argp_state *state);
const char *argp_program_version = MODES_READSB_VARIANT " " MODES_READSB_VERSION;
const char doc[] = "readsb Mode-S/ADSB/TIS Receiver   "
        MODES_READSB_VARIANT " " MODES_READSB_VERSION
        "\nBuild options: "
#ifdef ENABLE_RTLSDR
        "ENABLE_RTLSDR "
#endif
#ifdef ENABLE_BLADERF
        "ENABLE_BLADERF "
#endif
#ifdef ENABLE_PLUTOSDR
        "ENABLE_PLUTOSDR "
#endif
#ifdef SC16Q11_TABLE_BITS
#define stringize(x) _stringize(x)
        "SC16Q11_TABLE_BITS=" stringize(SC16Q11_TABLE_BITS)
#undef stringize
#endif
"\v"
"Debug mode flags: d = Log frames decoded with errors\n"
"                  D = Log frames decoded with zero errors\n"
"                  c = Log frames with bad CRC\n"
"                  C = Log frames with good CRC\n"
"                  p = Log frames with bad preamble\n"
"                  n = Log network debugging info\n"
"                  j = Log frames to frames.js, loadable by debug.html\n";

#undef _stringize
#undef verstring

const char args_doc[] = "";
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

//
// ============================= Utility functions ==========================
//
static void log_with_timestamp(const char *format, ...) __attribute__ ((format(printf, 1, 2)));

static void log_with_timestamp(const char *format, ...) {
    char timebuf[128];
    char msg[1024];
    time_t now;
    struct tm local;
    va_list ap;

    now = time(NULL);
    localtime_r(&now, &local);
    strftime(timebuf, 128, "%c %Z", &local);
    timebuf[127] = 0;

    va_start(ap, format);
    vsnprintf(msg, 1024, format, ap);
    va_end(ap);
    msg[1023] = 0;

    fprintf(stderr, "%s  %s\n", timebuf, msg);
}

static void sigintHandler(int dummy) {
    MODES_NOTUSED(dummy);
    if (Modes.decodeThread)
        pthread_kill(Modes.decodeThread, SIGUSR1);
    if (Modes.jsonThread)
        pthread_kill(Modes.jsonThread, SIGUSR1);
    if (Modes.jsonGlobeThread)
        pthread_kill(Modes.jsonGlobeThread, SIGUSR1);

    for (int i = 0; i < TRACE_THREADS; i++) {
        if (Modes.jsonTraceThread[i])
            pthread_kill(Modes.jsonTraceThread[i], SIGUSR1);
    }
    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
    log_with_timestamp("Caught SIGINT, shutting down..\n");
}

static void sigtermHandler(int dummy) {
    MODES_NOTUSED(dummy);
    if (Modes.decodeThread)
        pthread_kill(Modes.decodeThread, SIGUSR1);
    if (Modes.jsonThread)
        pthread_kill(Modes.jsonThread, SIGUSR1);
    if (Modes.jsonGlobeThread)
        pthread_kill(Modes.jsonGlobeThread, SIGUSR1);
    for (int i = 0; i < TRACE_THREADS; i++) {
        if (Modes.jsonTraceThread[i])
            pthread_kill(Modes.jsonTraceThread[i], SIGUSR1);
    }
    signal(SIGTERM, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
    log_with_timestamp("Caught SIGTERM, shutting down..\n");
}

void receiverPositionChanged(float lat, float lon, float alt) {
    log_with_timestamp("Autodetected receiver location: %.5f, %.5f at %.0fm AMSL", lat, lon, alt);
    writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()); // location changed
}


//
// =============================== Initialization ===========================
//
static void modesInitConfig(void) {
    // Default everything to zero/NULL
    memset(&Modes, 0, sizeof (Modes));

    for (int i = 0; i < 256; i++) {
        threadNumber[i] = i;
    }

    // Now initialise things that should not be 0/NULL to their defaults
    Modes.gain = MODES_MAX_GAIN;
    Modes.freq = MODES_DEFAULT_FREQ;
    Modes.check_crc = 1;
    Modes.net_heartbeat_interval = MODES_NET_HEARTBEAT_INTERVAL;
    Modes.net_input_raw_ports = strdup("0");
    Modes.net_output_raw_ports = strdup("0");
    Modes.net_output_sbs_ports = strdup("0");
    Modes.net_input_sbs_ports = strdup("0");
    Modes.net_input_beast_ports = strdup("0");
    Modes.net_output_beast_ports = strdup("0");
    Modes.net_output_beast_reduce_ports = strdup("0");
    Modes.net_output_beast_reduce_interval = 125;
    Modes.net_output_vrs_ports = strdup("0");
    Modes.net_output_json_ports = strdup("0");
    Modes.net_connector_delay = 30 * 1000;
    Modes.interactive_display_ttl = MODES_INTERACTIVE_DISPLAY_TTL;
    Modes.json_interval = 1000;
    Modes.json_location_accuracy = 1;
    Modes.maxRange = 1852 * 300; // 300NM default max range
    Modes.mode_ac_auto = 0;
    Modes.nfix_crc = 1;
    Modes.biastee = 0;
    Modes.filter_persistence = 8;
    Modes.net_sndbuf_size = 2; // Default to 256 kB network write buffers
    Modes.net_output_flush_size = 1200; // Default to 1200 Bytes
    Modes.net_output_flush_interval = 50; // Default to 50 ms
    Modes.basestation_is_mlat = 1;
    Modes.cpr_focus = 0xc0ffeeba;
    Modes.netReceiverId = 0;
    Modes.netIngest = 0;
    Modes.uuidFile = strdup("/boot/adsbx-uuid");
    Modes.json_trace_interval = 30 * 1000;

    //Modes.cpr_focus = 0x3d68d2;

    time_t nowish = (mstime() - 2000)/1000;
    struct tm utc;
    gmtime_r(&nowish, &utc);
    Modes.mday = utc.tm_mday;

    sdrInitConfig();

    //receiverTest();
}
//
//=========================================================================
//
static void modesInit(void) {
    int i;

    pthread_mutex_init(&Modes.data_mutex, NULL);
    pthread_cond_init(&Modes.data_cond, NULL);

    pthread_mutex_init(&Modes.decodeThreadMutex, NULL);
    pthread_mutex_init(&Modes.jsonThreadMutex, NULL);
    pthread_mutex_init(&Modes.jsonGlobeThreadMutex, NULL);
    for (int i = 0; i < TRACE_THREADS; i++) {
        pthread_mutex_init(&Modes.jsonTraceThreadMutex[i], NULL);
    }

    geomag_init();

    Modes.sample_rate = (double)2400000.0;

    // Allocate the various buffers used by Modes
    Modes.trailing_samples = (MODES_PREAMBLE_US + MODES_LONG_MSG_BITS + 16) * 1e-6 * Modes.sample_rate;

    if (Modes.sdr_type == SDR_NONE || Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS) {
        Modes.net_only = 1;
    }
    if (!Modes.net_only) {
        for (i = 0; i < MODES_MAG_BUFFERS; ++i) {
            if ((Modes.mag_buffers[i].data = calloc(MODES_MAG_BUF_SAMPLES + Modes.trailing_samples, sizeof (uint16_t))) == NULL) {
                fprintf(stderr, "Out of memory allocating magnitude buffer.\n");
                exit(1);
            }

            Modes.mag_buffers[i].length = 0;
            Modes.mag_buffers[i].dropped = 0;
            Modes.mag_buffers[i].sampleTimestamp = 0;
        }
    }

    // Validate the users Lat/Lon home location inputs
    if ((Modes.fUserLat > 90.0) // Latitude must be -90 to +90
            || (Modes.fUserLat < -90.0) // and
            || (Modes.fUserLon > 360.0) // Longitude must be -180 to +360
            || (Modes.fUserLon < -180.0)) {
        Modes.fUserLat = Modes.fUserLon = 0.0;
    } else if (Modes.fUserLon > 180.0) { // If Longitude is +180 to +360, make it -180 to 0
        Modes.fUserLon -= 360.0;
    }
    // If both Lat and Lon are 0.0 then the users location is either invalid/not-set, or (s)he's in the
    // Atlantic ocean off the west coast of Africa. This is unlikely to be correct.
    // Set the user LatLon valid flag only if either Lat or Lon are non zero. Note the Greenwich meridian
    // is at 0.0 Lon,so we must check for either fLat or fLon being non zero not both.
    // Testing the flag at runtime will be much quicker than ((fLon != 0.0) || (fLat != 0.0))
    Modes.bUserFlags &= ~MODES_USER_LATLON_VALID;
    if ((Modes.fUserLat != 0.0) || (Modes.fUserLon != 0.0)) {
        Modes.bUserFlags |= MODES_USER_LATLON_VALID;
    }

    // Limit the maximum requested raw output size to less than one Ethernet Block
    // Set to default if 0
    if (Modes.net_output_flush_size > (MODES_OUT_FLUSH_SIZE) || Modes.net_output_flush_size == 0) {
        Modes.net_output_flush_size = MODES_OUT_FLUSH_SIZE;
    }
    if (Modes.net_output_flush_interval > (MODES_OUT_FLUSH_INTERVAL)) {
        Modes.net_output_flush_interval = MODES_OUT_FLUSH_INTERVAL;
    }
    if (Modes.net_sndbuf_size > (MODES_NET_SNDBUF_MAX)) {
        Modes.net_sndbuf_size = MODES_NET_SNDBUF_MAX;
    }

    if((Modes.net_connector_delay <= 0) || (Modes.net_connector_delay > 86400 * 1000)) {
        Modes.net_connector_delay = 30 * 1000;
    }

    // Prepare error correction tables
    modesChecksumInit(Modes.nfix_crc);
    icaoFilterInit();
    modeACInit();

    if (Modes.show_only)
        icaoFilterAdd(Modes.show_only);
}

//
//=========================================================================
//
// We read data using a thread, so the main thread only handles decoding
// without caring about data acquisition
//
static void *readerThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srand(mstime());

    sdrRun();

    // Wake the main thread (if it's still waiting)
    pthread_mutex_lock(&Modes.data_mutex);
    if (!Modes.exit)
        Modes.exit = 2; // unexpected exit
    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}

static void *jsonThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srand(mstime());

    struct timespec slp = {0, 0};
    uint64_t interval = Modes.json_interval;
    if (interval > 994)
        interval = 994;
    slp.tv_sec =  (interval / 1000);
    slp.tv_nsec = (interval % 1000) * 1000 * 1000;

    pthread_mutex_lock(&Modes.jsonThreadMutex);

    uint64_t next_history = mstime();

    while (!Modes.exit) {

        pthread_mutex_unlock(&Modes.jsonThreadMutex);

        nanosleep(&slp, NULL);

        pthread_mutex_lock(&Modes.jsonThreadMutex);

        uint64_t now = mstime();

        struct char_buffer cb = generateAircraftJson(-1);
        if (Modes.json_gzip)
            writeJsonToGzip(Modes.json_dir, "aircraft.json.gz", cb, 3);
        writeJsonToFile(Modes.json_dir, "aircraft.json", cb);

        if ((ALL_JSON) && now >= next_history) {
            char filebuf[PATH_MAX];

            snprintf(filebuf, PATH_MAX, "history_%d.json", Modes.json_aircraft_history_next);
            writeJsonToFile(Modes.json_dir, filebuf, generateAircraftJson(-1));

            if (!Modes.json_aircraft_history_full) {
                writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()); // number of history entries changed
                if (Modes.json_aircraft_history_next == HISTORY_SIZE - 1)
                    Modes.json_aircraft_history_full = 1;
            }

            Modes.json_aircraft_history_next = (Modes.json_aircraft_history_next + 1) % HISTORY_SIZE;
            next_history = now + HISTORY_INTERVAL;
        }
    }

    pthread_mutex_unlock(&Modes.jsonThreadMutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}

static void *jsonGlobeThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srand(mstime());

    static int part;
    int n_parts = 4; // power of 2

    uint64_t sleep = Modes.json_interval / (3 * n_parts);
    // write twice every json interval

    struct timespec slp = {0, 0};
    slp.tv_sec =  (sleep / 1000);
    slp.tv_nsec = (sleep % 1000) * 1000 * 1000;

    pthread_mutex_lock(&Modes.jsonGlobeThreadMutex);

    while (!Modes.exit) {
        char filename[32];

        pthread_mutex_unlock(&Modes.jsonGlobeThreadMutex);

        nanosleep(&slp, NULL);

        pthread_mutex_lock(&Modes.jsonGlobeThreadMutex);

        for (int i = 0; i < GLOBE_SPECIAL_INDEX; i++) {
            if (i % n_parts == part) {
                snprintf(filename, 31, "globe_%04d.json", i);
                struct char_buffer cb = generateAircraftJson(i);
                writeJsonToGzip(Modes.json_dir, filename, cb, 3);
                free(cb.buffer);
            }
        }
        for (int i = GLOBE_MIN_INDEX; i <= GLOBE_MAX_INDEX; i++) {
            if (i % n_parts == part) {
                if (globe_index_index(i) >= GLOBE_MIN_INDEX) {
                    snprintf(filename, 31, "globe_%04d.json", i);
                    struct char_buffer cb = generateAircraftJson(i);
                    writeJsonToGzip(Modes.json_dir, filename, cb, 3);
                    free(cb.buffer);
                }
            }
        }

        part++;
        part %= n_parts;
    }

    pthread_mutex_unlock(&Modes.jsonGlobeThreadMutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}

static void *decodeThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srand(mstime());

    pthread_mutex_lock(&Modes.decodeThreadMutex);

    /* If the user specifies --net-only, just run in order to serve network
     * clients without reading data from the RTL device.
     * This rules also in case a local Mode-S Beast is connected via USB.
     */

    if (Modes.net_only) {
        int64_t background_cpu_millis = 0;
        int64_t prev_cpu_millis = 0;
        struct timespec slp = {0, 20 * 1000 * 1000};
        while (!Modes.exit) {
            int64_t sleep_millis = 50;
            struct timespec start_time;

            prev_cpu_millis = background_cpu_millis;

            start_cpu_timing(&start_time);
            backgroundTasks();
            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);

            background_cpu_millis = (int64_t) Modes.stats_current.background_cpu.tv_sec * 1000UL +
                Modes.stats_current.background_cpu.tv_nsec / 1000000UL;
            sleep_millis -= (background_cpu_millis - prev_cpu_millis);
            sleep_millis = (sleep_millis <= 5) ? 5 : sleep_millis;

            //fprintf(stderr, "%ld\n", sleep_millis);

            slp.tv_nsec = sleep_millis * 1000 * 1000;

            pthread_mutex_unlock(&Modes.decodeThreadMutex);

            nanosleep(&slp, NULL);

            pthread_mutex_lock(&Modes.decodeThreadMutex);
        }
    } else {
        int watchdogCounter = 10; // about 1 second

        // Create the thread that will read the data from the device.
        pthread_mutex_lock(&Modes.data_mutex);
        pthread_create(&Modes.reader_thread, NULL, readerThreadEntryPoint, NULL);

        while (!Modes.exit) {
            struct timespec start_time;

            if (Modes.first_free_buffer == Modes.first_filled_buffer) {
                /* wait for more data.
                 * we should be getting data every 50-60ms. wait for max 100ms before we give up and do some background work.
                 * this is fairly aggressive as all our network I/O runs out of the background work!
                 */

                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000;
                normalize_timespec(&ts);

                pthread_mutex_unlock(&Modes.decodeThreadMutex);

                pthread_cond_timedwait(&Modes.data_cond, &Modes.data_mutex, &ts); // This unlocks Modes.data_mutex, and waits for Modes.data_cond

                pthread_mutex_lock(&Modes.decodeThreadMutex);
            }

            // Modes.data_mutex is locked, and possibly we have data.

            // copy out reader CPU time and reset it
            add_timespecs(&Modes.reader_cpu_accumulator, &Modes.stats_current.reader_cpu, &Modes.stats_current.reader_cpu);
            Modes.reader_cpu_accumulator.tv_sec = 0;
            Modes.reader_cpu_accumulator.tv_nsec = 0;

            if (Modes.first_free_buffer != Modes.first_filled_buffer) {
                // FIFO is not empty, process one buffer.

                struct mag_buf *buf;

                start_cpu_timing(&start_time);
                buf = &Modes.mag_buffers[Modes.first_filled_buffer];

                // Process data after releasing the lock, so that the capturing
                // thread can read data while we perform computationally expensive
                // stuff at the same time.
                pthread_mutex_unlock(&Modes.data_mutex);

                demodulate2400(buf);
                if (Modes.mode_ac) {
                    demodulate2400AC(buf);
                }

                Modes.stats_current.samples_processed += buf->length;
                Modes.stats_current.samples_dropped += buf->dropped;
                end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);

                // Mark the buffer we just processed as completed.
                pthread_mutex_lock(&Modes.data_mutex);
                Modes.first_filled_buffer = (Modes.first_filled_buffer + 1) % MODES_MAG_BUFFERS;
                pthread_cond_signal(&Modes.data_cond);
                pthread_mutex_unlock(&Modes.data_mutex);
                watchdogCounter = 10;
            } else {
                // Nothing to process this time around.
                pthread_mutex_unlock(&Modes.data_mutex);
                if (--watchdogCounter <= 0) {
                    log_with_timestamp("No data received from the SDR for a long time, it may have wedged");
                    watchdogCounter = 600;
                }
            }

            start_cpu_timing(&start_time);
            backgroundTasks();
            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);
            pthread_mutex_lock(&Modes.data_mutex);
        }

        pthread_mutex_unlock(&Modes.data_mutex);

        log_with_timestamp("Waiting for receive thread termination");
        pthread_join(Modes.reader_thread, NULL); // Wait on reader thread exit
        pthread_cond_destroy(&Modes.data_cond); // Thread cleanup - only after the reader thread is dead!
        pthread_mutex_destroy(&Modes.data_mutex);
        pthread_mutex_destroy(&Modes.decodeThreadMutex);
        pthread_mutex_destroy(&Modes.jsonThreadMutex);
        pthread_mutex_destroy(&Modes.jsonGlobeThreadMutex);
        for (int i = 0; i < TRACE_THREADS; i++) {
            pthread_mutex_destroy(&Modes.jsonTraceThreadMutex[i]);
        }
    }

    pthread_mutex_unlock(&Modes.decodeThreadMutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}
//
// ============================== Snip mode =================================
//
// Get raw IQ samples and filter everything is < than the specified level
// for more than 256 samples in order to reduce example file size
//
static void snipMode(int level) {
    int i, q;
    uint64_t c = 0;

    while ((i = getchar()) != EOF && (q = getchar()) != EOF) {
        if (abs(i - 127) < level && abs(q - 127) < level) {
            c++;
            if (c > MODES_PREAMBLE_SIZE) continue;
        } else {
            c = 0;
        }
        putchar(i);
        putchar(q);
    }
}

static void display_total_stats(void) {
    struct stats added;
    add_stats(&Modes.stats_alltime, &Modes.stats_current, &added);
    display_stats(&added);
}

//
//=========================================================================
//
// This function is called a few times every second by main in order to
// perform tasks we need to do continuously, like accepting new clients
// from the net, refreshing the screen in interactive mode, and so forth
//
static void backgroundTasks(void) {
    static uint64_t next_stats_display;
    static uint64_t next_stats_update;
    static uint64_t next_second;

    uint64_t now = mstime();

    icaoFilterExpire();

    if (Modes.net) {
        modesNetPeriodicWork();
    }

    if (now > next_second) {
        next_second = now + 1000;

        if (Modes.net)
            modesNetSecondWork();
    }

    // Refresh screen when in interactive mode
    if (Modes.interactive) {
        interactiveShowData();
    }

    now = mstime();
    // always update end time so it is current when requests arrive
    Modes.stats_current.end = now;

    if (now >= next_stats_update) {
        int i;

        if (next_stats_update == 0) {
            next_stats_update = now + 60000;
        } else {
            Modes.stats_latest_1min = (Modes.stats_latest_1min + 1) % 15;
            Modes.stats_1min[Modes.stats_latest_1min] = Modes.stats_current;

            add_stats(&Modes.stats_current, &Modes.stats_alltime, &Modes.stats_alltime);
            add_stats(&Modes.stats_current, &Modes.stats_periodic, &Modes.stats_periodic);

            reset_stats(&Modes.stats_5min);
            for (i = 0; i < 5; ++i)
                add_stats(&Modes.stats_1min[(Modes.stats_latest_1min - i + 15) % 15], &Modes.stats_5min, &Modes.stats_5min);

            reset_stats(&Modes.stats_15min);
            for (i = 0; i < 15; ++i)
                add_stats(&Modes.stats_1min[i], &Modes.stats_15min, &Modes.stats_15min);

            reset_stats(&Modes.stats_current);
            Modes.stats_current.start = Modes.stats_current.end = now;

            if (Modes.json_dir)
                writeJsonToFile(Modes.json_dir, "stats.json", generateStatsJson());

            next_stats_update += 60000;
        }
    }

    if (Modes.stats && now >= next_stats_display) {
        if (next_stats_display == 0) {
            next_stats_display = now + Modes.stats;
        } else {
            add_stats(&Modes.stats_periodic, &Modes.stats_current, &Modes.stats_periodic);
            display_stats(&Modes.stats_periodic);
            reset_stats(&Modes.stats_periodic);

            next_stats_display += Modes.stats;
            if (next_stats_display <= now) {
                /* something has gone wrong, perhaps the system clock jumped */
                next_stats_display = now + Modes.stats;
            }
        }
    }


}

//=========================================================================
// Clean up memory prior to exit.
static void cleanup_and_exit(int code) {
    Modes.exit = 1;
    // Free any used memory
    geomag_destroy();
    interactiveCleanup();
    free(Modes.dev_name);
    free(Modes.filename);
    /* Free only when pointing to string in heap (strdup allocated when given as run parameter)
     * otherwise points to const string
     */
    free(Modes.json_dir);
    free(Modes.globe_history_dir);
    free(Modes.net_bind_address);
    free(Modes.net_input_beast_ports);
    free(Modes.net_output_beast_ports);
    free(Modes.net_output_beast_reduce_ports);
    free(Modes.net_output_vrs_ports);
    free(Modes.net_input_raw_ports);
    free(Modes.net_output_raw_ports);
    free(Modes.net_output_sbs_ports);
    free(Modes.net_input_sbs_ports);
    free(Modes.net_output_json_ports);
    free(Modes.beast_serial);
    free(Modes.json_globe_special_tiles);
    free(Modes.uuidFile);
    /* Go through tracked aircraft chain and free up any used memory */
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        struct aircraft *a = Modes.aircraft[j], *na;
        while (a) {
            na = a->next;
            if (a) {

                pthread_mutex_unlock(&a->trace_mutex);
                pthread_mutex_destroy(&a->trace_mutex);

                if (a->first_message)
                    free(a->first_message);
                if (a->trace) {
                    free(a->trace);
                    free(a->trace_all);
                }

                free(a);
            }
            a = na;
        }
    }

    int i;
    for (i = 0; i < MODES_MAG_BUFFERS; ++i) {
        free(Modes.mag_buffers[i].data);
    }
    crcCleanupTables();

    /* Cleanup network setup */
    cleanupNetwork();

    receiverCleanup();

    for (int i = 0; i <= GLOBE_MAX_INDEX; i++) {
        ca_destroy(&Modes.globeLists[i]);
    }

#ifndef _WIN32
    exit(code);
#else
    return (code);
#endif
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    switch (key) {
        case OptDevice:
            Modes.dev_name = strdup(arg);
            break;
        case OptGain:
            Modes.gain = (int) (atof(arg)*10); // Gain is in tens of DBs
            break;
        case OptFreq:
            Modes.freq = (int) strtoll(arg, NULL, 10);
            break;
        case OptDcFilter:
            Modes.dc_filter = 1;
            break;
        case OptBiasTee:
            Modes.biastee = 1;
            break;
        case OptFix:
            Modes.nfix_crc = 1;
            break;
        case OptNoFix:
            Modes.nfix_crc = 0;
            break;
        case OptNoCrcCheck:
            Modes.check_crc = 0;
            break;
        case OptRaw:
            Modes.raw = 1;
            break;
        case OptNet:
            Modes.net = 1;
            break;
        case OptModeAc:
            Modes.mode_ac = 1;
            Modes.mode_ac_auto = 0;
            break;
        case OptNoModeAcAuto:
            Modes.mode_ac_auto = 0;
            break;
        case OptNetOnly:
            Modes.net = 1;
            Modes.sdr_type = SDR_NONE;
            break;
        case OptQuiet:
            Modes.quiet = 1;
            break;
        case OptShowOnly:
            Modes.show_only = (uint32_t) strtoul(arg, NULL, 16);
            break;
        case OptMlat:
            Modes.mlat = 1;
            break;
        case OptForwardMlat:
            Modes.forward_mlat = 1;
            break;
        case OptOnlyAddr:
            Modes.onlyaddr = 1;
            break;
        case OptMetric:
            Modes.metric = 1;
            break;
        case OptGnss:
            Modes.use_gnss = 1;
            break;
        case OptAggressive:
            Modes.nfix_crc = MODES_MAX_BITERRORS;
            break;
        case OptInteractive:
            Modes.interactive = 1;
            break;
        case OptInteractiveTTL:
            Modes.interactive_display_ttl = (uint64_t) (1000 * atof(arg));
            break;
        case OptLat:
            Modes.fUserLat = atof(arg);
            break;
        case OptLon:
            Modes.fUserLon = atof(arg);
            break;
        case OptMaxRange:
            Modes.maxRange = atof(arg) * 1852.0; // convert to metres
            break;
        case OptStats:
            if (!Modes.stats)
                Modes.stats = (uint64_t) 1 << 60; // "never"
            break;
        case OptStatsRange:
            Modes.stats_range_histo = 1;
            break;
        case OptStatsEvery:
            Modes.stats = (uint64_t) (1000 * atof(arg));
            break;
        case OptSnip:
            snipMode(atoi(arg));
            cleanup_and_exit(0);
            break;
#ifndef _WIN32
        case OptJsonDir:
            Modes.json_dir = strdup(arg);
            break;
        case OptGlobeHistoryHeatmap:
            if (atof(arg) > 0)
                Modes.globe_history_heatmap = 1000 * atof(arg);
            break;
        case OptGlobeHistoryDir:
            Modes.globe_history_dir = strdup(arg);
            break;
        case OptJsonTime:
            Modes.json_interval = (uint64_t) (1000 * atof(arg));
            if (Modes.json_interval < 100) // 0.1s
                Modes.json_interval = 100;
            break;
        case OptJsonLocAcc:
            Modes.json_location_accuracy = atoi(arg);
            break;
        case OptJsonGzip:
            Modes.json_gzip = 1;
            break;
        case OptJsonTraceInt:
            if (atof(arg) > 0)
                Modes.json_trace_interval = 1000 * atof(arg);
            break;
        case OptJsonGlobeIndex:
            Modes.json_globe_index = 1;
            Modes.json_globe_special_tiles = calloc(GLOBE_SPECIAL_INDEX, sizeof(struct tile));
            if (!Modes.json_globe_special_tiles)
                return 1;
            init_globe_index(Modes.json_globe_special_tiles);
            break;
#endif
        case OptNetHeartbeat:
            Modes.net_heartbeat_interval = (uint64_t) (1000 * atof(arg));
            break;
        case OptNetRoSize:
            Modes.net_output_flush_size = atoi(arg);
            break;
        case OptNetRoRate:
            Modes.net_output_flush_interval = 1000 * atoi(arg) / 15; // backwards compatibility
            break;
        case OptNetRoIntervall:
            Modes.net_output_flush_interval = (uint64_t) (1000 * atof(arg));
            break;
        case OptNetRoPorts:
            free(Modes.net_output_raw_ports);
            Modes.net_output_raw_ports = strdup(arg);
            break;
        case OptNetRiPorts:
            free(Modes.net_input_raw_ports);
            Modes.net_input_raw_ports = strdup(arg);
            break;
        case OptNetBoPorts:
            free(Modes.net_output_beast_ports);
            Modes.net_output_beast_ports = strdup(arg);
            break;
        case OptNetBiPorts:
            free(Modes.net_input_beast_ports);
            Modes.net_input_beast_ports = strdup(arg);
            break;
        case OptNetBeastReducePorts:
            free(Modes.net_output_beast_reduce_ports);
            Modes.net_output_beast_reduce_ports = strdup(arg);
            break;
        case OptNetBeastReduceInterval:
            if (atof(arg) >= 0)
                Modes.net_output_beast_reduce_interval = (uint64_t) (1000 * atof(arg));
            if (Modes.net_output_beast_reduce_interval > 15000)
                Modes.net_output_beast_reduce_interval = 15000;
            break;
        case OptNetBindAddr:
            free(Modes.net_bind_address);
            Modes.net_bind_address = strdup(arg);
            break;
        case OptNetSbsPorts:
            free(Modes.net_output_sbs_ports);
            Modes.net_output_sbs_ports = strdup(arg);
            break;
        case OptNetJsonPorts:
            free(Modes.net_output_json_ports);
            Modes.net_output_json_ports = strdup(arg);
            break;
        case OptNetSbsInPorts:
            free(Modes.net_input_sbs_ports);
            Modes.net_input_sbs_ports = strdup(arg);
            break;
        case OptNetVRSPorts:
            free(Modes.net_output_vrs_ports);
            Modes.net_output_vrs_ports = strdup(arg);
            break;
        case OptNetBuffer:
            Modes.net_sndbuf_size = atoi(arg);
            break;
        case OptNetVerbatim:
            Modes.net_verbatim = 1;
            break;
        case OptNetReceiverId:
            Modes.netReceiverId = 1;
            break;
        case OptNetIngest:
            Modes.netIngest = 1;
            break;
        case OptUuidFile:
            free(Modes.uuidFile);
            Modes.uuidFile = strdup(arg);
            break;
        case OptNetConnector:
            if (!Modes.net_connectors || Modes.net_connectors_count + 1 > Modes.net_connectors_size) {
                Modes.net_connectors_size = Modes.net_connectors_count * 2 + 8;
                Modes.net_connectors = realloc(Modes.net_connectors,
                        sizeof(struct net_connector *) * Modes.net_connectors_size);
                if (!Modes.net_connectors)
                    return 1;
            }
            struct net_connector *con = calloc(1, sizeof(struct net_connector));
            Modes.net_connectors[Modes.net_connectors_count++] = con;
            char *connect_string = strdup(arg);
            con->address = strtok(connect_string, ",");
            con->port = strtok(NULL, ",");
            con->protocol = strtok(NULL, ",");
            if (pthread_mutex_init(&con->mutex, NULL)) {
                fprintf(stderr, "Unable to initialize connector mutex!\n");
                exit(1);
            }
            //fprintf(stderr, "%d %s\n", Modes.net_connectors_count, con->protocol);
            if (!con->address || !con->port || !con->protocol) {
                fprintf(stderr, "--net-connector: Wrong format: %s\n", arg);
                fprintf(stderr, "Correct syntax: --net-connector=ip,port,protocol\n");
                return 1;
            }
            if (strcmp(con->protocol, "beast_out") != 0
                    && strcmp(con->protocol, "beast_reduce_out") != 0
                    && strcmp(con->protocol, "beast_in") != 0
                    && strcmp(con->protocol, "raw_out") != 0
                    && strcmp(con->protocol, "raw_in") != 0
                    && strcmp(con->protocol, "vrs_out") != 0
                    && strcmp(con->protocol, "sbs_in") != 0
                    && strcmp(con->protocol, "sbs_in_mlat") != 0
                    && strcmp(con->protocol, "sbs_in_jaero") != 0
                    && strcmp(con->protocol, "sbs_in_prio") != 0
                    && strcmp(con->protocol, "sbs_out") != 0
                    && strcmp(con->protocol, "sbs_out_mlat") != 0
                    && strcmp(con->protocol, "sbs_out_jaero") != 0
                    && strcmp(con->protocol, "sbs_out_prio") != 0
                    && strcmp(con->protocol, "json_out") != 0
               ) {
                fprintf(stderr, "--net-connector: Unknown protocol: %s\n", con->protocol);
                fprintf(stderr, "Supported protocols: beast_out, beast_in, beast_reduce_out, raw_out, raw_in, sbs_out, sbs_in, vrs_out, json_out\n");
                return 1;
            }
            if (strcmp(con->address, "") == 0 || strcmp(con->address, "") == 0) {
                fprintf(stderr, "--net-connector: ip and port can't be empty!\n");
                fprintf(stderr, "Correct syntax: --net-connector=ip,port,protocol\n");
                return 1;
            }
            if (atol(con->port) > (1<<16) || atol(con->port) < 1) {
                fprintf(stderr, "--net-connector: port must be in range 1 to 65536\n");
                return 1;
            }
            break;
        case OptNetConnectorDelay:
            Modes.net_connector_delay = (uint64_t) 1000 * atof(arg);
            break;
        case OptDebug:
            while (*arg) {
                switch (*arg) {
                    case 'D': Modes.debug |= MODES_DEBUG_DEMOD;
                        break;
                    case 'd': Modes.debug |= MODES_DEBUG_DEMODERR;
                        break;
                    case 'C': Modes.debug |= MODES_DEBUG_GOODCRC;
                        break;
                    case 'c': Modes.debug |= MODES_DEBUG_BADCRC;
                        break;
                    case 'p': Modes.debug |= MODES_DEBUG_NOPREAMBLE;
                        break;
                    case 'n': Modes.debug |= MODES_DEBUG_NET;
                        break;
                    case 'P': Modes.debug_cpr = 1;
                        break;
                    case 'R': Modes.debug_receiver = 1;
                        break;
                    case 'S': Modes.debug_speed_check = 1;
                        break;
                    case 'T': Modes.debug_traceCount = 1;
                        break;
                    case 'j': Modes.debug |= MODES_DEBUG_JS;
                        break;
                    default:
                        fprintf(stderr, "Unknown debugging flag: %c\n", *arg);
                        return 1;
                        break;
                }
                arg++;
            }
            break;
#ifdef ENABLE_RTLSDR
        case OptRtlSdrEnableAgc:
        case OptRtlSdrPpm:
#endif
        case OptBeastSerial:
        case OptBeastDF1117:
        case OptBeastDF045:
        case OptBeastMlatTimeOff:
        case OptBeastCrcOff:
        case OptBeastFecOff:
        case OptBeastModeAc:
        case OptIfileName:
        case OptIfileFormat:
        case OptIfileThrottle:
#ifdef ENABLE_BLADERF
        case OptBladeFpgaDir:
        case OptBladeDecim:
        case OptBladeBw:
#endif
#ifdef ENABLE_PLUTOSDR
        case OptPlutoUri:
        case OptPlutoNetwork:
#endif
        case OptDeviceType:
            /* Forward interface option to the specific device handler */
            if (sdrHandleOption(key, arg) == false)
                return 1;
            break;
        case ARGP_KEY_END:
            if (state->arg_num > 0)
                /* We use only options but no arguments */
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

//
//=========================================================================
//

int main(int argc, char **argv) {
    srand(mstime());

    int j;

    // Set sane defaults
    modesInitConfig();

    // signal handlers:
    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigtermHandler);
    signal(SIGUSR1, SIG_IGN);

    // Parse the command line options
    if (argp_parse(&argp, argc, argv, 0, 0, 0)) {
        cleanup_and_exit(1);
    }

#ifdef _WIN32
    // Try to comply with the Copyright license conditions for binary distribution
    if (!Modes.quiet) {
        showCopyright();
    }
#endif

    // Initialization
    log_with_timestamp("%s starting up.", MODES_READSB_VARIANT);
    fprintf(stderr, "Version: %s\n", MODES_READSB_VERSION);

    //fprintf(stderr, "%zu\n", sizeof(struct state_flags));
    fprintf(stderr, "struct sizes: %zu, ", sizeof(struct aircraft));
    fprintf(stderr, "%zu, ", sizeof(struct state));
    fprintf(stderr, "%zu\n", sizeof(struct state_all));
    //fprintf(stderr, "%zu\n", sizeof(struct modesMessage));
    //fprintf(stderr, "%zu\n", sizeof(pthread_mutex_t));
    //fprintf(stderr, "%zu\n", 10000 * sizeof(struct aircraft));
    modesInit();

    if (!sdrOpen()) {
        cleanup_and_exit(1);
    }

    if (Modes.net) {
        modesInitNet();
    }

    // init stats:
    Modes.stats_current.start = Modes.stats_current.end =
            Modes.stats_alltime.start = Modes.stats_alltime.end =
            Modes.stats_periodic.start = Modes.stats_periodic.end =
            Modes.stats_5min.start = Modes.stats_5min.end =
            Modes.stats_15min.start = Modes.stats_15min.end = mstime();

    for (j = 0; j < 15; ++j)
        Modes.stats_1min[j].start = Modes.stats_1min[j].end = Modes.stats_current.start;

    // write initial json files so they're not missing
    writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson());
    writeJsonToFile(Modes.json_dir, "stats.json", generateStatsJson());
    writeJsonToFile(Modes.json_dir, "aircraft.json", generateAircraftJson(-1));

    interactiveInit();

    if (Modes.globe_history_dir) {
        fprintf(stderr, "loading state .....\n");
        pthread_t threads[8];
        int numbers[8];
        for (int i = 0; i < 8; i++) {
            numbers[i] = i;
            pthread_create(&threads[i], NULL, load_state, &numbers[i]);
        }
        for (int i = 0; i < 8; i++) {
            pthread_join(threads[i], NULL);
        }
        for (int i = 0; i < 8; i++) {
            numbers[i] = i;
            pthread_create(&threads[i], NULL, load_blobs, &numbers[i]);
        }
        for (int i = 0; i < 8; i++) {
            pthread_join(threads[i], NULL);
        }
        uint32_t count_ac = 0;
        for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
            for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
                int new_index = a->globe_index;
                a->globe_index = -5;
                set_globe_index(a, new_index);
                count_ac++;
            }
        }
        fprintf(stderr, " .......... done, loaded %u aircraft!\n", count_ac);
        Modes.aircraftCount = count_ac;
        fprintf(stderr, "aircraft table fill: %0.1f\n", Modes.aircraftCount / (double) AIRCRAFT_BUCKETS );
    }
    if (Modes.globe_history_dir) {
        writeHeatmap();
        cleanup_and_exit(0);
    }else if (Modes.globe_history_heatmap) {
        fprintf(stderr, "Fatal: no globe-history-dir specified!\n");
        cleanup_and_exit(1);
    }

    pthread_create(&Modes.decodeThread, NULL, decodeThreadEntryPoint, NULL);

    if (Modes.json_dir) {

        //if (ALL_JSON || !Modes.json_globe_index)
            pthread_create(&Modes.jsonThread, NULL, jsonThreadEntryPoint, NULL);

        if (Modes.json_globe_index) {
            pthread_create(&Modes.jsonGlobeThread, NULL, jsonGlobeThreadEntryPoint, NULL);

            char pathbuf[PATH_MAX];
            snprintf(pathbuf, PATH_MAX, "%s/traces", Modes.json_dir);
            mkdir(pathbuf, 0755);
            for (int i = 0; i < 256; i++) {
                snprintf(pathbuf, PATH_MAX, "%s/traces/%02x", Modes.json_dir, i);
                mkdir(pathbuf, 0755);
            }

        }
        if (Modes.json_globe_index) {
            for (int i = 0; i < TRACE_THREADS; i++) {
                pthread_create(&Modes.jsonTraceThread[i], NULL, jsonTraceThreadEntryPoint, &threadNumber[i]);
            }
        }

        if (Modes.globe_history_dir) {
            char pathbuf[PATH_MAX];
            if (mkdir(Modes.globe_history_dir, 0755) && errno != EEXIST)
                perror(Modes.globe_history_dir);

            snprintf(pathbuf, PATH_MAX, "%s/internal_state", Modes.globe_history_dir);
            if (mkdir(pathbuf, 0755) && errno != EEXIST)
                perror(pathbuf);

            /*
            for (int i = 0; i < 256; i++) {
                snprintf(pathbuf, PATH_MAX, "%s/internal_state/%02x", Modes.globe_history_dir, i);
                mkdir(pathbuf, 0755);
            }
            */
        }
    }


    while (!Modes.exit) {
        struct timespec slp = {1, 0};

        nanosleep(&slp, NULL);

        trackPeriodicUpdate();
    }

    pthread_join(Modes.decodeThread, NULL); // Wait on json writer thread exit

    if (Modes.json_dir) {

        //if (ALL_JSON || !Modes.json_globe_index)
            pthread_join(Modes.jsonThread, NULL); // Wait on json writer thread exit

        if (Modes.json_globe_index) {
            pthread_join(Modes.jsonGlobeThread, NULL); // Wait on json writer thread exit
        }
        if (Modes.json_globe_index || Modes.globe_history_dir) {
            for (int i = 0; i < TRACE_THREADS; i++) {
                pthread_join(Modes.jsonTraceThread[i], NULL); // Wait on json writer thread exit
            }
        }
    }

    if (Modes.globe_history_dir) {
        fprintf(stderr, "saving state .....\n");

        pthread_t threads[8];
        int numbers[8];
        for (int i = 0; i < 8; i++) {
            numbers[i] = i;
            pthread_create(&threads[i], NULL, save_state, &numbers[i]);
        }
        for (int i = 0; i < 8; i++) {
            pthread_join(threads[i], NULL);
        }
        fprintf(stderr, "............. done!\n");
    }
    // If --stats were given, print statistics
    if (Modes.stats) {
        display_total_stats();
    }
    sdrClose();
    if (Modes.exit != 1) {
        log_with_timestamp("Abnormal exit.");
        cleanup_and_exit(1);
    }

    log_with_timestamp("Normal exit.");
    cleanup_and_exit(0);
}
//
//=========================================================================
//
//
static void writeHeatmap() {
    char pathbuf[PATH_MAX];
    snprintf(pathbuf, PATH_MAX, "%s/heatmap.bin.csv", Modes.globe_history_dir);

    int fd = open(pathbuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(pathbuf);
        cleanup_and_exit(1);
    }
    uint64_t len = 0;
    uint64_t len2 = 0;
    uint64_t alloc = 256 * 1024 * 1024;
    int32_t *buffer = malloc(alloc * sizeof(int32_t));
    int32_t *buffer2 = malloc(alloc * sizeof(int32_t));

    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if (a->trace_len == 0) continue;

            struct state *trace = a->trace;
            uint64_t next = 0;

            for (int i = 0; i < a->trace_len && i < 8000; i++) {
                if (len + 3 > alloc)
                    break;
                if (trace[i].timestamp < next)
                    continue;
                if (trace[i].flags.on_ground)
                    continue;
                if (!trace[i].flags.altitude_valid)
                    continue;

                buffer[len++] = trace[i].lat;
                buffer[len++] = trace[i].lon;
                buffer[len++] = trace[i].altitude;

                next = trace[i].timestamp + Modes.globe_history_heatmap;

            }
        }
    }
#define mod 4096

    int l = 0;
    int done[mod];
    while (l < mod * 5 / 6) {
        int rnd = rand() % mod;
        if (done[rnd])
            continue;
        done[rnd] = 1;
        l++;
        for (unsigned k = rnd * 3; k < len; k += 3 * mod) {
            buffer2[len2++] = buffer[k];
            buffer2[len2++] = buffer[k+1];
            buffer2[len2++] = buffer[k+2];
        }
    }
    for (int i = 0; i < mod; i++) {
        if (done[i])
            continue;
        done[i] = 1;
        for (unsigned k = i * 3; k < len; k += 3 * mod) {
            buffer2[len2++] = buffer[k];
            buffer2[len2++] = buffer[k+1];
            buffer2[len2++] = buffer[k+2];
        }
    }
    gzFile gzfp = gzdopen(fd, "wb");
    gzbuffer(gzfp, 256 * 1024);
    gzsetparams(gzfp, 9, Z_DEFAULT_STRATEGY);
    if (gzwrite(gzfp, buffer2, len * sizeof(int32_t)) != (int) (len * sizeof(int32_t)))
        perror("gzwrite");
    gzclose(gzfp);
    len = 0;
}

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

#include "readsb.h"
#include "help.h"
#include "geomag.h"

#include <stdarg.h>

struct _Modes Modes;

static void backgroundTasks(void);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

//
// ============================= Utility functions ==========================
//
static void cleanup_and_exit(int code);

static void sigintHandler(int dummy) {
    MODES_NOTUSED(dummy);
    Modes.exit = 1; // Signal to threads that we are done

    pthread_cond_signal(&Modes.mainCond);

    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    log_with_timestamp("Caught SIGINT, shutting down..\n");
}

static void sigtermHandler(int dummy) {
    MODES_NOTUSED(dummy);
    Modes.exit = 1; // Signal to threads that we are done

    pthread_cond_signal(&Modes.mainCond);

    signal(SIGTERM, SIG_DFL); // reset signal handler - bit extra safety
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
        Modes.threadNumber[i] = i;
    }

    // Now initialise things that should not be 0/NULL to their defaults
    Modes.gain = MODES_MAX_GAIN;
    Modes.freq = MODES_DEFAULT_FREQ;
    Modes.check_crc = 1;
    Modes.net_heartbeat_interval = MODES_NET_HEARTBEAT_INTERVAL;
    Modes.db_file = strdup("/usr/local/share/tar1090/git-db/aircraft.csv.gz");
    Modes.net_input_raw_ports = strdup("0");
    Modes.net_output_raw_ports = strdup("0");
    Modes.net_output_sbs_ports = strdup("0");
    Modes.net_input_sbs_ports = strdup("0");
    Modes.net_input_beast_ports = strdup("0");
    Modes.net_output_beast_ports = strdup("0");
    Modes.net_output_beast_reduce_ports = strdup("0");
    Modes.net_output_beast_reduce_interval = 125;
    Modes.net_output_vrs_ports = strdup("0");
    Modes.net_output_vrs_interval = 5 * SECONDS;
    Modes.net_output_json_ports = strdup("0");
    Modes.net_output_api_ports = strdup("0");
    Modes.net_input_jaero_ports = strdup("0");
    Modes.net_output_jaero_ports = strdup("0");
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
    Modes.net_output_flush_size = 1280; // Default to 1280 Bytes
    Modes.net_output_flush_interval = 50; // Default to 50 ms
    Modes.netReceiverId = 0;
    Modes.netIngest = 0;
    Modes.uuidFile = strdup("/boot/adsbx-uuid");
    Modes.json_trace_interval = 30 * 1000;
    Modes.heatmap_current_interval = -15;
    Modes.heatmap_interval = 60 * SECONDS;
    Modes.json_reliable = -13;

    Modes.cpr_focus = 0xc0ffeeba;
    //Modes.cpr_focus = 0x43BF95;
    //
    //Modes.receiver_focus = 0x1aa14156975948af;
    //
    Modes.show_only = 0xc0ffeeba; // default to out of normal range value
    Modes.trackExpireJaero = TRACK_EXPIRE_JAERO;

    sdrInitConfig();

    reset_stats(&Modes.stats_current);
    for (int i = 0; i < 90; ++i) {
        reset_stats(&Modes.stats_10[i]);
    }
    //receiverTest();
    Modes.scratch = malloc(sizeof(struct aircraft));
}
//
//=========================================================================
//
static void modesInit(void) {

    if (Modes.json_reliable == -13) {
        if (Modes.json_globe_index || Modes.globe_history_dir)
            Modes.json_reliable = 2;
        else if (Modes.bUserFlags & MODES_USER_LATLON_VALID)
            Modes.json_reliable = 1;
        else
            Modes.json_reliable = 2;
    }
    if (Modes.net_output_flush_interval < 5)
        Modes.net_output_flush_interval = 5;
    if (Modes.net_output_flush_interval > 500)
        Modes.net_output_flush_interval = 500;

    Modes.filter_persistence += Modes.json_reliable - 1;

    uint64_t now = mstime();
    Modes.next_stats_update = now + 10 * SECONDS;
    Modes.next_stats_display = now + Modes.stats;

    pthread_mutex_init(&Modes.mainMutex, NULL);
    pthread_cond_init(&Modes.mainCond, NULL);

    pthread_mutex_init(&Modes.data_mutex, NULL);
    pthread_cond_init(&Modes.data_cond, NULL);

    pthread_mutex_init(&Modes.decodeMutex, NULL);
    pthread_cond_init(&Modes.decodeCond, NULL);

    pthread_mutex_init(&Modes.jsonGlobeMutex, NULL);
    pthread_cond_init(&Modes.jsonGlobeCond, NULL);

    pthread_mutex_init(&Modes.jsonMutex, NULL);
    pthread_cond_init(&Modes.jsonCond, NULL);

    pthread_mutex_init(&Modes.miscMutex, NULL);
    pthread_cond_init(&Modes.miscCond, NULL);

    for (int i = 0; i < TRACE_THREADS; i++) {
        pthread_mutex_init(&Modes.jsonTraceMutex[i], NULL);
        pthread_cond_init(&Modes.jsonTraceCond[i], NULL);
    }
    for (int i = 0; i < STALE_THREADS; i++) {
        pthread_mutex_init(&Modes.staleMutex[i], NULL);
        pthread_cond_init(&Modes.staleCond[i], NULL);

        pthread_mutex_init(&Modes.staleDoneMutex[i], NULL);
        pthread_cond_init(&Modes.staleDoneCond[i], NULL);
    }

    for (int i = 0; i <= GLOBE_MAX_INDEX; i++) {
        ca_init(&Modes.globeLists[i]);
    }

    geomag_init();

    Modes.sample_rate = (double)2400000.0;

    // Allocate the various buffers used by Modes
    Modes.trailing_samples = (MODES_PREAMBLE_US + MODES_LONG_MSG_BITS + 16) * 1e-6 * Modes.sample_rate;

    if (Modes.sdr_type == SDR_NONE) {
        if (Modes.net)
            Modes.net_only = 1;
        if (!Modes.net_only) {
            fprintf(stderr, "No networking or SDR input selected, exiting!\n");
            cleanup_and_exit(1);
        }
    } else if (Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS) {
        Modes.net_only = 1;
    } else {
        Modes.net_only = 0;
    }

    if (!Modes.net_only) {
        for (int i = 0; i < MODES_MAG_BUFFERS; ++i) {
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
    if ((Modes.fUserLat != 0.0) || (Modes.fUserLon != 0.0) || Modes.bUserFlags & MODES_USER_LATLON_VALID) {
        Modes.bUserFlags |= MODES_USER_LATLON_VALID;
        fprintf(stderr, "Using lat: %9.4f, lon: %9.4f\n", Modes.fUserLat, Modes.fUserLon);
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

    if (Modes.api) {
        Modes.byLat = malloc(API_INDEX_MAX * sizeof(struct av));
        Modes.byLon = malloc(API_INDEX_MAX * sizeof(struct av));
    }

    // Prepare error correction tables
    modesChecksumInit(Modes.nfix_crc);
    icaoFilterInit();
    modeACInit();

    icaoFilterAdd(Modes.show_only);

    Modes.json_globe_special_tiles = calloc(GLOBE_SPECIAL_INDEX, sizeof(struct tile));
    init_globe_index(Modes.json_globe_special_tiles);
}

//
//=========================================================================
//
// We read data using a thread, so the main thread only handles decoding
// without caring about data acquisition
//
static void *readerThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

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
    srandom(get_seed());

    uint64_t sleep_ms = Modes.json_interval;

    uint64_t next_history = mstime();

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    pthread_mutex_lock(&Modes.jsonMutex);

    writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson());

    while (!Modes.exit) {

        struct timespec start_time;
        start_cpu_timing(&start_time);

        uint64_t now = mstime();

        struct char_buffer cb = generateAircraftJson();
        if (Modes.json_gzip)
            writeJsonToGzip(Modes.json_dir, "aircraft.json.gz", cb, 5);
        writeJsonToFile(Modes.json_dir, "aircraft.json", cb);

        if ((ALL_JSON) && now >= next_history) {
            char filebuf[PATH_MAX];

            snprintf(filebuf, PATH_MAX, "history_%d.json", Modes.json_aircraft_history_next);
            writeJsonToFile(Modes.json_dir, filebuf, generateAircraftJson());

            if (!Modes.json_aircraft_history_full) {
                writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()); // number of history entries changed
                if (Modes.json_aircraft_history_next == HISTORY_SIZE - 1)
                    Modes.json_aircraft_history_full = 1;
            }

            Modes.json_aircraft_history_next = (Modes.json_aircraft_history_next + 1) % HISTORY_SIZE;
            next_history = now + HISTORY_INTERVAL;
        }

        end_cpu_timing(&start_time, &Modes.stats_current.aircraft_json_cpu);

        incTimedwait(&ts, sleep_ms);

        int err = pthread_cond_timedwait(&Modes.jsonCond, &Modes.jsonMutex, &ts);
        if (err && err != ETIMEDOUT)
            fprintf(stderr, "json thread: pthread_cond_timedwait unexpected error: %s\n", strerror(err));
    }

    pthread_mutex_unlock(&Modes.jsonMutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}

static void *jsonGlobeThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    int part = 0;
    int writeJson = 0;
    int n_parts = 4; // power of 2

    uint64_t sleep_ms = Modes.json_interval / n_parts / 2;
    // write globe binCraft at double speed, globe json at normal speed

    pthread_mutex_lock(&Modes.jsonGlobeMutex);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {
        char filename[32];
        struct timespec start_time;
        start_cpu_timing(&start_time);

        if (part == 0)
            writeJson = !writeJson;

        for (int i = 0; i <= GLOBE_MAX_INDEX; i++) {
            if (i == Modes.specialTileCount)
                i = GLOBE_MIN_INDEX;

            if (i % n_parts != part)
                continue;

            if (i >= GLOBE_MIN_INDEX) {
                int index_index = globe_index_index(i);
                if (index_index!= i) {

                    if (index_index >= GLOBE_MIN_INDEX) {
                        fprintf(stderr, "weird globe index: %d\n", i);
                    }
                    continue;
                }
            }

            snprintf(filename, 31, "globe_%04d.binCraft", i);
            struct char_buffer cb2 = generateGlobeBin(i, 0);
            writeJsonToGzip(Modes.json_dir, filename, cb2, 5);
            free(cb2.buffer);

            snprintf(filename, 31, "globeMil_%04d.binCraft", i);
            struct char_buffer cb3 = generateGlobeBin(i, 1);
            writeJsonToGzip(Modes.json_dir, filename, cb3, 2);
            free(cb3.buffer);

            if (!Modes.jsonBinCraft && writeJson) {
                snprintf(filename, 31, "globe_%04d.json", i);
                struct char_buffer cb = generateGlobeJson(i);
                writeJsonToGzip(Modes.json_dir, filename, cb, 2);
                free(cb.buffer);
            }
        }

        part++;
        part %= n_parts;
        end_cpu_timing(&start_time, &Modes.stats_current.globe_json_cpu);

        incTimedwait(&ts, sleep_ms);

        int err = pthread_cond_timedwait(&Modes.jsonGlobeCond, &Modes.jsonGlobeMutex, &ts);
        if (err && err != ETIMEDOUT)
            fprintf(stderr, "jsonGlobeThread: pthread_cond_timedwait unexpected error: %s\n", strerror(err));
    }

    pthread_mutex_unlock(&Modes.jsonGlobeMutex);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}

static void *decodeThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    pthread_mutex_lock(&Modes.decodeMutex);

    modesInitNet();

    /* If the user specifies --net-only, just run in order to serve network
     * clients without reading data from the RTL device.
     * This rules also in case a local Mode-S Beast is connected via USB.
     */

    fprintf(stderr, "startup complete after %.3f seconds.\n", (mstime() - Modes.startup_time) / 1000.0);

    interactiveInit();

    if (Modes.net_only) {
        uint32_t maxSleep = Modes.net_output_flush_interval / 2; // in ms
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        while (!Modes.exit) {
            struct timespec start_time;
            struct timespec watch;

            startWatch(&watch);
            start_cpu_timing(&start_time);

            backgroundTasks();

            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);
            int64_t elapsed = stopWatch(&watch);

            static uint64_t antiSpam;
            if (elapsed > 100 && mstime() > antiSpam + 30 * SECONDS) {
                antiSpam = mstime();
                fprintf(stderr, "<3>High load: net work took %"PRId64" ms, suppressing for 30 seconds!\n", elapsed);
            }
            //fprintf(stderr, "net %"PRId64" ms\n", elapsed);

            incTimedwait(&ts, maxSleep);

            int err = pthread_cond_timedwait(&Modes.decodeCond, &Modes.decodeMutex, &ts);
            if (err && err != ETIMEDOUT)
                fprintf(stderr, "decode: pthread_cond_timedwait unexpected error: %s\n", strerror(err));
        }
    } else {
        int watchdogCounter = 50; // about 5 seconds

        uint32_t maxSleep = 80; // in ms
        // Create the thread that will read the data from the device.
        pthread_mutex_lock(&Modes.data_mutex);
        pthread_create(&Modes.reader_thread, NULL, readerThreadEntryPoint, NULL);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        while (!Modes.exit) {
            struct timespec start_time;

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
                watchdogCounter = 50;
            } else {
                // Nothing to process this time around.
                pthread_mutex_unlock(&Modes.data_mutex);
                if (--watchdogCounter <= 0) {
                    log_with_timestamp("No data received from the SDR for a long time, it may have wedged, exiting!");
                    Modes.exit = 1;
                    sdrCancel();
                }
            }

            start_cpu_timing(&start_time);
            backgroundTasks();
            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);

            pthread_mutex_lock(&Modes.data_mutex);

            uint64_t now = mstime();

            if (Modes.first_free_buffer == Modes.first_filled_buffer || now > Modes.next_remove_stale + 1 * SECONDS) {
                /* wait for more data.
                 * we should be getting data every 50-60ms. wait for max 80 before we give up and do some background work.
                 * this is fairly aggressive as all our network I/O runs out of the background work!
                 */

                // make sure we go to removeStale and the other functions
                // in trackPeriodicUpdate that require the decodeMutex
                // (the latest if when we are 2 seconds late, use a short wait
                // that should be sufficient to make the other thread get the lock)
                if (now > Modes.next_remove_stale + 1 * SECONDS) {
                    incTimedwait(&ts, 3);
                } else {
                    incTimedwait(&ts, maxSleep);
                }

                pthread_mutex_unlock(&Modes.decodeMutex);

                // This unlocks Modes.data_mutex, and waits for Modes.data_cond
                int err = pthread_cond_timedwait(&Modes.data_cond, &Modes.data_mutex, &ts);
                if (err && err != ETIMEDOUT)
                    fprintf(stderr, "decode: pthread_cond_timedwait unexpected error: %s\n", strerror(err));

                pthread_mutex_lock(&Modes.decodeMutex);
            }
        }

        pthread_mutex_unlock(&Modes.data_mutex);

        log_with_timestamp("Waiting for receive thread termination");
        int err;
        int count = 100;
        // Wait on reader thread exit
        while (count-- > 0 && (err = pthread_tryjoin_np(Modes.reader_thread, NULL))) {
            msleep(100);
        }
        if (err) {
            log_with_timestamp("Receive thread termination failed, will raise SIGKILL on exit!");
            Modes.exit = SIGKILL;
        } else {
            pthread_cond_destroy(&Modes.data_cond); // Thread cleanup - only after the reader thread is dead!
            pthread_mutex_destroy(&Modes.data_mutex);
        }
    }

    pthread_mutex_unlock(&Modes.decodeMutex);

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
    static uint64_t next_second;

    if (Modes.net) {
        modesNetPeriodicWork();
    }

    uint64_t now = mstime();

    icaoFilterExpire(now);

    if (now > next_second) {
        next_second = now + 1000;

        if (Modes.net)
            modesNetSecondWork();
    }

    // Refresh screen when in interactive mode
    if (Modes.interactive) {
        interactiveShowData();
    }

}

//=========================================================================
// Clean up memory prior to exit.
static void cleanup_and_exit(int code) {
    Modes.exit = 1;
    // Free any used memory
    geomag_destroy();
    interactiveCleanup();
    free(Modes.scratch);
    free(Modes.dev_name);
    free(Modes.filename);
    free(Modes.byLat);
    free(Modes.byLon);
    free(Modes.prom_file);
    free(Modes.json_dir);
    free(Modes.globe_history_dir);
    free(Modes.heatmap_dir);
    free(Modes.state_dir);
    free(Modes.globalStatsCount.rssi_table);
    free(Modes.net_bind_address);
    free(Modes.db_file);
    free(Modes.net_input_beast_ports);
    free(Modes.net_output_beast_ports);
    free(Modes.net_output_beast_reduce_ports);
    free(Modes.net_output_vrs_ports);
    free(Modes.net_input_raw_ports);
    free(Modes.net_output_raw_ports);
    free(Modes.net_output_sbs_ports);
    free(Modes.net_input_sbs_ports);
    free(Modes.net_input_jaero_ports);
    free(Modes.net_output_jaero_ports);
    free(Modes.net_output_json_ports);
    free(Modes.net_output_api_ports);
    free(Modes.beast_serial);
    free(Modes.json_globe_special_tiles);
    free(Modes.uuidFile);
    free(Modes.dbIndex);
    free(Modes.db);
    /* Go through tracked aircraft chain and free up any used memory */
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        struct aircraft *a = Modes.aircraft[j], *na;
        while (a) {
            na = a->next;
            if (a) {

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
static int make_net_connector(char *arg) {
    if (!Modes.net_connectors || Modes.net_connectors_count + 1 > Modes.net_connectors_size) {
        Modes.net_connectors_size = Modes.net_connectors_count * 2 + 8;
        Modes.net_connectors = realloc(Modes.net_connectors,
                sizeof(struct net_connector *) * Modes.net_connectors_size);
        if (!Modes.net_connectors) {
            fprintf(stderr, "realloc error net_connectors\n");
            exit(1);
        }
    }
    struct net_connector *con = calloc(1, sizeof(struct net_connector));
    Modes.net_connectors[Modes.net_connectors_count++] = con;
    char *connect_string = strdup(arg);
    con->address = con->address0 = strtok(connect_string, ",");
    con->port = con->port0 = strtok(NULL, ",");
    con->protocol = strtok(NULL, ",");
    con->address1 = strtok(NULL, ",");
    con->port1 = strtok(NULL, ",");

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
            && strcmp(con->protocol, "sbs_out_replay") != 0
            && strcmp(con->protocol, "sbs_out_mlat") != 0
            && strcmp(con->protocol, "sbs_out_jaero") != 0
            && strcmp(con->protocol, "sbs_out_prio") != 0
            && strcmp(con->protocol, "json_out") != 0
       ) {
        fprintf(stderr, "--net-connector: Unknown protocol: %s\n", con->protocol);
        fprintf(stderr, "Supported protocols: beast_out, beast_in, beast_reduce_out, raw_out, raw_in, \n"
                "sbs_out, sbs_out_replay, sbs_out_mlat, sbs_out_jaero, \n"
                "sbs_in, sbs_in_mlat, sbs_in_jaero, \n"
                "vrs_out, json_out\n");
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
    return 0;
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
            Modes.net_only = 1;
            break;
        case OptQuiet:
            Modes.quiet = 1;
            break;
        case OptNoInteractive:
            Modes.interactive = 0;
            if (Modes.viewadsb)
                Modes.quiet = 0;
            break;
        case OptShowOnly:
            Modes.show_only = (uint32_t) strtoul(arg, NULL, 16);
            Modes.interactive = 0;
            Modes.quiet = 1;
            Modes.cpr_focus = Modes.show_only;
            break;
        case OptFilterDF:
            Modes.filterDF = (uint8_t) strtoul(arg, NULL, 10);
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
        case OptPromFile:
            Modes.prom_file = strdup(arg);
            break;
        case OptJsonDir:
            Modes.json_dir = strdup(arg);
            break;
        case OptHeatmap:
            Modes.heatmap = 1;
            if (atof(arg) > 0)
                Modes.heatmap_interval = 1000 * atof(arg);
            break;
        case OptHeatmapDir:
            Modes.heatmap_dir = strdup(arg);
            break;
        case OptGlobeHistoryDir:
            Modes.globe_history_dir = strdup(arg);
            if (!Modes.state_dir) {
                Modes.state_dir = malloc(PATH_MAX);
                snprintf(Modes.state_dir, PATH_MAX, "%s/internal_state", Modes.globe_history_dir);
            }
            break;
        case OptStateDir:
            if (Modes.state_dir)
                free(Modes.state_dir);
            Modes.state_dir = strdup(arg);
            break;
        case OptJsonTime:
            Modes.json_interval = (uint64_t) (1000 * atof(arg));
            if (Modes.json_interval < 100) // 0.1s
                Modes.json_interval = 100;
            break;
        case OptJsonLocAcc:
            Modes.json_location_accuracy = atoi(arg);
            break;

        case OptJaeroTimeout:
            Modes.trackExpireJaero = (uint32_t) (atof(arg) * MINUTES);
            break;
        case OptJsonReliable:
            Modes.json_reliable = atoi(arg);
            if (Modes.json_reliable < -1)
                Modes.json_reliable = -1;
            if (Modes.json_reliable > 4)
                Modes.json_reliable = 4;
            break;
        case OptDbFile:
            free(Modes.db_file);
            Modes.db_file = strdup(arg);
            break;
        case OptJsonGzip:
            Modes.json_gzip = 1;
            break;
        case OptJsonBinCraft:
            Modes.jsonBinCraft = atoi(arg);
            break;
        case OptJsonTraceInt:
            if (atof(arg) > 0)
                Modes.json_trace_interval = 1000 * atof(arg);
            break;
        case OptJsonGlobeIndex:
            Modes.json_globe_index = 1;
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
        case OptNetApiPorts:
            free(Modes.net_output_api_ports);
            Modes.net_output_api_ports = strdup(arg);
            Modes.api = 1;
            break;
        case OptNetSbsInPorts:
            free(Modes.net_input_sbs_ports);
            Modes.net_input_sbs_ports = strdup(arg);
            break;
        case OptNetJaeroPorts:
            free(Modes.net_output_jaero_ports);
            Modes.net_output_jaero_ports = strdup(arg);
            break;
        case OptNetJaeroInPorts:
            free(Modes.net_input_jaero_ports);
            Modes.net_input_jaero_ports = strdup(arg);
            break;
        case OptNetVRSPorts:
            free(Modes.net_output_vrs_ports);
            Modes.net_output_vrs_ports = strdup(arg);
            break;
        case OptNetVRSInterval:
            if (atof(arg) > 0)
                Modes.net_output_vrs_interval = atof(arg) * SECONDS;
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
        case OptNetReceiverIdJson:
            Modes.netReceiverIdJson = 1;
            break;
        case OptGarbage:
            Modes.garbage_ports = strdup(arg);
            break;
        case OptNetIngest:
            Modes.netIngest = 1;
            break;
        case OptUuidFile:
            free(Modes.uuidFile);
            Modes.uuidFile = strdup(arg);
            break;
        case OptNetConnector:
            if (make_net_connector(arg))
                return 1;
            break;
        case OptNetConnectorDelay:
            Modes.net_connector_delay = (uint64_t) 1000 * atof(arg);
            break;

        case OptCprFocus:
            Modes.cpr_focus = strtol(arg, NULL, 16);
            Modes.interactive = 0;
            Modes.keep_traces = 2 * HOURS;
            fprintf(stderr, "cpr_focus = %06x\n", Modes.cpr_focus);
            break;
        case OptReceiverFocus:
            Modes.receiver_focus = strtoull(arg, NULL, 16);
            fprintf(stderr, "receiver_focus = %016"PRIx64"\n", Modes.receiver_focus);
            break;

        case OptDebug:
            while (*arg) {
                switch (*arg) {
                    case 'n': Modes.debug_net = 1;
                        break;
                    case 'P': Modes.debug_cpr = 1;
                        break;
                    case 'R': Modes.debug_receiver = 1;
                        break;
                    case 'S': Modes.debug_speed_check = 1;
                        break;
                    case 'G': Modes.debug_garbage = 1;
                        break;
                    case 't': Modes.debug_traceAlloc = 1;
                        break;
                    case 'T': Modes.debug_traceCount = 1;
                        break;
                    case 'K': Modes.debug_sampleCounter = 1;
                        break;
                    case 'O': Modes.debug_rough_receiver_location = 1;
                        break;
                    case 'U': Modes.debug_dbJson = 1;
                        break;
                    default:
                        fprintf(stderr, "Unknown debugging flag: %c\n", *arg);
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

int parseCommandLine(int argc, char **argv) {
    // check if we are running as viewadsb and set according behaviour
    int argv0Len = strlen(argv[0]);
    if (argv0Len >= 8 && !strcmp(argv[0] + (argv0Len - 8), "viewadsb")) {
        Modes.viewadsb = 1;
        Modes.net = 1;
        Modes.sdr_type = SDR_NONE;
        Modes.net_only = 1;
        Modes.interactive = 1;
        Modes.quiet = 1;
        Modes.net_connector_delay = 5 * 1000;
        // let this get overwritten in case the command line specifies a net-connector
        make_net_connector("127.0.0.1,30005,beast_in");
        Modes.net_connectors_count--;
        // we count it back up if it's still zero after the arg parse so it's used
    }

    // This is a little silly, but that's how the preprocessor works..
#define _stringize(x) #x

    const char *doc = "readsb Mode-S/ADSB/TIS Receiver   "
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
        "";
#undef _stringize
#undef verstring

    if (Modes.viewadsb) {
        doc = "vieadsb Mode-S/ADSB/TIS commandline viewer   ";
    }


    struct argp_option *options = Modes.viewadsb ? optionsViewadsb : optionsReadsb;

    const char args_doc[] = "";
    struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};


    if (argp_parse(&argp, argc, argv, ARGP_NO_EXIT, 0, 0)) {
        fprintf(stderr, "Command line used:\n");
        for (int i = 0; i < argc; i++) {
            fprintf(stderr, "%s ", argv[i]);
        }
        fprintf(stderr, "\n");
        cleanup_and_exit(1);
    }
    if (argc >= 2 && (
                !strcmp(argv[1], "--help")
                || !strcmp(argv[1], "--usage")
                || !strcmp(argv[1], "--version")
                || !strcmp(argv[1], "-V")
                || !strcmp(argv[1], "-?")
                )
       ) {
        exit(0);
    }

    return 0;
}

static void configAfterParse() {
    Modes.trackExpireMax = Modes.trackExpireJaero + TRACK_EXPIRE_LONG + 1 * MINUTES;

    if (Modes.viewadsb && Modes.net_connectors_count == 0) {
        Modes.net_connectors_count++; // activate the default net-connector for viewadsb
    }

    if (Modes.heatmap) {
        if (!Modes.globe_history_dir && !Modes.heatmap_dir) {
            fprintf(stderr, "Heatmap requires globe history dir or heatmap dir to be set, disabling heatmap!\n");
            Modes.heatmap = 0;
        }
    }

    if (Modes.json_globe_index) {
        Modes.keep_traces = 24 * HOURS + 40 * MINUTES; // include 40 minutes overlap, tar1090 needs at least 30 minutes currently
    } else if (Modes.heatmap) {
        Modes.keep_traces = 35 * MINUTES; // heatmap is written every 30 minutes
    }
}

//
//=========================================================================
//

int main(int argc, char **argv) {
    // Set sane defaults
    modesInitConfig();

    Modes.startup_time = mstime();
    srandom(get_seed());

    // signal handlers:
    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigtermHandler);
    signal(SIGUSR1, SIG_IGN);

    // Parse the command line options
    parseCommandLine(argc, argv);

    configAfterParse();

#ifdef _WIN32
    // Try to comply with the Copyright license conditions for binary distribution
    if (!Modes.quiet) {
        showCopyright();
    }
#endif

    // Initialization
    log_with_timestamp("%s starting up.", MODES_READSB_VARIANT);
    fprintf(stderr, VERSION_STRING"\n");
    //fprintf(stderr, "%zu\n", sizeof(struct state_flags));
    fprintf(stderr, "struct sizes: %zu, ", sizeof(struct aircraft));
    fprintf(stderr, "%zu, ", sizeof(struct state));
    fprintf(stderr, "%zu, ", sizeof(struct state_all));
    fprintf(stderr, "%zu\n", sizeof(struct binCraft));
    //fprintf(stderr, "%zu\n", sizeof(struct modesMessage));
    //fprintf(stderr, "%zu\n", sizeof(pthread_mutex_t));
    //fprintf(stderr, "%zu\n", 10000 * sizeof(struct aircraft));
    modesInit();

    if (Modes.sdr_type != SDR_NONE && !sdrOpen()) {
        cleanup_and_exit(1);
    }

    // init stats:
    Modes.stats_current.start = Modes.stats_current.end =
            Modes.stats_alltime.start = Modes.stats_alltime.end =
            Modes.stats_periodic.start = Modes.stats_periodic.end =
            Modes.stats_1min.start = Modes.stats_1min.end =
            Modes.stats_5min.start = Modes.stats_5min.end =
            Modes.stats_15min.start = Modes.stats_15min.end = mstime();

    for (int j = 0; j < STAT_BUCKETS; ++j)
        Modes.stats_10[j].start = Modes.stats_10[j].end = Modes.stats_current.start;

    if (Modes.json_dir && Modes.json_globe_index) {
        char pathbuf[PATH_MAX];
        snprintf(pathbuf, PATH_MAX, "%s/traces", Modes.json_dir);
        mkdir(pathbuf, 0755);
        for (int i = 0; i < 256; i++) {
            snprintf(pathbuf, PATH_MAX, "%s/traces/%02x", Modes.json_dir, i);
            mkdir(pathbuf, 0755);
        }
    }

    checkNewDay(mstime());

    if (Modes.state_dir) {
        fprintf(stderr, "loading state .....\n");
        struct timespec watch;
        startWatch(&watch);
        pthread_t threads[IO_THREADS];
        int numbers[IO_THREADS];
        for (int i = 0; i < IO_THREADS; i++) {
            numbers[i] = i;
            pthread_create(&threads[i], NULL, load_blobs, &numbers[i]);
        }
        for (int i = 0; i < IO_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        uint64_t aircraftCount = 0; // includes quite old aircraft, just for checking hash table fill
        for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
            for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
                aircraftCount++;
            }
        }
        Modes.aircraftCount = aircraftCount;

        double elapsed = stopWatch(&watch) / 1000.0;
        fprintf(stderr, " .......... done, loaded %llu aircraft in %.3f seconds!\n", (unsigned long long) aircraftCount, elapsed);
        fprintf(stderr, "aircraft table fill: %0.1f\n", aircraftCount / (double) AIRCRAFT_BUCKETS );

        char pathbuf[PATH_MAX];

        if (Modes.globe_history_dir && mkdir(Modes.globe_history_dir, 0755) && errno != EEXIST) {
            perror(Modes.globe_history_dir);
        }

        if (mkdir(Modes.state_dir, 0755) && errno != EEXIST) {
            perror(pathbuf);
        }
    }
    // db update on startup
    if (!Modes.exit)
        dbUpdate();
    if (!Modes.exit)
        dbFinishUpdate();

    for (int thread = 0; thread < STALE_THREADS; thread++) {
        Modes.staleRun[thread] = 1;
        pthread_mutex_lock(&Modes.staleDoneMutex[thread]);
        pthread_create(&Modes.staleThread[thread], NULL, staleThreadEntryPoint, &Modes.threadNumber[thread]);
        while (Modes.staleRun[thread]) {
            pthread_cond_wait(&Modes.staleDoneCond[thread], &Modes.staleDoneMutex[thread]);
        }
    }

    pthread_create(&Modes.decodeThread, NULL, decodeThreadEntryPoint, NULL);

    pthread_create(&Modes.miscThread, NULL, miscThreadEntryPoint, NULL);

    if (Modes.json_dir && Modes.json_globe_index) {
        for (int i = 0; i < TRACE_THREADS; i++) {
            pthread_create(&Modes.jsonTraceThread[i], NULL, jsonTraceThreadEntryPoint, &Modes.threadNumber[i]);
        }
    }

    if (Modes.json_dir) {
        pthread_create(&Modes.jsonThread, NULL, jsonThreadEntryPoint, NULL);

        if (Modes.json_globe_index) {
            // globe_xxxx.json
            pthread_create(&Modes.jsonGlobeThread, NULL, jsonGlobeThreadEntryPoint, NULL);
        }
    }

    pthread_mutex_lock(&Modes.mainMutex);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {
        trackPeriodicUpdate();

        incTimedwait(&ts, PERIODIC_UPDATE);

        //fprintf(stderr, "%.1f\n", ts.tv_nsec / 1e6);

        int err = pthread_cond_timedwait(&Modes.mainCond, &Modes.mainMutex, &ts);
        if (err && err != ETIMEDOUT)
            fprintf(stderr, "main thread: pthread_cond_timedwait unexpected error: %s\n", strerror(err));
    }

    pthread_mutex_unlock(&Modes.mainMutex);

    if (Modes.json_dir) {

        pthread_mutex_lock(&Modes.jsonMutex);
        pthread_cond_signal(&Modes.jsonCond);
        pthread_mutex_unlock(&Modes.jsonMutex);
        pthread_join(Modes.jsonThread, NULL); // Wait on json writer thread exit

        char pathbuf[PATH_MAX];
        snprintf(pathbuf, PATH_MAX, "%s/receiver.json", Modes.json_dir);
        unlink(pathbuf);

        if (Modes.json_globe_index) {
            pthread_mutex_lock(&Modes.jsonGlobeMutex);
            pthread_cond_signal(&Modes.jsonGlobeCond);
            pthread_mutex_unlock(&Modes.jsonGlobeMutex);
            pthread_join(Modes.jsonGlobeThread, NULL); // Wait on json writer thread exit
        }
    }
    if (Modes.json_dir && Modes.json_globe_index) {
        for (int i = 0; i < TRACE_THREADS; i++) {
            pthread_mutex_lock(&Modes.jsonTraceMutex[i]);
            pthread_cond_signal(&Modes.jsonTraceCond[i]);
            pthread_mutex_unlock(&Modes.jsonTraceMutex[i]);
            pthread_join(Modes.jsonTraceThread[i], NULL);
        }
    }

    pthread_mutex_lock(&Modes.miscMutex);
    pthread_cond_signal(&Modes.miscCond);
    pthread_mutex_unlock(&Modes.miscMutex);
    pthread_join(Modes.miscThread, NULL);
    pthread_mutex_destroy(&Modes.miscMutex);

    pthread_mutex_lock(&Modes.decodeMutex);
    pthread_cond_signal(&Modes.decodeCond);
    pthread_mutex_unlock(&Modes.decodeMutex);

    pthread_mutex_lock(&Modes.data_mutex);
    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);

    pthread_join(Modes.decodeThread, NULL); // Wait on json writer thread exit

    // force stats to be done, this must happen before network cleanup as it checks network stuff
    Modes.next_stats_update = 0;
    trackPeriodicUpdate();
    // ------------

    /* Cleanup network setup */
    cleanupNetwork();

    if (Modes.state_dir) {
        fprintf(stderr, "saving state .....\n");
        struct timespec watch;
        startWatch(&watch);

        pthread_t threads[IO_THREADS];
        int numbers[IO_THREADS];
        for (int i = 0; i < IO_THREADS; i++) {
            numbers[i] = i;
            pthread_create(&threads[i], NULL, save_blobs, &numbers[i]);
        }
        for (int i = 0; i < IO_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        double elapsed = stopWatch(&watch) / 1000.0;
        fprintf(stderr, " .......... done, saved %llu aircraft in %.3f seconds!\n", (unsigned long long) Modes.aircraftCount, elapsed);
    }

    // stop stale threads and be careful about it
    Modes.staleStop = 1;
    for (int i = 0; i < STALE_THREADS; i++) {
        pthread_mutex_lock(&Modes.staleMutex[i]);
        pthread_cond_signal(&Modes.staleCond[i]);
        pthread_mutex_unlock(&Modes.staleDoneMutex[i]);
        pthread_mutex_unlock(&Modes.staleMutex[i]);

        pthread_join(Modes.staleThread[i], NULL);

        pthread_mutex_destroy(&Modes.staleMutex[i]);
        pthread_mutex_destroy(&Modes.staleDoneMutex[i]);
        pthread_cond_destroy(&Modes.staleCond[i]);
        pthread_cond_destroy(&Modes.staleDoneCond[i]);
        //fprintf(stderr, "%d\n", i);
    }


    pthread_mutex_destroy(&Modes.decodeMutex);
    pthread_mutex_destroy(&Modes.jsonMutex);
    pthread_mutex_destroy(&Modes.jsonGlobeMutex);
    pthread_cond_destroy(&Modes.decodeCond);
    pthread_cond_destroy(&Modes.jsonCond);
    pthread_cond_destroy(&Modes.jsonGlobeCond);
    for (int i = 0; i < TRACE_THREADS; i++) {
        pthread_mutex_destroy(&Modes.jsonTraceMutex[i]);
        pthread_cond_destroy(&Modes.jsonTraceCond[i]);
    }


    pthread_mutex_destroy(&Modes.mainMutex);
    pthread_cond_destroy(&Modes.mainCond);

    // If --stats were given, print statistics
    if (Modes.stats) {
        display_total_stats();
    }
    if (Modes.exit == SIGKILL) {
        raise(SIGKILL);
    }
    sdrClose();
    if (Modes.exit != 1) {
        log_with_timestamp("Abnormal exit.");
        cleanup_and_exit(1);
    }

    log_with_timestamp("Normal exit.");
    cleanup_and_exit(0);
}

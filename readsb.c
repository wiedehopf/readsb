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

#include <sys/time.h>
#include <sys/resource.h>

struct _Modes Modes;
struct _Threads Threads;

static void loadReplaceState();
static void checkReplaceState();
static void backgroundTasks(int64_t now);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

//
// ============================= Utility functions ==========================
//
static void cleanup_and_exit(int code);

void setExit(int arg) {
    // Signal to main loop that program is exiting soon, delay depends on cli arguments
    // see main loop for details

    Modes.exitSoon = arg;

    uint64_t one = 1;
    ssize_t res = write(Modes.exitSoonEventfd, &one, sizeof(one));
    MODES_NOTUSED(res);
}
static void sigintHandler(int dummy) {
    MODES_NOTUSED(dummy);
    setExit(1);

    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    log_with_timestamp("Caught SIGINT, shutting down...");
}

static void sigtermHandler(int dummy) {
    MODES_NOTUSED(dummy);
    setExit(1);

    signal(SIGTERM, SIG_DFL); // reset signal handler - bit extra safety
    log_with_timestamp("Caught SIGTERM, shutting down...");
}

void receiverPositionChanged(float lat, float lon, float alt) {
    log_with_timestamp("Autodetected receiver location: %.5f, %.5f at %.0fm AMSL", lat, lon, alt);

    if (Modes.json_dir) {
        free(writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()).buffer); // location changed
    }
}



//
// =============================== Initialization ===========================
//
static void configSetDefaults(void) {
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
    //Modes.db_file = strdup("/usr/local/share/tar1090/git-db/aircraft.csv.gz");
    Modes.db_file = NULL;
    Modes.net_input_raw_ports = strdup("0");
    Modes.net_output_raw_ports = strdup("0");
    Modes.net_output_sbs_ports = strdup("0");
    Modes.net_input_sbs_ports = strdup("0");
    Modes.net_input_beast_ports = strdup("0");
    Modes.net_output_beast_ports = strdup("0");
    Modes.net_output_beast_reduce_ports = strdup("0");
    Modes.net_output_beast_reduce_interval = 250;
    Modes.beast_reduce_filter_distance = -1;
    Modes.beast_reduce_filter_altitude = -1;
    Modes.net_output_vrs_ports = strdup("0");
    Modes.net_output_vrs_interval = 5 * SECONDS;
    Modes.net_output_json_ports = strdup("0");
    Modes.net_output_api_ports = strdup("0");
    Modes.net_input_jaero_ports = strdup("0");
    Modes.net_output_jaero_ports = strdup("0");
    Modes.net_connector_delay = 30 * 1000;
    Modes.interactive_display_ttl = MODES_INTERACTIVE_DISPLAY_TTL;
    Modes.json_interval = 1000;
    Modes.json_location_accuracy = 2;
    Modes.maxRange = 1852 * 450; // 450 nmi default max range
    Modes.nfix_crc = 1;
    Modes.biastee = 0;
    Modes.position_persistence = 4;
    Modes.net_sndbuf_size = 2; // Default to 256 kB SNDBUF / RCVBUF
    Modes.net_output_flush_size = 1280; // Default to 1280 Bytes
    Modes.net_output_flush_interval = 50; // Default to 50 ms
    Modes.netReceiverId = 0;
    Modes.netIngest = 0;
    Modes.uuidFile = strdup("/usr/local/share/adsbexchange/adsbx-uuid");
    Modes.json_trace_interval = 20 * 1000;
    Modes.heatmap_current_interval = -15;
    Modes.heatmap_interval = 60 * SECONDS;
    Modes.json_reliable = -13;
    Modes.acasFD1 = -1; // set to -1 so it's clear we don't have that fd
    Modes.acasFD2 = -1; // set to -1 so it's clear we don't have that fd

    Modes.currentTask = "unset";
    Modes.joinTimeout = 30 * SECONDS;

    Modes.state_chunk_size = 12 * 1024 * 1024;
    Modes.state_chunk_size_read = Modes.state_chunk_size;

    Modes.decodeThreads = 1;

    Modes.filterDF = 0;
    Modes.filterDFbitset = 0;
    Modes.cpr_focus = BADDR;
    Modes.leg_focus = BADDR;
    Modes.trace_focus = BADDR;
    Modes.show_only = BADDR;

    Modes.outline_json = 1; // enable by default
    Modes.range_outline_duration = 24 * HOURS;
    //Modes.receiver_focus = 0x123456;
    //
    Modes.trackExpireJaero = TRACK_EXPIRE_JAERO;

    Modes.fixDF = 1;

    sdrInitConfig();

    reset_stats(&Modes.stats_current);
    for (int i = 0; i < 90; ++i) {
        reset_stats(&Modes.stats_10[i]);
    }
    //receiverTest();

    struct rlimit limits;
    getrlimit(RLIMIT_NOFILE, &limits);
    Modes.max_fds = limits.rlim_cur;

    Modes.sdr_buf_size = 16 * 16 * 1024;

    // in seconds, default to 1 hour
    Modes.dump_interval = 60 * 60;


    Modes.ping_reduce = PING_REDUCE;
    Modes.ping_reject = PING_REJECT;

    Modes.binCraftVersion = 20220916;
    Modes.messageRateMult = 1.0f;

    Modes.apiShutdownDelay = 0 * SECONDS;

    Modes.aircraft_json_seen_by_list_timeout = 3;
}
//
//=========================================================================
//
static void modesInit(void) {

    int64_t now = mstime();
    Modes.next_stats_update = roundSeconds(10, 5, now + 10 * SECONDS);
    Modes.next_stats_display = now + Modes.stats_display_interval;

    pthread_mutex_init(&Modes.traceDebugMutex, NULL);
    pthread_mutex_init(&Modes.hungTimerMutex, NULL);

    threadInit(&Threads.reader, "reader");
    threadInit(&Threads.upkeep, "upkeep");
    threadInit(&Threads.decode, "decode");
    threadInit(&Threads.json, "json");
    threadInit(&Threads.globeJson, "globeJson");
    threadInit(&Threads.globeBin, "globeBin");
    threadInit(&Threads.misc, "misc");
    threadInit(&Threads.apiUpdate, "apiUpdate");

    if (Modes.json_globe_index || Modes.netReceiverId || AIRCRAFT_HASH_BITS > 16) {
        // to keep decoding and the other threads working well, don't use all available processors
        Modes.allPoolSize = imax(1, Modes.num_procs);
    } else {
        Modes.allPoolSize = 1;
    }

    Modes.allTasks = allocate_task_group(2 * Modes.allPoolSize);
    Modes.allPool = threadpool_create(Modes.allPoolSize, 4);

    for (int i = 0; i <= GLOBE_MAX_INDEX; i++) {
        ca_init(&Modes.globeLists[i]);
    }
    ca_init(&Modes.aircraftActive);

    geomag_init();

    Modes.sample_rate = (double)2400000.0;

    // Allocate the various buffers used by Modes
    Modes.trailing_samples = (unsigned)((MODES_PREAMBLE_US + MODES_LONG_MSG_BITS + 16) * 1e-6 * Modes.sample_rate);

    if (!Modes.net_only) {
        for (int i = 0; i < MODES_MAG_BUFFERS; ++i) {
            size_t alloc = (Modes.sdr_buf_samples + Modes.trailing_samples) * sizeof (uint16_t);
            if ((Modes.mag_buffers[i].data = cmalloc(alloc)) == NULL) {
                fprintf(stderr, "Out of memory allocating magnitude buffer.\n");
                exit(1);
            }
            memset(Modes.mag_buffers[i].data, 0, alloc);

            Modes.mag_buffers[i].length = 0;
            Modes.mag_buffers[i].dropped = 0;
            Modes.mag_buffers[i].sampleTimestamp = 0;
        }
    }

    // Prepare error correction tables
    modesChecksumInit(Modes.nfix_crc);
    icaoFilterInit();
    modeACInit();

    icaoFilterAdd(Modes.show_only);

    init_globe_index();

    quickInit();
}

static void lockThreads() {
    for (int i = 0; i < Modes.lockThreadsCount; i++) {
        //fprintf(stderr, "locking %s\n", Modes.lockThreads[i]->name);
        Modes.currentTask = Modes.lockThreads[i]->name;
        pthread_mutex_lock(&Modes.lockThreads[i]->mutex);
    }
}

static void unlockThreads() {
    for (int i = Modes.lockThreadsCount - 1; i >= 0; i--) {
        //fprintf(stderr, "unlocking %s\n", Modes.lockThreads[i]->name);
        pthread_mutex_unlock(&Modes.lockThreads[i]->mutex);
    }
}

static int64_t ms_until_priority() {
    int64_t now = mstime();
    int64_t mono = mono_milli_seconds();
    int64_t ms_until_run = imin(
            Modes.next_stats_update - now,
            Modes.next_remove_stale - mono
            );

    if (Modes.replace_state_inhibit_traces_until) {
        if (mono > Modes.replace_state_inhibit_traces_until) {
            Modes.replace_state_inhibit_traces_until = 0;
        } else if (ms_until_run > 100) {
            ms_until_run = 80;
        }
    }

    if (ms_until_run > 0 && Modes.replace_state_blob) {
        ms_until_run = 0;
    }
    return ms_until_run;
}

// function to check if priorityTasksRun() needs to run
int priorityTasksPending() {
    if (ms_until_priority() <= 0) {
        return 1; // something to do
    } else {
        return 0; // nothing to do
    }
}

//
// Entry point for periodic updates
//

void priorityTasksRun() {
    pthread_mutex_lock(&Modes.hungTimerMutex);
    startWatch(&Modes.hungTimer1);
    pthread_mutex_unlock(&Modes.hungTimerMutex);

    Modes.currentTask = "priorityTasks_start";

    // stop all threads so we can remove aircraft from the list.
    // adding aircraft does not need to be done with locking:
    // the worst case is that the newly added aircraft is skipped as it's not yet
    // in the cache used by the json threads.

    Modes.currentTask = "locking";
    lockThreads();
    Modes.currentTask = "locked";

    int64_t now = mstime();
    int64_t mono = mono_milli_seconds();

    int removed_stale = 0;

    static int64_t last_periodic_mono;
    int64_t periodic_interval = mono - last_periodic_mono;
    if (periodic_interval > 5 * SECONDS && last_periodic_mono && !(Modes.syntethic_now_suppress_errors && Modes.synthetic_now)) {
        fprintf(stderr, "<3> priorityTasksRun didn't run for %.1f seconds!\n", periodic_interval / 1000.0);
    }
    last_periodic_mono = mono;

    struct timespec watch;
    startWatch(&watch);
    struct timespec start_time;
    start_monotonic_timing(&start_time);
    struct timespec before = threadpool_get_cumulative_thread_time(Modes.allPool);

    if (Modes.replace_state_blob) {
        loadReplaceState();
        checkReplaceState();
    }

    // finish db update under lock
    if (dbFinishUpdate()) {
        // don't do trackRemoveStale at the same time as dbFinishUpdate
    } else if (mono >= Modes.next_remove_stale) {
        pthread_mutex_lock(&Modes.hungTimerMutex);
        startWatch(&Modes.hungTimer2);
        pthread_mutex_unlock(&Modes.hungTimerMutex);

        Modes.currentTask = "trackRemoveStale";

        trackRemoveStale(now);
        traceDelete();

        int64_t interval = mono - Modes.next_remove_stale;
        if (interval > 5 * SECONDS && Modes.next_remove_stale && !(Modes.syntethic_now_suppress_errors && Modes.synthetic_now)) {
            fprintf(stderr, "<3> removeStale didn't run for %.1f seconds!\n", interval / 1000.0);
        }

        Modes.next_remove_stale = mono + REMOVE_STALE_INTERVAL;
        removed_stale = 1;
    }

    int64_t elapsed1 = lapWatch(&watch);

    if (now >= Modes.next_stats_update) {
        Modes.updateStats = 1;
    }
    if (Modes.updateStats) {
        Modes.currentTask = "statsUpdate";
        statsUpdate(now); // needs to happen under lock
    }

    int64_t elapsed2 = lapWatch(&watch);

    Modes.currentTask = "unlocking";
    unlockThreads();
    Modes.currentTask = "unlocked";

    static int64_t antiSpam;
    if (0 || (Modes.debug_removeStaleDuration) || ((elapsed1 > 150 || elapsed2 > 150) && mono > antiSpam + 30 * SECONDS)) {
        fprintf(stderr, "<3>High load: removeStale took %"PRIi64"/%"PRIi64" ms! stats: %d (suppressing for 30 seconds)\n", elapsed1, elapsed2, Modes.updateStats);
        antiSpam = mono;
    }

    //fprintf(stderr, "running for %ld ms\n", mstime() - Modes.startup_time);
    //fprintf(stderr, "removeStale took %"PRIu64" ms, running for %ld ms\n", elapsed, now - Modes.startup_time);

    if (Modes.updateStats) {
        Modes.currentTask = "statsReset";
        statsResetCount();

        int64_t now = mstime();

        Modes.currentTask = "statsCount";
        statsCountAircraft(now);


        Modes.currentTask = "statsProcess";
        statsProcess(now);

        Modes.updateStats = 0;

        if (Modes.json_dir) {
            free(writeJsonToFile(Modes.json_dir, "status.json", generateStatusJson(now)).buffer);
            free(writeJsonToFile(Modes.json_dir, "status.prom", generateStatusProm(now)).buffer);
        }
    }

    end_monotonic_timing(&start_time, &Modes.stats_current.remove_stale_cpu);
    if (removed_stale) {
        struct timespec after = threadpool_get_cumulative_thread_time(Modes.allPool);
        timespec_add_elapsed(&before, &after, &Modes.stats_current.remove_stale_cpu);
    }
    Modes.currentTask = "priorityTasks_end";
}

//
//=========================================================================
//
// We read data using a thread, so the main thread only handles decoding
// without caring about data acquisition
//
static void *readerEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    if (!sdrOpen()) {
        setExit(2); // unexpected exit
        log_with_timestamp("sdrOpen() failed, exiting!");
        return NULL;
    }

    if (sdrHasRun()) {
        sdrRun();
        // Wake the main thread (if it's still waiting)
        if (!Modes.exit)
            setExit(2); // unexpected exit
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        pthread_mutex_lock(&Threads.reader.mutex);

        while (!Modes.exit) {
            threadTimedWait(&Threads.reader, &ts, 15 * SECONDS);
        }
        pthread_mutex_unlock(&Threads.reader.mutex);
    }

    sdrClose();

    return NULL;
}

static void *jsonEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    int64_t next_history = mstime();

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    pthread_mutex_lock(&Threads.json.mutex);

    threadpool_buffer_t pass_buffer = { 0 };
    threadpool_buffer_t zstd_buffer = { 0 };

    ZSTD_CCtx* cctx = ZSTD_createCCtx();

    while (!Modes.exit) {

        struct timespec start_time;
        start_cpu_timing(&start_time);

        int64_t now = mstime();

        // old direct creation, slower when creating json for an aircraft more than once
        //struct char_buffer cb = generateAircraftJson(0);

        if (Modes.onlyBin < 2) {
            // new way: use the apiBuffer of json fragments
            struct char_buffer cb = apiGenerateAircraftJson(&pass_buffer);
            if (Modes.json_gzip) {
                writeJsonToGzip(Modes.json_dir, "aircraft.json.gz", cb, 2);
            }
            writeJsonToFile(Modes.json_dir, "aircraft.json", cb);

            if ((ALL_JSON) && Modes.onlyBin < 2 && now >= next_history) {
                char filebuf[PATH_MAX];

                snprintf(filebuf, PATH_MAX, "history_%d.json", Modes.json_aircraft_history_next);
                writeJsonToFile(Modes.json_dir, filebuf, cb);

                if (!Modes.json_aircraft_history_full) {
                    free(writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()).buffer); // number of history entries changed
                    if (Modes.json_aircraft_history_next == HISTORY_SIZE - 1)
                        Modes.json_aircraft_history_full = 1;
                }

                Modes.json_aircraft_history_next = (Modes.json_aircraft_history_next + 1) % HISTORY_SIZE;
                next_history = now + HISTORY_INTERVAL;
            }
        }

        if (Modes.debug_recent) {
            struct char_buffer cb = generateAircraftJson(1 * SECONDS);
            writeJsonToFile(Modes.json_dir, "aircraft_recent.json", cb);
            sfree(cb.buffer);
        }

        struct char_buffer cb3 = generateAircraftBin(&pass_buffer);

        if (Modes.enableBinGz) {
            writeJsonToGzip(Modes.json_dir, "aircraft.binCraft", cb3, 1);
        }

        //fprintf(stderr, "uncompressed size %ld\n", (long) cb3.len);
        writeJsonToFile(Modes.json_dir, "aircraft.binCraft.zst", generateZstd(cctx, &zstd_buffer, cb3, 1));

        if (Modes.json_globe_index) {
            struct char_buffer cb2 = generateGlobeBin(-1, 1, &pass_buffer);
            if (Modes.enableBinGz) {
                writeJsonToGzip(Modes.json_dir, "globeMil_42777.binCraft", cb2, 1);
            }
            writeJsonToFile(Modes.json_dir, "globeMil_42777.binCraft.zst", ident(generateZstd(cctx, &zstd_buffer, cb2, 1)));
        }

        end_cpu_timing(&start_time, &Modes.stats_current.aircraft_json_cpu);

        // we should exit this wait early due to a cond_signal from api.c
        threadTimedWait(&Threads.json, &ts, Modes.json_interval * 3);
    }

    ZSTD_freeCCtx(cctx);
    free_threadpool_buffer(&zstd_buffer);
    free_threadpool_buffer(&pass_buffer);

    pthread_mutex_unlock(&Threads.json.mutex);

    return NULL;
}

static void *globeJsonEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    if (Modes.onlyBin > 0)
        return NULL;

    pthread_mutex_lock(&Threads.globeJson.mutex);

    threadpool_buffer_t pass_buffer = { 0 };

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    while (!Modes.exit) {
        struct timespec start_time;
        start_cpu_timing(&start_time);

        for (int j = 0; j <= Modes.json_globe_indexes_len; j++) {
            int index = Modes.json_globe_indexes[j];

            char filename[32];
            snprintf(filename, 31, "globe_%04d.json", index);
            struct char_buffer cb = apiGenerateGlobeJson(index, &pass_buffer);
            writeJsonToGzip(Modes.json_dir, filename, cb, 1);
        }

        end_cpu_timing(&start_time, &Modes.stats_current.globe_json_cpu);

        // we should exit this wait early due to a cond_signal from api.c
        threadTimedWait(&Threads.globeJson, &ts, Modes.json_interval * 3);
    }

    free_threadpool_buffer(&pass_buffer);

    pthread_mutex_unlock(&Threads.globeJson.mutex);
    return NULL;
}

static void *globeBinEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    int part = 0;
    int n_parts = 8; // power of 2

    int64_t sleep_ms = Modes.json_interval / n_parts;

    pthread_mutex_lock(&Threads.globeBin.mutex);

    threadpool_buffer_t pass_buffer = { 0 };
    threadpool_buffer_t zstd_buffer = { 0 };

    ZSTD_CCtx* cctx = ZSTD_createCCtx();

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {
        char filename[32];
        struct timespec start_time;
        start_cpu_timing(&start_time);

        for (int j = 0; j < Modes.json_globe_indexes_len; j++) {
            if (j % n_parts != part)
                continue;

            int index = Modes.json_globe_indexes[j];

            struct char_buffer cb2 = generateGlobeBin(index, 0, &pass_buffer);

            if (Modes.enableBinGz) {
                snprintf(filename, 31, "globe_%04d.binCraft", index);
                writeJsonToGzip(Modes.json_dir, filename, cb2, 1);
            }

            snprintf(filename, 31, "globe_%04d.binCraft.zst", index);
            writeJsonToFile(Modes.json_dir, filename, ident(generateZstd(cctx, &zstd_buffer, cb2, 1)));

            struct char_buffer cb3 = generateGlobeBin(index, 1, &pass_buffer);

            if (Modes.enableBinGz) {
                snprintf(filename, 31, "globeMil_%04d.binCraft", index);
                writeJsonToGzip(Modes.json_dir, filename, cb3, 1);
            }

            snprintf(filename, 31, "globeMil_%04d.binCraft.zst", index);
            writeJsonToFile(Modes.json_dir, filename, ident(generateZstd(cctx, &zstd_buffer, cb3, 1)));
        }

        part++;
        part %= n_parts;
        end_cpu_timing(&start_time, &Modes.stats_current.bin_cpu);

        threadTimedWait(&Threads.globeBin, &ts, sleep_ms);
    }

    ZSTD_freeCCtx(cctx);
    free_threadpool_buffer(&zstd_buffer);
    free_threadpool_buffer(&pass_buffer);

    pthread_mutex_unlock(&Threads.globeBin.mutex);

    return NULL;
}

static void timingStatistics(struct mag_buf *buf) {
    if (0) {
        static int64_t last;

        double elapsed = buf->sysMicroseconds - last;
        last = buf->sysMicroseconds;

        static double last_elapsed;
        // diff more than 2 ms:
        if (fabs(elapsed - last_elapsed) > 200) {
            fprintf(stderr, "time between USB transfers: %.0f us\n", elapsed);
            last_elapsed = elapsed;
        }
    }

    {
        static int64_t last_sys;
        static int64_t last_sample;
        if (!last_sys) {
            last_sys = buf->sysMicroseconds;
            last_sample = buf->sampleTimestamp;
        }
        double elapsed_sys = buf->sysMicroseconds - last_sys;
        if (elapsed_sys > 30 * SECONDS * 1000) {
            double elapsed_sample = buf->sampleTimestamp - last_sample;
            double freq_ratio = elapsed_sample / (elapsed_sys * 12.0);
            double diff_us = elapsed_sys - elapsed_sample / 12.0;
            double ppm = (freq_ratio - 1) * 1e6;
            // ignore the first 30 seconds for alerting purposes
            if (last_sample != 0) {
                Modes.estimated_ppm = ppm;
                if (fabs(ppm) > 600) {
                    if (ppm < -1000) {
                        int packets_lost = (int) nearbyint(ppm / -1820);
                        Modes.stats_current.samples_lost += packets_lost * Modes.sdr_buf_samples;
                        fprintf(stderr, "Lost %d packets (%.1f us) on USB, MLAT could be UNSTABLE, check sync! (ppm: %.0f)"
                                "(or the system clock jumped for some reason)\n", packets_lost, diff_us, ppm);
                    } else {
                        fprintf(stderr, "SDR ppm out of specification (could cause MLAT issues) or local clock jumped / not syncing with ntp or chrony! ppm: %.0f\n", ppm);
                    }
                }
            }
            last_sys = buf->sysMicroseconds;
            last_sample = buf->sampleTimestamp;
        }
    }
}

static void *decodeEntryPoint(void *arg) {
    // only go higher priority if we have multiple processors
    if (Modes.num_procs > 1 && Modes.num_procs > Modes.decodeThreads) {
        setPriorityPthread();
    }

    MODES_NOTUSED(arg);
    srandom(get_seed());

    pthread_mutex_lock(&Threads.decode.mutex);

    modesInitNet();

    /* If the user specifies --net-only, just run in order to serve network
     * clients without reading data from the RTL device.
     * This rules also in case a local Mode-S Beast is connected via USB.
     */

    //fprintf(stderr, "startup complete after %.3f seconds.\n", (mstime() - Modes.startup_time) / 1000.0);

    interactiveInit();

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now = mstime();
    int64_t mono = mono_milli_seconds();
    if (Modes.net_only) {
        while (!Modes.exit) {
            struct timespec start_time;

            // in case we're not waiting in backgroundTasks and priorityTasks doesn't have a chance to schedule
            if (mono > Modes.next_remove_stale + 5 * SECONDS) {
                threadTimedWait(&Threads.decode, &ts, 1);
            }
            start_cpu_timing(&start_time);

            // sleep via epoll_wait in net_periodic_work
            now = mstime();
            backgroundTasks(now);

            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);
        }
    } else {

        int watchdogCounter = 200; // roughly 20 seconds

        while (!Modes.exit) {
            struct timespec start_time;

            lockReader();
            // reader is locked, and possibly we have data.
            // copy out reader CPU time and reset it
            add_timespecs(&Modes.reader_cpu_accumulator, &Modes.stats_current.reader_cpu, &Modes.stats_current.reader_cpu);
            Modes.reader_cpu_accumulator.tv_sec = 0;
            Modes.reader_cpu_accumulator.tv_nsec = 0;

            struct mag_buf *buf = NULL;
            if (Modes.first_free_buffer != Modes.first_filled_buffer) {
                // FIFO is not empty, process one buffer.
                buf = &Modes.mag_buffers[Modes.first_filled_buffer];
            } else {
                buf = NULL;
            }
            unlockReader();

            if (buf) {
                start_cpu_timing(&start_time);
                demodulate2400(buf);
                if (Modes.mode_ac) {
                    demodulate2400AC(buf);
                }

                Modes.stats_current.samples_processed += buf->length;
                Modes.stats_current.samples_dropped += buf->dropped;
                end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);

                // Mark the buffer we just processed as completed.
                lockReader();
                Modes.first_filled_buffer = (Modes.first_filled_buffer + 1) % MODES_MAG_BUFFERS;
                pthread_cond_signal(&Threads.reader.cond);
                unlockReader();

                Modes.stats_current.samples_lost += Modes.sdr_buf_samples - buf->length;

                timingStatistics(buf);

                watchdogCounter = 100; // roughly 10 seconds
            } else {
                // Nothing to process this time around.
                if (--watchdogCounter <= 0) {
                    fprintf(stderr, "<3>SDR wedged, exiting! (check power supply / avoid using an USB extension / SDR might be defective)\n");
                    setExit(2);
                    break;
                }
            }
            start_cpu_timing(&start_time);
            now = mstime();
            backgroundTasks(now);
            end_cpu_timing(&start_time, &Modes.stats_current.background_cpu);

            lockReader();
            int newData = (Modes.first_free_buffer != Modes.first_filled_buffer);
            unlockReader();

            if (!newData) {
                /* wait for more data.
                 * we should be getting data every 50-60ms. wait for max 80 before we give up and do some background work.
                 * this is fairly aggressive as all our network I/O runs out of the background work!
                 */
                threadTimedWait(&Threads.decode, &ts, 80);
            }
            if (mono > Modes.next_remove_stale + 5 * SECONDS) {
                threadTimedWait(&Threads.decode, &ts, 1);
            }
        }
        sdrCancel();
    }

    pthread_mutex_unlock(&Threads.decode.mutex);
    return NULL;
}

static void traceWriteTask(void *arg, threadpool_threadbuffers_t *buffer_group) {
    task_info_t *info = (task_info_t *) arg;

    if (mono_milli_seconds() > Modes.traceWriteTimelimit) {
        return;
    }

    struct aircraft *a;
    // increment info->from to mark this part of the task as finshed
    for (int j = info->from; j < info->to; j++, info->from++) {
        for (a = Modes.aircraft[j]; a; a = a->next) {
            if (Modes.triggerPastDayTraceWrite && !a->initialTraceWriteDone) {
                a->trace_writeCounter = 0xc0ffee;
                a->trace_write |= WRECENT;
                a->trace_write |= WMEM;
            }
            if (a->trace_write) {
                int64_t before = mono_milli_seconds();
                if (before > Modes.traceWriteTimelimit) {
                    return;
                }
                traceWrite(a, buffer_group);
                a->initialTraceWriteDone = 1;
                int64_t elapsed = mono_milli_seconds() - before;
                if (elapsed > 4 * SECONDS) {
                    fprintf(stderr, "<3>traceWrite() for %06x took %.1f s!\n", a->addr, elapsed / 1000.0);
                }
            }
        }
    }
}


static void setLowPriorityTask(void *arg, threadpool_threadbuffers_t *buffer_group) {
    MODES_NOTUSED(arg);
    MODES_NOTUSED(buffer_group);

    setLowestPriorityPthread();
}


static void writeTraces(int64_t mono) {
    static int lastRunFinished;
    static int part;
    static int64_t lastCompletion;
    static int64_t nextResetCycleDuration;
    static int firstRunDone;

    if (!Modes.tracePool) {
        if (Modes.num_procs <= 1) {
            Modes.tracePoolSize = 1;
        } else if (Modes.num_procs <= 4) {
            Modes.tracePoolSize = Modes.num_procs - 1;
        } else {
            Modes.tracePoolSize = Modes.num_procs - 2;
        }
        Modes.tracePool = threadpool_create(Modes.tracePoolSize, 4);
        Modes.traceTasks = allocate_task_group(8 * Modes.tracePoolSize);
        lastRunFinished = 1;
        lastCompletion = mono;


        // set low priority for this trace pool
        if (0) {
            int taskCount = Modes.tracePoolSize;
            threadpool_task_t *tasks = Modes.traceTasks->tasks;
            for (int i = 0; i < taskCount; i++) {
                threadpool_task_t *task = &tasks[i];

                task->function = setLowPriorityTask;
                task->argument = NULL;
            }
            threadpool_run(Modes.tracePool, tasks, taskCount);
        }
    }

    int taskCount = Modes.traceTasks->task_count;
    threadpool_task_t *tasks = Modes.traceTasks->tasks;
    task_info_t *infos = Modes.traceTasks->infos;

    // how long until we want to have checked every aircraft if a trace needs to be written
    int completeTime = 4 * SECONDS;
    // how many invocations we get in that timeframe
    int invocations = imax(1, completeTime / PERIODIC_UPDATE);
    // how many parts we want to split the complete workload into
    int n_parts = taskCount * invocations;
    int thread_section_len = AIRCRAFT_BUCKETS / n_parts;
    int extra = AIRCRAFT_BUCKETS % n_parts;

    // only assign new task if we finished the last set of tasks
    if (lastRunFinished) {
        for (int i = 0; i < taskCount; i++) {
            int64_t elapsed = mono - lastCompletion;

            if (elapsed > Modes.writeTracesActualDuration) {
                nextResetCycleDuration = mono + 60 * SECONDS;
                Modes.writeTracesActualDuration = elapsed;
            }

            if (part >= n_parts) {
                part = 0;

                if (mono > nextResetCycleDuration) {
                    nextResetCycleDuration = mono + 60 * SECONDS;
                    Modes.writeTracesActualDuration = elapsed;
                }

                if (Modes.triggerPastDayTraceWrite) {
                    // deactivate after one sweep
                    Modes.triggerPastDayTraceWrite = 0;
                    fprintf(stderr, "Wrote live traces for aircraft active within the last 24 hours. This took %.1f seconds (roughly %.0f minutes).\n",
                            elapsed / 1000.0, elapsed / (double) MINUTES);
                }
                if (!firstRunDone) {
                    firstRunDone = 1;
                    fprintf(stderr, "Wrote live traces for aircraft active within the last 15 minutes. This took %.1f seconds (roughly %.0f minutes).\n",
                            elapsed / 1000.0, elapsed / (double) MINUTES);
                    Modes.triggerPastDayTraceWrite = 1; // activated for one sweep
                }

                if (elapsed > 30 * SECONDS && mstime() - Modes.startup_time > 10 * MINUTES) {
                    fprintf(stderr, "trace writing iteration took %.1f seconds (roughly %.0f minutes), live traces will lag behind (historic traces are fine), "
                            "consider alloting more CPU cores or increasing json-trace-interval!\n",
                            elapsed / 1000.0, elapsed / (double) MINUTES);
                }

                lastCompletion = mono;
            }


            threadpool_task_t *task = &tasks[i];
            task_info_t *range = &infos[i];

            int thread_start = part * thread_section_len + imin(extra, part);
            int thread_end = thread_start + thread_section_len + (part < extra ? 1 : 0);

            part++;

            //fprintf(stderr, "%8d %8d %8d\n", thread_start, thread_end, AIRCRAFT_BUCKETS);

            if (thread_end > AIRCRAFT_BUCKETS) {
                thread_end = AIRCRAFT_BUCKETS;
                fprintf(stderr, "check traceWriteTask distribution\n");
            }

            range->from = thread_start;
            range->to = thread_end;

            task->function = traceWriteTask;
            task->argument = range;

            if (part >= n_parts) {
                if (thread_end != AIRCRAFT_BUCKETS || i != taskCount - 1) {
                    fprintf(stderr, "check traceWriteTask distribution\n");
                }
            }
        }
    }

    struct timespec before = threadpool_get_cumulative_thread_time(Modes.tracePool);
    threadpool_run(Modes.tracePool, tasks, taskCount);
    struct timespec after = threadpool_get_cumulative_thread_time(Modes.tracePool);
    timespec_add_elapsed(&before, &after, &Modes.stats_current.trace_json_cpu);

    lastRunFinished = 1;
    for (int i = 0; i < taskCount; i++) {
        task_info_t *range = &infos[i];
        if (range->from != range->to) {
            lastRunFinished = 0;
        }
    }

    //fprintf(stderr, "n_parts %4d part %4d lastRunFinished %d\n", n_parts, part, lastRunFinished);

    // reset allocated buffers every 5 minutes
    static int64_t next_buffer_reset;
    if (mono > next_buffer_reset) {
        next_buffer_reset = mono + 5 * MINUTES;
        threadpool_reset_buffers(Modes.tracePool);
    }
}

static void *upkeepEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());

    pthread_mutex_lock(&Threads.upkeep.mutex);

    Modes.lockThreads[Modes.lockThreadsCount++] = &Threads.misc;
    Modes.lockThreads[Modes.lockThreadsCount++] = &Threads.apiUpdate;
    Modes.lockThreads[Modes.lockThreadsCount++] = &Threads.globeJson;
    Modes.lockThreads[Modes.lockThreadsCount++] = &Threads.globeBin;
    Modes.lockThreads[Modes.lockThreadsCount++] = &Threads.json;
    Modes.lockThreads[Modes.lockThreadsCount++] = &Threads.decode;

    if (Modes.lockThreadsCount > LOCK_THREADS_MAX) {
        fprintf(stderr, "FATAL: LOCK_THREADS_MAX insufficient!\n");
        exit(1);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {

        priorityTasksRun();

        if (Modes.json_globe_index) {
            // writing a trace takes some time, to increase timing precision the priority tasks, allot a little less time than available
            // this isn't critical though
            int64_t time_alloted = ms_until_priority();
            if (time_alloted > 50) {

                int64_t mono = mono_milli_seconds();
                Modes.traceWriteTimelimit = mono + time_alloted;

                struct timespec watch;
                startWatch(&watch);

                Modes.currentTask = "writeTraces_start";
                writeTraces(mono);
                Modes.currentTask = "writeTraces_end";

                int64_t elapsed = stopWatch(&watch);
                if (elapsed > 4 * SECONDS) {
                    fprintf(stderr, "<3>writeTraces() took %"PRIu64" ms!\n", elapsed);
                }
            }
        }

        checkReplaceState();

        int64_t wait = ms_until_priority();

        if (Modes.synthetic_now) {
            wait = 5000;
        }

        if (0) {
            fprintf(stderr, "upkeep wait: %ld ms\n", (long) wait);
        }

        if (wait > 0) {
            clock_gettime(CLOCK_REALTIME, &ts);
            threadTimedWait(&Threads.upkeep, &ts, wait);
        }
    }

    pthread_mutex_unlock(&Threads.upkeep.mutex);

    return NULL;
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

//
//=========================================================================
//
// This function is called a few times every second by main in order to
// perform tasks we need to do continuously, like accepting new clients
// from the net, refreshing the screen in interactive mode, and so forth
//
static void backgroundTasks(int64_t now) {
    if (Modes.net) {
        modesNetPeriodicWork();
    }

    // Refresh screen when in interactive mode
    static int64_t next_interactive;
    if (Modes.interactive && now > next_interactive) {
        interactiveShowData();
        next_interactive = now + 42;
    }

    static int64_t next_flip = 0;
    if (now >= next_flip) {
        icaoFilterExpire(now);
        next_flip = now + MODES_ICAO_FILTER_TTL;
    }

    static int64_t next_every_second;
    if (now > next_every_second) {
        next_every_second = now + 1 * SECONDS;

        checkNewDayAcas(now);

        if (Modes.mode_ac) {
            trackMatchAC(now);
        }
    }
}

static void print_commandline(int argc, char **argv) {
    fprintf(stderr, "invoked by: ");
    for (int k = 0; k < argc; k++) {
        fprintf(stderr, "%s", argv[k]);
        if (k < argc - 1) {
            fprintf(stderr, " ");
        }
    }
    fprintf(stderr, "\n");
}

//=========================================================================
// Clean up memory prior to exit.
static void cleanup_and_exit(int code) {
    if (Modes.acasFD1 > -1)
        close(Modes.acasFD1);
    if (Modes.acasFD2 > -1)
        close(Modes.acasFD2);
    // Free any used memory
    geomag_destroy();
    interactiveCleanup();
    cleanup_globe_index();
    sfree(Modes.dev_name);
    sfree(Modes.filename);
    sfree(Modes.prom_file);
    sfree(Modes.json_dir);
    sfree(Modes.globe_history_dir);
    sfree(Modes.heatmap_dir);
    sfree(Modes.dump_beast_dir);
    sfree(Modes.state_dir);
    sfree(Modes.globalStatsCount.rssi_table);
    sfree(Modes.net_bind_address);
    sfree(Modes.db_file);
    sfree(Modes.net_input_beast_ports);
    sfree(Modes.net_output_beast_ports);
    sfree(Modes.net_output_beast_reduce_ports);
    sfree(Modes.net_output_vrs_ports);
    sfree(Modes.net_input_raw_ports);
    sfree(Modes.net_output_raw_ports);
    sfree(Modes.net_output_sbs_ports);
    sfree(Modes.net_input_sbs_ports);
    sfree(Modes.net_input_jaero_ports);
    sfree(Modes.net_output_jaero_ports);
    sfree(Modes.net_output_json_ports);
    sfree(Modes.net_output_api_ports);
    sfree(Modes.beast_serial);
    sfree(Modes.uuidFile);
    sfree(Modes.dbIndex);
    sfree(Modes.db);

    int i;
    for (i = 0; i < MODES_MAG_BUFFERS; ++i) {
        sfree(Modes.mag_buffers[i].data);
    }
    crcCleanupTables();

    receiverCleanup();

    for (int i = 0; i <= GLOBE_MAX_INDEX; i++) {
        ca_destroy(&Modes.globeLists[i]);
    }
    ca_destroy(&Modes.aircraftActive);

    icaoFilterDestroy();
    quickDestroy();

    exit(code);
}

static int make_net_connector(char *arg) {
    if (!Modes.net_connectors || Modes.net_connectors_count + 1 > Modes.net_connectors_size) {
        Modes.net_connectors_size = Modes.net_connectors_count * 2 + 8;
        Modes.net_connectors = realloc(Modes.net_connectors, sizeof(struct net_connector) * (size_t) Modes.net_connectors_size);
        if (!Modes.net_connectors) {
            fprintf(stderr, "realloc error net_connectors\n");
            exit(1);
        }
    }
    struct net_connector *con = &Modes.net_connectors[Modes.net_connectors_count++];
    memset(con, 0x0, sizeof(struct net_connector));
    char *connect_string = strdup(arg);

    int maxTokens = 128;
    char* token[maxTokens];
    tokenize(&connect_string, ",", token, maxTokens);

    int k = 0;
    con->address = con->address0 = token[k++];
    con->port = con->port0 = token[k++];
    con->protocol = token[k++];

    if (token[k] && strcmp(token[k], "silent_fail") == 0) {
        k++;
        con->silent_fail = 1;
    }

    con->address1 = token[k++];
    con->port1 = token[k++];


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
            && strcmp(con->protocol, "beast_reduce_plus_out") != 0
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
            && strcmp(con->protocol, "feedmap_out") != 0
            && strcmp(con->protocol, "gpsd_in") != 0
            && strcmp(con->protocol, "uat_in") != 0
       ) {
        fprintf(stderr, "--net-connector: Unknown protocol: %s\n", con->protocol);
        fprintf(stderr, "Supported protocols: beast_out, beast_in, beast_reduce_out, beast_reduce_plus_out, raw_out, raw_in, \n"
                "sbs_out, sbs_out_replay, sbs_out_mlat, sbs_out_jaero, \n"
                "sbs_in, sbs_in_mlat, sbs_in_jaero, \n"
                "vrs_out, json_out, gpsd_in, uat_in\n");
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

static int parseLongs(char *p, long long *results, int result_size) {
    char *saveptr = NULL;
    char *endptr = NULL;
    int count = 0;
    char *tok = strtok_r(p, ",", &saveptr);
    while (tok && count < result_size) {
        results[count] = strtoll(tok, &endptr, 10);
        if (tok != endptr)
            count++;
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return count;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    //fprintf(stderr, "parse_opt(%d, %s, argp_state*)\n", key, arg);
    int maxTokens = 128;
    char* token[maxTokens];
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
        case OptNoFixDf:
            Modes.fixDF = 0;
            break;
        case OptRaw:
            Modes.raw = 1;
            break;
        case OptPreambleThreshold:
            Modes.preambleThreshold = (uint32_t) (imax(imin(strtoll(arg, NULL, 10), PREAMBLE_THRESHOLD_MAX), PREAMBLE_THRESHOLD_MIN));
            break;
        case OptNet:
            Modes.net = 1;
            break;
        case OptModeAc:
            Modes.mode_ac = 1;
            break;
        case OptModeAcAuto:
            Modes.mode_ac_auto = 1;
            break;
        case OptNetOnly:
            Modes.net = 1;
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
            Modes.show_only = (uint32_t) strtol(arg, NULL, 16);
            Modes.decode_all = 1;
            Modes.interactive = 0;
            Modes.quiet = 1;
            //Modes.cpr_focus = Modes.show_only;
            fprintf(stderr, "show-only: %06x\n", Modes.show_only);
            break;
        case OptFilterDF:
            Modes.filterDF = 1;
            Modes.filterDFbitset = 0; // reset it
#define dfs_size 128
            long long dfs[dfs_size];
            int count = parseLongs(arg, dfs, dfs_size);
            for (int i = 0; i < count; i++)
            {
                Modes.filterDFbitset |= (1 << dfs[i]);
            }
            fprintf(stderr, "filter-DF: %s\n", arg);
#undef dfs_size
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
            Modes.quiet = 1;
            break;
        case OptInteractiveTTL:
            Modes.interactive_display_ttl = (int64_t) (1000 * atof(arg));
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
            if (!Modes.stats_display_interval)
                Modes.stats_display_interval = ((int64_t) 1) << 60; // "never"
            break;
        case OptStatsRange:
            Modes.stats_range_histo = 1;
            break;
        case OptStatsEvery:
            Modes.stats_display_interval = ((int64_t) nearbyint(atof(arg) / 10.0)) * 10 * SECONDS;
            if (Modes.stats_display_interval == 0) {
                Modes.stats_display_interval = 10 * SECONDS;
            }
            break;
        case OptRangeOutlineDuration:
            Modes.range_outline_duration = (int64_t) (atof(arg) * HOURS);
            if (Modes.range_outline_duration < 1 * MINUTES) {
                Modes.range_outline_duration = 1 * MINUTES;
                fprintf(stderr, "--range-outline-hours too small, using minimum value 0.016\n");
            }
            break;
        case OptSnip:
            snipMode(atoi(arg));
            cleanup_and_exit(0);
            break;
        case OptPromFile:
            Modes.prom_file = strdup(arg);
            break;
        case OptJsonDir:
            sfree(Modes.json_dir);
            Modes.json_dir = strdup(arg);
            break;
        case OptAircraftJsonSeenByList:
            Modes.aircraft_json_seen_by_list = 1;
            break;
        case OptAircraftJsonSeenByListTimeout:
            Modes.aircraft_json_seen_by_list_timeout = atoi(arg);
        break;
        case OptHeatmap:
            Modes.heatmap = 1;
            if (atof(arg) > 0)
                Modes.heatmap_interval = (int64_t)(1000.0 * atof(arg));
            break;
        case OptHeatmapDir:
            sfree(Modes.heatmap_dir);
            Modes.heatmap_dir = strdup(arg);
            break;
        case OptDumpBeastDir:
            tokenize(&arg, ",", token, maxTokens); if (!token[0]) { break; }

            sfree(Modes.dump_beast_dir);
            Modes.dump_beast_dir = strdup(token[0]);
            if (token[1]) { Modes.dump_interval = atoi(token[1]); }
            // enable networking as this is required
            Modes.net = 1;
            break;
        case OptGlobeHistoryDir:
            sfree(Modes.globe_history_dir);
            Modes.globe_history_dir = strdup(arg);
            break;
        case OptStateOnlyOnExit:
            Modes.state_only_on_exit = 1;
            break;
        case OptStateDir:
            sfree(Modes.state_parent_dir);
            Modes.state_parent_dir = strdup(arg);
            break;
        case OptJsonTime:
            Modes.json_interval = (int64_t) (1000.0 * atof(arg));
            if (Modes.json_interval < 100) // 0.1s
                Modes.json_interval = 100;
            break;
        case OptJsonLocAcc:
            Modes.json_location_accuracy = (int8_t) atoi(arg);
            break;

        case OptJaeroTimeout:
            Modes.trackExpireJaero = (uint32_t) (atof(arg) * MINUTES);
            break;
        case OptPositionPersistence:
            Modes.position_persistence = imax(0, atoi(arg));
            break;
        case OptJsonReliable:
            Modes.json_reliable = atoi(arg);
            if (Modes.json_reliable < -1)
                Modes.json_reliable = -1;
            if (Modes.json_reliable > 4)
                Modes.json_reliable = 4;
            break;
        case OptDbFileLongtype:
            Modes.jsonLongtype = 1;
            break;
        case OptDbFile:
            sfree(Modes.db_file);
            if (strcmp(arg, "tar1090") == 0) {
                Modes.db_file = strdup("/usr/local/share/tar1090/git-db/aircraft.csv.gz");
            } else {
                Modes.db_file = strdup(arg);
            }
            if (strlen(Modes.db_file) == 0 || strcmp(Modes.db_file, "none") == 0) {
                sfree(Modes.db_file);
                Modes.db_file = NULL;
            }
            break;
        case OptJsonGzip:
            Modes.json_gzip = 1;
            break;
        case OptEnableBinGz:
            Modes.enableBinGz = 1;
            break;
        case OptJsonOnlyBin:
            Modes.onlyBin = (int8_t) atoi(arg);
            break;
        case OptJsonTraceHistOnly:
            Modes.trace_hist_only = (int8_t) atoi(arg);
            break;
        case OptJsonTraceInt:
            Modes.json_trace_interval = (int64_t)(1000 * atof(arg));
            break;
        case OptJsonGlobeIndex:
            Modes.json_globe_index = 1;
            break;
        case OptNetHeartbeat:
            Modes.net_heartbeat_interval = (int64_t) (1000 * atof(arg));
            break;
        case OptNetRoSize:
            Modes.net_output_flush_size = atoi(arg);
            break;
        case OptNetRoRate:
            Modes.net_output_flush_interval = 1000 * atoi(arg) / 15; // backwards compatibility
            break;
        case OptNetRoIntervall:
            Modes.net_output_flush_interval = (int64_t) (1000 * atof(arg));
            break;
        case OptNetRoPorts:
            sfree(Modes.net_output_raw_ports);
            Modes.net_output_raw_ports = strdup(arg);
            break;
        case OptNetRiPorts:
            sfree(Modes.net_input_raw_ports);
            Modes.net_input_raw_ports = strdup(arg);
            break;
        case OptNetBoPorts:
            sfree(Modes.net_output_beast_ports);
            Modes.net_output_beast_ports = strdup(arg);
            break;
        case OptNetBiPorts:
            sfree(Modes.net_input_beast_ports);
            Modes.net_input_beast_ports = strdup(arg);
            break;
        case OptNetBeastReducePorts:
            sfree(Modes.net_output_beast_reduce_ports);
            Modes.net_output_beast_reduce_ports = strdup(arg);
            break;
        case OptNetBeastReduceFilterAlt:
            if (atof(arg) > 0)
                Modes.beast_reduce_filter_altitude = (float) atof(arg);
            break;
        case OptNetBeastReduceFilterDist:
            if (atof(arg) > 0)
                Modes.beast_reduce_filter_distance = (float) atof(arg) * 1852.0f; // convert to meters
            break;
        case OptNetBeastReduceInterval:
            if (atof(arg) >= 0)
                Modes.net_output_beast_reduce_interval = (int64_t) (1000 * atof(arg));
            if (Modes.net_output_beast_reduce_interval > 15000)
                Modes.net_output_beast_reduce_interval = 15000;
            break;
        case OptNetSbsReduce:
            Modes.sbsReduce = 1;
            break;
        case OptNetBindAddr:
            sfree(Modes.net_bind_address);
            Modes.net_bind_address = strdup(arg);
            break;
        case OptNetSbsPorts:
            sfree(Modes.net_output_sbs_ports);
            Modes.net_output_sbs_ports = strdup(arg);
            break;
        case OptNetJsonPortNoPos:
            Modes.net_output_json_include_nopos = 1;
            break;
        case OptNetJsonPortInterval:
            Modes.net_output_json_interval = (int64_t)(atof(arg) * SECONDS);
            break;
        case OptNetJsonPorts:
            sfree(Modes.net_output_json_ports);
            Modes.net_output_json_ports = strdup(arg);
            break;
        case OptTar1090UseApi:
            Modes.tar1090_use_api = 1;
            break;
        case OptNetApiPorts:
            sfree(Modes.net_output_api_ports);
            Modes.net_output_api_ports = strdup(arg);
            Modes.api = 1;
            break;
        case OptApiShutdownDelay:
            Modes.apiShutdownDelay = atof(arg) * SECONDS;
            break;
        case OptNetSbsInPorts:
            sfree(Modes.net_input_sbs_ports);
            Modes.net_input_sbs_ports = strdup(arg);
            break;
        case OptNetJaeroPorts:
            sfree(Modes.net_output_jaero_ports);
            Modes.net_output_jaero_ports = strdup(arg);
            break;
        case OptNetJaeroInPorts:
            sfree(Modes.net_input_jaero_ports);
            Modes.net_input_jaero_ports = strdup(arg);
            break;
        case OptNetVRSPorts:
            sfree(Modes.net_output_vrs_ports);
            Modes.net_output_vrs_ports = strdup(arg);
            break;
        case OptNetVRSInterval:
            if (atof(arg) > 0)
                Modes.net_output_vrs_interval = (int64_t)(atof(arg) * SECONDS);
            break;
        case OptNetBuffer:
            Modes.net_sndbuf_size = atoi(arg);
            break;
        case OptNetVerbatim:
            Modes.net_verbatim = 1;
            break;
        case OptSdrBufSize:
            Modes.sdr_buf_size = atoi(arg) * 1024;
            break;
        case OptNetReceiverId:
            Modes.netReceiverId = 1;
            Modes.ping = 1;
            break;
        case OptNetReceiverIdJson:
            Modes.netReceiverIdJson = 1;
            break;
        case OptGarbage:
            Modes.garbage_ports = strdup(arg);
            break;
        case OptDecodeThreads:
            Modes.decodeThreads = imax(1, atoi(arg));
            break;
        case OptNetIngest:
            Modes.netIngest = 1;
            break;
        case OptUuidFile:
            sfree(Modes.uuidFile);
            Modes.uuidFile = strdup(arg);
            break;
        case OptNetConnector:
            if (make_net_connector(arg)) {
                return ARGP_ERR_UNKNOWN;
            }
            break;
        case OptNetConnectorDelay:
            Modes.net_connector_delay = (int64_t) (1000 * atof(arg));
            break;

        case OptTraceFocus:
            Modes.trace_focus = (uint32_t) strtol(arg, NULL, 16);
            Modes.interactive = 0;
            fprintf(stderr, "trace_focus = %06x\n", Modes.trace_focus);
            break;
        case OptCprFocus:
            Modes.cpr_focus = (uint32_t) strtol(arg, NULL, 16);
            Modes.interactive = 0;
            fprintf(stderr, "cpr_focus = %06x\n", Modes.cpr_focus);
            break;
        case OptLegFocus:
            Modes.leg_focus = (uint32_t) strtol(arg, NULL, 16);
            fprintf(stderr, "leg_focus = %06x\n", Modes.leg_focus);
            break;
        case OptReceiverFocus:
            {
                char rfocus[16];
                char *p = arg;
                for (uint32_t i = 0; i < sizeof(rfocus); i++) {
                    if (*p == '-')
                        p++;
                    rfocus[i] = *p;
                    if (*p != 0)
                        p++;
                }
                Modes.receiver_focus = strtoull(rfocus, NULL, 16);
                fprintf(stderr, "receiver_focus = %016"PRIx64"\n", Modes.receiver_focus);
            }
            break;

        case OptDevel:
            {
                tokenize(&arg, ",", token, maxTokens);
                if (!token[0]) {
                    break;
                }
                if (strcasecmp(token[0], "lastStatus") == 0) {
                    if (token[1]) {
                        Modes.debug_lastStatus = atoi(token[1]);
                        fprintf(stderr, "lastStatus: %d\n", Modes.debug_lastStatus);
                    } else {
                        Modes.debug_lastStatus = 1;
                    }
                }
                if (strcasecmp(token[0], "apiThreads") == 0) {
                    Modes.apiThreadCount = atoi(token[1]);
                }
                if (strcasecmp(token[0], "accept_synthetic") == 0) {
                    Modes.dump_accept_synthetic_now = 1;
                }
                if (strcasecmp(token[0], "dump_reduce") == 0) {
                    Modes.dump_reduce = 1;
                    fprintf(stderr, "Modes.dump_reduce: %d\n", Modes.dump_reduce);
                }
                if (strcasecmp(token[0], "ping_reject") == 0 && token[1]) {
                    Modes.ping_reject = atoi(token[1]);
                    Modes.ping_reduce = Modes.ping_reject / 2;
                }

                if (strcasecmp(token[0], "messageRateMult") == 0 && token[1]) {
                    Modes.messageRateMult = atof(token[1]);
                }
                if (strcasecmp(token[0], "incrementId") == 0) {
                    Modes.incrementId = 1;
                }
            }
            break;

        case OptDebug:
            while (*arg) {
                switch (*arg) {
                    case 'n': Modes.debug_net = 1;
                        break;
                    case 'N': Modes.debug_nextra = 1;
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
                    case 'A': Modes.debug_ACAS = 1;
                        fprintf(stderr, "debug_ACAS enabled!\n");
                        break;
                    case 'a': Modes.debug_api = 1;
                        break;
                    case 'C': Modes.debug_recent = 1;
                        break;
                    case 's': Modes.debug_squawk = 1;
                        break;
                    case 'p': Modes.debug_ping = 1;
                        break;
                    case 'c': Modes.debug_callsign = 1;
                        break;
                    case 'g': Modes.debug_nogps = 1;
                        break;
                    case 'u': Modes.debug_uuid = 1;
                        break;
                    case 'b': Modes.debug_bogus = 1;
                              Modes.decode_all = 1;
                        break;
                    case 'm': Modes.debug_maxRange = 1;
                        break;
                    case 'r': Modes.debug_removeStaleDuration = 1;
                        break;
                    case 'X': Modes.debug_receiverRangeLimit = 1;
                        break;
                    case 'v': Modes.verbose = 1;
                        break;
                    case 'Y': Modes.debug_yeet = 1;
                        break;
                    case '7': Modes.debug_7700 = 1;
                        break;
                    case 'D': Modes.debug_send_uuid = 1;
                        break;
                    case 'd': Modes.debug_no_discard = 1;
                        break;
                    case 'Z': Modes.debug_provoke_segfault = 1;
                        break;
                    case 'y': Modes.debug_position_timing = 1;
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
        case OptBeastBaudrate:
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
        case ARGP_KEY_ARG:
            return ARGP_ERR_UNKNOWN;
            break;
        case ARGP_KEY_END:
            if (state->arg_num > 0) {
                /* We use only options but no arguments */
                return ARGP_ERR_UNKNOWN;
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

int parseCommandLine(int argc, char **argv) {
    // check if we are running as viewadsb and set according behaviour
    if (strstr(argv[0], "viewadsb")) {
        Modes.viewadsb = 1;
        Modes.net = 1;
        Modes.sdr_type = SDR_NONE;
        Modes.net_only = 1;
#ifndef DISABLE_INTERACTIVE
        Modes.interactive = 1;
        Modes.quiet = 1;
#endif
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

    const char* args_doc = "";
    struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

    if (argp_parse(&argp, argc, argv, ARGP_NO_EXIT, 0, 0)) {
        print_commandline(argc, argv);
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

    print_commandline(argc, argv);

    log_with_timestamp("readsb starting up.");
    fprintf(stderr, VERSION_STRING"\n");

    return 0;
}

static void configAfterParse() {
    Modes.sdr_buf_samples = Modes.sdr_buf_size / 2;
    Modes.trackExpireMax = Modes.trackExpireJaero + TRACK_EXPIRE_LONG + 1 * MINUTES;

    if (Modes.json_globe_index) {
        Modes.keep_traces = 24 * HOURS + 60 * MINUTES; // include 60 minutes overlap
    } else if (Modes.heatmap || Modes.trace_focus != BADDR) {
        Modes.keep_traces = 35 * MINUTES; // heatmap is written every 30 minutes
    }

    Modes.traceMax = alignSFOUR((Modes.keep_traces + 1 * HOURS) / 1000 * 3); // 3 position per second, usually 2 per second is max

    if (Modes.json_trace_interval > 10 * SECONDS) {
        Modes.traceReserve = alignSFOUR(16);
    } else if (Modes.json_trace_interval > 5 * SECONDS) {
        Modes.traceReserve = alignSFOUR(32);
    } else {
        Modes.traceReserve = alignSFOUR(64);
    }

    Modes.traceChunkPoints = alignSFOUR(4 * 64);
    Modes.traceChunkMaxBytes = 16 * 1024;

    if (Modes.json_trace_interval < 1) {
        Modes.json_trace_interval = 1; // 1 ms
    }

    if (Modes.json_trace_interval < 4 * SECONDS) {
        double oversize = 4.0 / fmax(1, (double) Modes.json_trace_interval / 1000.0);
        Modes.traceChunkPoints = alignSFOUR(Modes.traceChunkPoints * oversize);
        Modes.traceChunkMaxBytes *= oversize;
    }

    //Modes.traceChunkPoints = alignSFOUR(4);

    Modes.traceRecentPoints = alignSFOUR(TRACE_RECENT_POINTS);
    Modes.traceCachePoints = alignSFOUR(Modes.traceRecentPoints + TRACE_CACHE_EXTRA);

    if (Modes.verbose) {
        fprintf(stderr, "traceChunkPoints: %d size: %ld\n", Modes.traceChunkPoints, (long) stateBytes(Modes.traceChunkPoints));
    }

    Modes.num_procs = 1; // default this value to 1
    cpu_set_t mask;
    if (sched_getaffinity(getpid(), sizeof(mask), &mask) == 0) {
        Modes.num_procs = CPU_COUNT(&mask);
        if (Modes.num_procs < 2 && !Modes.preambleThreshold && Modes.sdr_type != SDR_NONE) {
            fprintf(stderr, "WARNING: Reducing preamble threshold / decoding performance as this system has only 1 core (explicitely set --preamble-threshold to disable this behaviour)!\n");
            Modes.preambleThreshold = PREAMBLE_THRESHOLD_PIZERO;
            Modes.fixDF = 0;
        }
    }
    if (Modes.num_procs < 1) {
        Modes.num_procs = 1; // sanity check
    }
    if (!Modes.preambleThreshold) {
        Modes.preambleThreshold = PREAMBLE_THRESHOLD_DEFAULT;
    }

    if (Modes.mode_ac)
        Modes.mode_ac_auto = 0;

    if (!Modes.quiet)
        Modes.decode_all = 1;

    if (Modes.viewadsb && Modes.net_connectors_count == 0) {
        Modes.net_connectors_count++; // activate the default net-connector for viewadsb
    }

    if (Modes.heatmap) {
        if (!Modes.globe_history_dir && !Modes.heatmap_dir) {
            fprintf(stderr, "Heatmap requires globe history dir or heatmap dir to be set, disabling heatmap!\n");
            Modes.heatmap = 0;
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
    if ((Modes.fUserLat != 0.0) || (Modes.fUserLon != 0.0)) {
        Modes.userLocationValid = 1;
        fprintf(stderr, "Using lat: %9.4f, lon: %9.4f\n", Modes.fUserLat, Modes.fUserLon);
    }
    if (!Modes.userLocationValid || !Modes.json_dir) {
        Modes.outline_json = 0; // disable outline_json
    }
    if (Modes.json_reliable == -13) {
        if (Modes.userLocationValid && Modes.maxRange != 0)
            Modes.json_reliable = 1;
        else
            Modes.json_reliable = 2;
    }
    //fprintf(stderr, "json_reliable: %d\n", Modes.json_reliable);

    if (Modes.position_persistence < Modes.json_reliable) {
        Modes.position_persistence = imax(0, Modes.json_reliable);
        fprintf(stderr, "position-persistence must be >= json-reliable! setting position-persistence: %d\n",
                Modes.position_persistence);
    }

    if (Modes.net_output_flush_size > (MODES_OUT_BUF_SIZE)) {
        Modes.net_output_flush_size = MODES_OUT_BUF_SIZE;
    }
    if (Modes.net_output_flush_size < 750) {
        Modes.net_output_flush_size = 750;
    }
    if (Modes.net_output_flush_interval > (MODES_OUT_FLUSH_INTERVAL)) {
        Modes.net_output_flush_interval = MODES_OUT_FLUSH_INTERVAL;
    }
    if (Modes.net_output_flush_interval < 0)
        Modes.net_output_flush_interval = 0;

    if (Modes.net_sndbuf_size > (MODES_NET_SNDBUF_MAX)) {
        Modes.net_sndbuf_size = MODES_NET_SNDBUF_MAX;
    }

    if (Modes.net_connector_delay <= 50) {
        Modes.net_connector_delay = 50;
    }
    if ((Modes.net_connector_delay > 600 * 1000)) {
        Modes.net_connector_delay = 600 * 1000;
    }

    if (Modes.sdr_type == SDR_NONE) {
        if (Modes.net)
            Modes.net_only = 1;
        if (!Modes.net_only) {
            fprintf(stderr, "No networking or SDR input selected, exiting! Try '--device-type rtlsdr'! See 'readsb --help'\n");
            cleanup_and_exit(1);
        }
    } else if (Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS) {
        Modes.net = 1;
        Modes.net_only = 1;
    } else {
        Modes.net_only = 0;
    }
}

static void notask_save_blob(uint32_t blob) {
    threadpool_buffer_t pbuffer1 = { 0 };
    threadpool_buffer_t pbuffer2 = { 0 };
    save_blob(blob, &pbuffer1, &pbuffer2);
    free_threadpool_buffer(&pbuffer1);
    free_threadpool_buffer(&pbuffer2);
}

static void loadReplaceState() {
    if (!Modes.replace_state_blob) {
        return;
    }

    fprintf(stderr, "overriding current state with this blob: %s\n", Modes.replace_state_blob);
    threadpool_buffer_t buffers[4];
    memset(buffers, 0x0, sizeof(buffers));
    threadpool_threadbuffers_t group = { .buffer_count = 4, .buffers = buffers };
    load_blob(Modes.replace_state_blob, &group);

    char blob[1024];
    snprintf(blob, 1024, "%s.zstl", Modes.replace_state_blob);
    unlink(blob);

    for (uint32_t k = 0; k < group.buffer_count; k++) {
        free_threadpool_buffer(&buffers[k]);
    }
    free(Modes.replace_state_blob);
    Modes.replace_state_blob = NULL;
}

static void checkReplaceState() {
    if (!Modes.state_dir) {
        return;
    }
    if (Modes.replace_state_blob) {
        return;
    }
    char filename[PATH_MAX];

    snprintf(filename, PATH_MAX, "%s/replaceState", Modes.state_dir);
    if (!Modes.replace_state_blob && access(filename, R_OK) == 0) {
        for (int j = 0; j < STATE_BLOBS; j++) {
            char blob[1024];
            snprintf(blob, 1024, "%s/blob_%02x.zstl", filename, j);
            if (access(blob, R_OK) == 0) {
                snprintf(blob, 1024, "%s/blob_%02x", filename, j);
                Modes.replace_state_blob = strdup(blob);
                int64_t mono = mono_milli_seconds();
                Modes.replace_state_inhibit_traces_until = mono + 10 * SECONDS;
                break;
            }
        }
    }
}

static void miscStuff(int64_t now) {

    checkNewDay(now);

    if (Modes.outline_json) {
        static int64_t nextOutlineWrite;
        if (now > nextOutlineWrite) {
            free(writeJsonToFile(Modes.json_dir, "outline.json", generateOutlineJson()).buffer);
            nextOutlineWrite = now + 15 * SECONDS;
        }
    }

    // don't do everything at once ... this stuff isn't that time critical it'll get its turn

    if (Modes.state_dir) {
        static uint32_t blob; // current blob
        static int64_t next_blob;

        char filename[PATH_MAX];
        snprintf(filename, PATH_MAX, "%s/writeState", Modes.state_dir);
        int fd = open(filename, O_RDONLY);
        if (fd > -1) {
            next_blob = now + 45 * SECONDS;

            char tmp[3];
            int len = read(fd, tmp, 2);
            close(fd);

            tmp[2] = '\0';

            if (len == 0) {
                writeInternalState();
            } else if (len == 2) {
                uint32_t suffix = strtol(tmp, NULL, 16);
                notask_save_blob(suffix);
                fprintf(stderr, "save_blob: %02x\n", suffix);
            }

            unlink(filename);
            // unlink only after writing state, if the file doesn't exist that's fine as well
            // this is a hack to detect from a shell script when the task is done

            return;
        }

        // only continuously write state if we keep permanent trace
        if (!Modes.state_only_on_exit && now > next_blob) {
            //fprintf(stderr, "save_blob: %02x\n", blob);
            notask_save_blob(blob);
            blob = (blob + 1) % STATE_BLOBS;
            next_blob = now + 60 * MINUTES / STATE_BLOBS;

            return;
        }
    }

    // function can unlock / lock misc mutex
    if (handleHeatmap(now)) {
        return;
    }

    static int64_t next_clients_json;
    if (Modes.json_dir && now > next_clients_json) {
        next_clients_json = now + 10 * SECONDS;
        if (Modes.netIngest)
            free(writeJsonToFile(Modes.json_dir, "clients.json", generateClientsJson()).buffer);
        if (Modes.netReceiverIdJson)
            free(writeJsonToFile(Modes.json_dir, "receivers.json", generateReceiversJson()).buffer);

        return;
    }

    if (dbUpdate(now)) {
        return;
    }
}

static void *miscEntryPoint(void *arg) {
    MODES_NOTUSED(arg);

    if (0) {
        // this is a low priority thread
        setLowestPriorityPthread();
    }

    pthread_mutex_lock(&Threads.misc.mutex);

    srandom(get_seed());
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {
        threadTimedWait(&Threads.misc, &ts, 200); // check every 0.2 seconds if there is something to do

        // priorityTasksRun has priority
        if (priorityTasksPending()) {
            continue;
        }

        struct timespec watch;
        startWatch(&watch);

        struct timespec start_time;
        start_cpu_timing(&start_time);

        int64_t now = mstime();

        // function can unlock / lock misc mutex
        miscStuff(now);

        end_cpu_timing(&start_time, &Modes.stats_current.heatmap_and_state_cpu);

        int64_t elapsed = stopWatch(&watch);
        static int64_t antiSpam2;
        if (elapsed > 12 * SECONDS && now > antiSpam2 + 30 * SECONDS) {
            fprintf(stderr, "<3>High load: heatmap_and_stuff took %"PRIu64" ms! Suppressing for 30 seconds\n", elapsed);
            antiSpam2 = now;
        }
    }

    pthread_mutex_unlock(&Threads.misc.mutex);

    pthread_exit(NULL);
}

//
//=========================================================================
//

int main(int argc, char **argv) {

    if (0) {
        unlink("test.gz");
        gzipFile("test");
        exit(1);
    }

    srandom(get_seed());

    // Set sane defaults
    configSetDefaults();

    Modes.startup_time = mstime();

    if (lzo_init() != LZO_E_OK)
    {
        fprintf(stderr, "internal error - lzo_init() failed !!!\n");
        fprintf(stderr, "(this usually indicates a compiler bug - try recompiling\nwithout optimizations, and enable '-DLZO_DEBUG' for diagnostics)\n");
        return 3;
    }

    // signal handling stuff
    Modes.exitNowEventfd = eventfd(0, EFD_NONBLOCK);
    Modes.exitSoonEventfd = eventfd(0, EFD_NONBLOCK);
    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigtermHandler);
    signal(SIGUSR1, SIG_IGN);

    if (argc >= 2 && !strcmp(argv[1], "--structs")) {
        fprintf(stderr, VERSION_STRING"\n");
        fprintf(stderr, "struct aircraft: %zu\n", sizeof(struct aircraft));
        fprintf(stderr, "struct validity: %zu\n", sizeof(data_validity));
        fprintf(stderr, "state: %zu\n", sizeof(struct state));
        fprintf(stderr, "state_all: %zu\n", sizeof(struct state_all));
        fprintf(stderr, "fourState: %zu\n", sizeof(fourState));
        fprintf(stderr, "binCraft: %zu\n", sizeof(struct binCraft));
        fprintf(stderr, "apiEntry: %zu\n", sizeof(struct apiEntry));
        //fprintf(stderr, "%zu\n", sizeof(struct state_flags));
        fprintf(stderr, "modesMessage: %zu\n", sizeof(struct modesMessage));
        fprintf(stderr, "stateChunk: %zu\n", sizeof(stateChunk));

        //struct aircraft dummy;
        //fprintf(stderr, "dbFlags offset %zu\n", (char*) &dummy.dbFlags - (char*) &dummy);

        exit(0);
    }

    // Parse the command line options
    parseCommandLine(argc, argv);

    configAfterParse();

    // Initialization
    //log_with_timestamp("%s starting up.", MODES_READSB_VARIANT);

    modesInit();

    receiverInit();

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
        mkdir_error(pathbuf, 0755, stderr);

        for (int i = 0; i < 256; i++) {
            snprintf(pathbuf, PATH_MAX, "%s/traces/%02x", Modes.json_dir, i);
            mkdir_error(pathbuf, 0755, stderr);
        }
    }

    if (Modes.state_parent_dir) {
        Modes.state_dir = malloc(PATH_MAX);
        snprintf(Modes.state_dir, PATH_MAX, "%s/internal_state", Modes.state_parent_dir);
    } else if (Modes.globe_history_dir) {
        Modes.state_dir = malloc(PATH_MAX);
        snprintf(Modes.state_dir, PATH_MAX, "%s/internal_state", Modes.globe_history_dir);
    }

    if (Modes.state_parent_dir && mkdir(Modes.state_parent_dir, 0755) && errno != EEXIST) {
        fprintf(stderr, "Unable to create state directory (%s): %s\n", Modes.state_parent_dir, strerror(errno));
    }

    if (Modes.globe_history_dir && mkdir(Modes.globe_history_dir, 0755) && errno != EEXIST) {
        fprintf(stderr, "Unable to create globe history directory (%s): %s\n", Modes.globe_history_dir, strerror(errno));
    }

    checkNewDay(mstime());
    checkNewDayAcas(mstime());

    if (Modes.state_dir) {
        readInternalState();
        if (Modes.writeInternalState) {
            Modes.writeInternalState = 0;
            writeInternalState();
        }
    }

    if (Modes.sdr_type != SDR_NONE) {
        threadCreate(&Threads.reader, NULL, readerEntryPoint, NULL);
    }

    threadCreate(&Threads.decode, NULL, decodeEntryPoint, NULL);

    threadCreate(&Threads.misc, NULL, miscEntryPoint, NULL);

    if (Modes.api || (Modes.json_dir && Modes.onlyBin < 2)) {
        // provide a json buffer
        Modes.apiUpdate = 1;
        apiBufferInit();
        if (Modes.api) {
            // after apiBufferInit()
            apiInit();
        }
    }

    if (Modes.json_globe_index) {
        threadCreate(&Threads.globeBin, NULL, globeBinEntryPoint, NULL);
    }

    if (Modes.json_dir) {
        threadCreate(&Threads.json, NULL, jsonEntryPoint, NULL);

        if (Modes.json_globe_index) {
            // globe_xxxx.json
            threadCreate(&Threads.globeJson, NULL, globeJsonEntryPoint, NULL);
        }

        free(writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()).buffer);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    threadCreate(&Threads.upkeep, NULL, upkeepEntryPoint, NULL);

    int mainEpfd = my_epoll_create(&Modes.exitSoonEventfd);
    struct epoll_event *events = NULL;
    int maxEvents = 1;
    epollAllocEvents(&events, &maxEvents);

    // init hungtimers
    pthread_mutex_lock(&Modes.hungTimerMutex);
    startWatch(&Modes.hungTimer1);
    startWatch(&Modes.hungTimer2);
    pthread_mutex_unlock(&Modes.hungTimerMutex);

    struct timespec mainloopTimer;
    startWatch(&mainloopTimer);
    while (!Modes.exit) {
        if (epoll_wait(mainEpfd, events, maxEvents, 5 * SECONDS) > 0) {
            if (Modes.exitSoon) {
                if (Modes.apiShutdownDelay) {
                    // delay for graceful api shutdown
                    fprintf(stderr, "Waiting %.3f seconds (--api-shutdown-delay) ...\n", Modes.apiShutdownDelay / 1000.0);
                    msleep(Modes.apiShutdownDelay);
                }
                // Signal to threads that program is exiting
                Modes.exit = Modes.exitSoon;
                uint64_t one = 1;
                ssize_t res = write(Modes.exitNowEventfd, &one, sizeof(one));
                MODES_NOTUSED(res);
            } else if (!Modes.exit) {
                // this shouldn't happen
                fprintf(stderr, "wtf? too2Mee7\n");
                msleep(50);
            }
            continue;
        }

        int64_t elapsed0 = lapWatch(&mainloopTimer);

        pthread_mutex_lock(&Modes.hungTimerMutex);
        int64_t elapsed1 = stopWatch(&Modes.hungTimer1);
        int64_t elapsed2 = stopWatch(&Modes.hungTimer2);
        pthread_mutex_unlock(&Modes.hungTimerMutex);


        if (elapsed0 > 10 * SECONDS) {
            fprintf(stderr, "<3> readsb was suspended for more than 5 seconds, this isn't healthy you know!\n");
        } else {
            if (elapsed1 > 60 * SECONDS && !Modes.synthetic_now) {
                fprintf(stderr, "<3>FATAL: priorityTasksRun() interval %.1f seconds! Trying for an orderly shutdown as well as possible!\n", (double) elapsed1 / SECONDS);
                fprintf(stderr, "<3>lockThreads() probably hung on %s\n", Modes.currentTask);
                setExit(2);
                break;
            }
            if (elapsed2 > 60 * SECONDS && !Modes.synthetic_now) {
                fprintf(stderr, "<3>FATAL: removeStale() interval %.1f seconds! Trying for an orderly shutdown as well as possible!\n", (double) elapsed2 / SECONDS);
                setExit(2);
                break;
            }
        }

        if (Modes.debug_provoke_segfault) {
            static int64_t next_fail;
            int64_t now = mstime();
            if (next_fail == 0) {
                next_fail = now + 15 * SECONDS;
            } else if (now > next_fail) {
                fprintf(stderr, "debug=Z -> provoking SEGFAULT now!\n");
                int *a = NULL;
                *a = 0;
            }
        }
    }

    close(mainEpfd);
    sfree(events);

    if (Modes.json_dir) {
        // mark this instance as deactivated, webinterface won't load
        char pathbuf[PATH_MAX];
        snprintf(pathbuf, PATH_MAX, "%s/receiver.json", Modes.json_dir);
        unlink(pathbuf);
    }

    threadSignalJoin(&Threads.misc);

    if (Modes.sdr_type != SDR_NONE) {
        threadSignalJoin(&Threads.reader);
    }

    threadSignalJoin(&Threads.upkeep);

    if (Modes.json_dir) {
        threadSignalJoin(&Threads.json);

        if (Modes.json_globe_index) {
            threadSignalJoin(&Threads.globeJson);
            threadSignalJoin(&Threads.globeBin);
        }
    }

    // after miscThread for the moment
    if (Modes.api) {
        apiCleanup();
    }
    if (Modes.apiUpdate) {
        // after apiCleanup()
        apiBufferCleanup();
    }

    threadSignalJoin(&Threads.decode);

    if (Modes.exit < 2) {
        // force stats to be done, this must happen before network cleanup as it checks network stuff
        Modes.next_stats_update = 0;
        priorityTasksRun();

        /* Cleanup network setup */
        cleanupNetwork();
    }

    threadDestroyAll();

    pthread_mutex_destroy(&Modes.traceDebugMutex);
    pthread_mutex_destroy(&Modes.hungTimerMutex);

    if (Modes.debug_bogus) {
        display_total_short_range_stats();
    }
    // If --stats were given, print statistics
    if (Modes.stats_display_interval) {
        display_total_stats();
    }

    if (Modes.allPool) {
        threadpool_destroy(Modes.allPool);
        destroy_task_group(Modes.allTasks);
    }

    if (Modes.tracePool) {
        threadpool_destroy(Modes.tracePool);
        destroy_task_group(Modes.traceTasks);
    }

    // frees aircraft when Modes.free_aircraft is set
    // writes state if Modes.state_dir is set
    Modes.free_aircraft = 1;
    writeInternalState();


    if (Modes.exit != 1) {
        log_with_timestamp("Abnormal exit.");
        cleanup_and_exit(1);
    }

    log_with_timestamp("Normal exit.");
    cleanup_and_exit(0);
}

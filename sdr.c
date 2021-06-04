// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr.c: generic SDR infrastructure
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2016-2017 Oliver Jowett <oliver@mutability.co.uk>
// Copyright (c) 2017 FlightAware LLC
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

#include "readsb.h"

#include "sdr_ifile.h"
#ifdef ENABLE_RTLSDR
#include "sdr_rtlsdr.h"
#endif
#ifdef ENABLE_BLADERF
#include "sdr_bladerf.h"
#include "sdr_ubladerf.h"
#endif
#ifdef ENABLE_PLUTOSDR
#include "sdr_plutosdr.h"
#endif

#include "sdr_beast.h"

#define SDR_TIMEOUT 5000 // timeout for sdr open / cancel / close calls in milliseconds

typedef struct {
    void (*initConfig)();
    bool(*handleOption)(int, char*);
    bool(*open)();
    void (*run)();
    void (*cancel)();
    void (*close)();
    const char *name;
    sdr_type_t sdr_type;
    pthread_t manageThread;
} sdr_handler;

static void noInitConfig() {
}

static bool noHandleOption(int argc, char *argv) {
    MODES_NOTUSED(argc);
    MODES_NOTUSED(argv);

    return false;
}

static bool noOpen() {
    fprintf(stderr, "No SDR device or file selected.\n");
    return true;
}

static void noRun() {
}

static void noCancel() {
}

static void noClose() {
}

static bool unsupportedOpen() {
    fprintf(stderr, "Support for this SDR type was not enabled in this build.\n");
    return false;
}

static sdr_handler sdr_handlers[] = {
#ifdef ENABLE_RTLSDR
    { rtlsdrInitConfig, rtlsdrHandleOption, rtlsdrOpen, rtlsdrRun, rtlsdrCancel, rtlsdrClose, "rtlsdr", SDR_RTLSDR, 0},
#endif

#ifdef ENABLE_BLADERF
    { bladeRFInitConfig, bladeRFHandleOption, bladeRFOpen, bladeRFRun, noCancel, bladeRFClose, "bladerf", SDR_BLADERF, 0},
    { ubladeRFInitConfig, ubladeRFHandleOption, ubladeRFOpen, ubladeRFRun, noCancel, ubladeRFClose, "ubladerf", SDR_MICROBLADERF, 0},
#endif

#ifdef ENABLE_PLUTOSDR
    { plutosdrInitConfig, plutosdrHandleOption, plutosdrOpen, plutosdrRun, noCancel, plutosdrClose, "plutosdr", SDR_PLUTOSDR, 0},
#endif

    { beastInitConfig, beastHandleOption, beastOpen, noRun, noCancel, noClose, "modesbeast", SDR_MODESBEAST, 0},
    { beastInitConfig, beastHandleOption, beastOpen, noRun, noCancel, noClose, "gnshulc", SDR_GNS, 0},
    { ifileInitConfig, ifileHandleOption, ifileOpen, ifileRun, noCancel, ifileClose, "ifile", SDR_IFILE, 0},
    { noInitConfig, noHandleOption, noOpen, noRun, noCancel, noClose, "none", SDR_NONE, 0},

    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, SDR_NONE, 0} /* must come last */
};

void sdrInitConfig() {
    // Default SDR is the first type available in the handlers array.
    // rather don't have a default SDR ....
    // Modes.sdr_type = sdr_handlers[0].sdr_type;

    for (int i = 0; sdr_handlers[i].name; ++i) {
        sdr_handlers[i].initConfig();
    }
}

bool sdrHandleOption(int argc, char *argv) {
    switch (argc) {
        case OptDeviceType:
            for (int i = 0; sdr_handlers[i].name; ++i) {
                if (!strcasecmp(sdr_handlers[i].name, argv)) {
                    Modes.sdr_type = sdr_handlers[i].sdr_type;
                    return true;
                }
            }
            break;
        default:
            for (int i = 0; sdr_handlers[i].sdr_type; ++i) {
                if (Modes.sdr_type == sdr_handlers[i].sdr_type) {
                    return sdr_handlers[i].handleOption(argc, argv);
                }
            }
    }

    fprintf(stderr, "SDR type '%s' not recognized; supported SDR types are:\n", argv);
    for (int i = 0; sdr_handlers[i].name; ++i) {
        fprintf(stderr, "  %s\n", sdr_handlers[i].name);
    }

    return false;
}

static sdr_handler *current_handler() {
    static sdr_handler unsupported_handler = {noInitConfig, noHandleOption, unsupportedOpen, noRun, noCancel, noClose, "unsupported", SDR_NONE, 0};

    for (int i = 0; sdr_handlers[i].name; ++i) {
        if (Modes.sdr_type == sdr_handlers[i].sdr_type) {
            return &sdr_handlers[i];
        }
    }

    return &unsupported_handler;
}

// avoid synchronous calls for all SDR handlers
// in particular rtlsdr_cancel_async() and rtlsdr_close() are suspect
// of never returning in some cases
//


// if a thread fails to terminate within milliseconds timeout
// return 0 on successful join, non-zero otherwise
// 50 ms granularity
static int tryJoinThread(pthread_t thread, int timeout) {
    int err = 0;
    int step = 50; // granularity
    int countdown = timeout / step + 1;
    while (countdown-- > 0 && (err = pthread_tryjoin_np(thread, NULL))) {
        msleep(step);
    }
    return err;
}


static void *sdrOpenThreadEntry(void *arg) {
    bool *res = (bool *) arg;
    *res = current_handler()->open();
    pthread_exit(NULL);
}

bool sdrOpen() {
    pthread_t manageThread = current_handler()->manageThread;
    bool res = false;

    pthread_create(&manageThread, NULL, sdrOpenThreadEntry, &res);

    // Wait on open handler to finish:
    if (tryJoinThread(manageThread, SDR_TIMEOUT)) {
        fprintf(stderr, "<3> FATAL: sdrOpen() timed out, will raise SIGKILL, clean exit not possible!\n");
        log_with_timestamp("Raising SIGKILL!");
        raise(SIGKILL);
    }
    return res;
}

void sdrRun() {
    return current_handler()->run();
}

static void *sdrCancelThreadEntry(void *arg) {
    MODES_NOTUSED(arg);
    current_handler()->cancel();
    pthread_exit(NULL);
}

static void *sdrCloseThreadEntry(void *arg) {
    MODES_NOTUSED(arg);
    current_handler()->close();
    pthread_exit(NULL);
}

void sdrCancel() {
    // Call cancel() asynchronously:
    pthread_create(&current_handler()->manageThread, NULL, sdrCancelThreadEntry, NULL);
}

bool sdrClose() {
    bool fatal = false;
    pthread_t manageThread = current_handler()->manageThread;

    // wait on the thread started by sdrCancel to finish
    if (tryJoinThread(manageThread, SDR_TIMEOUT)) {
        fprintf(stderr, "<3> FATAL: The SDR being stopped timed out, will raise SIGKILL!\n");
        log_with_timestamp("Raising SIGKILL!");
        raise(SIGKILL);
    }


    // Wait on readerThread to finish
    if (tryJoinThread(Modes.reader_thread, SDR_TIMEOUT)) {
        fprintf(stderr, "<3> FATAL: SDR receive thread termination timed out, will raise SIGKILL!\n");
        fatal = true;
    } else {
        pthread_cond_destroy(&Modes.data_cond); // Thread cleanup - only after the reader thread is dead!
        pthread_mutex_destroy(&Modes.data_mutex);
    }


    // Call close() asynchronously:
    pthread_create(&manageThread, NULL, sdrCloseThreadEntry, NULL);
    if (tryJoinThread(manageThread, SDR_TIMEOUT)) {
        fprintf(stderr, "<3> FATAL: Clean closing of the SDR resource timed out, will raise SIGKILL!\n");
        fatal = true;
    }

    if (fatal) {
        log_with_timestamp("Raising SIGKILL!");
        raise(SIGKILL);
    }

    return true;
}

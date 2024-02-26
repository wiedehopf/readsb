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
#ifdef ENABLE_HACKRF
#include "sdr_hackrf.h"
#endif
#ifdef ENABLE_PLUTOSDR
#include "sdr_plutosdr.h"
#endif
#ifdef ENABLE_SOAPYSDR
#include "sdr_soapy.h"
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

static bool noHandleOption(int key, char *arg) {
    MODES_NOTUSED(key);
    MODES_NOTUSED(arg);

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

#ifdef ENABLE_HACKRF
    { hackRFInitConfig, hackRFHandleOption, hackRFOpen, hackRFRun, noCancel, hackRFClose, "hackrf", SDR_HACKRF, 0},
#endif

#ifdef ENABLE_PLUTOSDR
    { plutosdrInitConfig, plutosdrHandleOption, plutosdrOpen, plutosdrRun, noCancel, plutosdrClose, "plutosdr", SDR_PLUTOSDR, 0},
#endif

#ifdef ENABLE_SOAPYSDR
    { soapyInitConfig, soapyHandleOption, soapyOpen, soapyRun, noCancel, soapyClose, "soapysdr", SDR_SOAPYSDR, 0 },
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

bool sdrHandleOption(int key, char *arg) {
    switch (key) {
        case OptDeviceType:
            for (int i = 0; sdr_handlers[i].name; ++i) {
                if (!strcasecmp(sdr_handlers[i].name, arg)) {
                    Modes.sdr_type = sdr_handlers[i].sdr_type;
                    return true;
                }
            }
            break;
        default:
            for (int i = 0; sdr_handlers[i].sdr_type; ++i) {
                if (Modes.sdr_type == sdr_handlers[i].sdr_type) {
                    return sdr_handlers[i].handleOption(key, arg);
                }
            }
    }

    fprintf(stderr, "SDR type '%s' not recognized; supported SDR types are:\n", arg);
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

bool sdrOpen() {
    return current_handler()->open();
}

bool sdrHasRun() {
    return (current_handler()->run != noRun);
}

void sdrRun() {
    // Create the thread that will read the data from the device.
    current_handler()->run();
}

void sdrCancel() {
    current_handler()->cancel();
}

void sdrClose() {
    current_handler()->close();
}

void lockReader() {
    pthread_mutex_lock(&Threads.reader.mutex);
}
void unlockReader() {
    pthread_mutex_unlock(&Threads.reader.mutex);
}
void wakeDecode() {
    pthread_cond_signal(&Threads.decode.cond);
}

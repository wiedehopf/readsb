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

typedef struct {
    void (*initConfig)();
    bool(*handleOption)(int, char*);
    bool(*open)();
    void (*run)();
    void (*close)();
    const char *name;
    sdr_type_t sdr_type;
    uint32_t padding;
} sdr_handler;

static void noInitConfig() {
}

static bool noHandleOption(int argc, char *argv) {
    MODES_NOTUSED(argc);
    MODES_NOTUSED(argv);

    return false;
}

static bool noOpen() {
    fprintf(stderr, "Net-only mode, no SDR device or file open.\n");
    return true;
}

static void noRun() {
}

static void noClose() {
}

static bool unsupportedOpen() {
    fprintf(stderr, "Support for this SDR type was not enabled in this build.\n");
    return false;
}

static sdr_handler sdr_handlers[] = {
#ifdef ENABLE_RTLSDR
    { rtlsdrInitConfig, rtlsdrHandleOption, rtlsdrOpen, rtlsdrRun, rtlsdrClose, "rtlsdr", SDR_RTLSDR, 0},
#endif

#ifdef ENABLE_BLADERF
    { bladeRFInitConfig, bladeRFHandleOption, bladeRFOpen, bladeRFRun, bladeRFClose, "bladerf", SDR_BLADERF, 0},
    { ubladeRFInitConfig, ubladeRFHandleOption, ubladeRFOpen, ubladeRFRun, ubladeRFClose, "ubladerf", SDR_MICROBLADERF, 0},
#endif

#ifdef ENABLE_PLUTOSDR
    { plutosdrInitConfig, plutosdrHandleOption, plutosdrOpen, plutosdrRun, plutosdrClose, "plutosdr", SDR_PLUTOSDR, 0},
#endif

    { beastInitConfig, beastHandleOption, beastOpen, noRun, noClose, "modesbeast", SDR_MODESBEAST, 0},
    { beastInitConfig, beastHandleOption, beastOpen, noRun, noClose, "gns5894", SDR_GNS, 0},
    { ifileInitConfig, ifileHandleOption, ifileOpen, ifileRun, ifileClose, "ifile", SDR_IFILE, 0},
    { noInitConfig, noHandleOption, noOpen, noRun, noClose, "none", SDR_NONE, 0},

    { NULL, NULL, NULL, NULL, NULL, NULL, SDR_NONE, 0} /* must come last */
};

void sdrInitConfig() {
    // Default SDR is the first type available in the handlers array.
    Modes.sdr_type = sdr_handlers[0].sdr_type;

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
    static sdr_handler unsupported_handler = {noInitConfig, noHandleOption, unsupportedOpen, noRun, noClose, "unsupported", SDR_NONE, 0};

    for (int i = 0; sdr_handlers[i].name; ++i) {
        if (Modes.sdr_type == sdr_handlers[i].sdr_type) {
            return &sdr_handlers[i];
        }
    }

    return &unsupported_handler;
}

bool sdrOpen() {
    return current_handler()->open();
}

void sdrRun() {
    return current_handler()->run();
}

void sdrClose() {
    current_handler()->close();
}

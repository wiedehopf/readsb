// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_rtlsdr.c: rtlsdr dongle support
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014-2017 Oliver Jowett <oliver@mutability.co.uk>
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
#include "sdr_rtlsdr.h"

#include <rtl-sdr.h>

#if (defined(__arm__) || defined(__aarch64__)) && !defined(DISABLE_RTLSDR_ZEROCOPY_WORKAROUND)
// Assume we need to use a bounce buffer to avoid performance problems on Pis running kernel 5.x and using zerocopy
#  define USE_BOUNCE_BUFFER
#endif

static struct {
    iq_convert_fn converter;
    struct converter_state *converter_state;
    rtlsdr_dev_t *dev;
    uint8_t *bounce_buffer;
    int ppm_error;
    bool digital_agc;
    int numgains;
    int *gains;
    int curGain;
    int tunerAgcEnabled;
} RTLSDR;

//
// =============================== RTLSDR handling ==========================
//

void rtlsdrInitConfig() {
    RTLSDR.dev = NULL;
    RTLSDR.digital_agc = false;
    RTLSDR.ppm_error = 0;
    RTLSDR.converter = NULL;
    RTLSDR.converter_state = NULL;
    RTLSDR.bounce_buffer = NULL;
    RTLSDR.numgains = 0;
    RTLSDR.gains = NULL;
    RTLSDR.tunerAgcEnabled = 0;
}

void rtlsdrSetGain() {
    if (Modes.gain == MODES_AUTO_GAIN || Modes.gain >= 520) {
        Modes.gain = 590;

        RTLSDR.tunerAgcEnabled = 1;

        fprintf(stderr, "rtlsdr: enabling tuner AGC\n");
        if (rtlsdr_set_tuner_gain_mode(RTLSDR.dev, 0)) {
            fprintf(stderr, "rtlsdr: enabling tuner AGC failed\n");
            return;
        }
    } else {

        int target = (Modes.gain == MODES_MAX_GAIN ? 9999 : Modes.gain);
        int closest = -1;

        for (int i = 0; i < RTLSDR.numgains; ++i) {
            if (closest == -1 || abs(RTLSDR.gains[i] - target) < abs(RTLSDR.gains[closest] - target))
                closest = i;
        }
        int newGain = RTLSDR.gains[closest];

        if (RTLSDR.tunerAgcEnabled) {
            if (rtlsdr_set_tuner_gain_mode(RTLSDR.dev, 1)) {
                fprintf(stderr, "rtlsdr: disabling tuner AGC failed\n");
                return;
            }
            RTLSDR.tunerAgcEnabled = 0;
            usleep(1000);
        }
        if (rtlsdr_set_tuner_gain(RTLSDR.dev, newGain)) {
            fprintf(stderr, "rtlsdr: setting tuner gain failed\n");
            return;
        } else {
            fprintf(stderr, "rtlsdr: tuner gain set to %.1f dB\n", newGain / 10.0);
            Modes.gain = newGain;
        }
    }
}

static void show_rtlsdr_devices() {
    int device_count = rtlsdr_get_device_count();
    fprintf(stderr, "rtlsdr: found %d device(s):\n", device_count);
    for (int i = 0; i < device_count; i++) {
        char vendor[256], product[256], serial[256];

        if (rtlsdr_get_device_usb_strings(i, vendor, product, serial) != 0) {
            fprintf(stderr, "  %d:  unable to read device details\n", i);
        } else {
            fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
        }
    }
}

static int find_device_index(char *s) {
    int device_count = rtlsdr_get_device_count();
    if (!device_count) {
        return -1;
    }

    /* does string look like raw id number */
    if (!strcmp(s, "0")) {
        return 0;
    } else if (s[0] != '0') {
        char *s2;
        int device = (int) strtol(s, &s2, 10);
        if (s2[0] == '\0' && device >= 0 && device < device_count) {
            return device;
        }
    }

    /* does string exact match a serial */
    for (int i = 0; i < device_count; i++) {
        char serial[256];
        if (rtlsdr_get_device_usb_strings(i, NULL, NULL, serial) == 0 && !strcmp(s, serial)) {
            return i;
        }
    }

    /* does string prefix match a serial */
    for (int i = 0; i < device_count; i++) {
        char serial[256];
        if (rtlsdr_get_device_usb_strings(i, NULL, NULL, serial) == 0 && !strncmp(s, serial, strlen(s))) {
            return i;
        }
    }

    /* does string suffix match a serial */
    for (int i = 0; i < device_count; i++) {
        char serial[256];
        if (rtlsdr_get_device_usb_strings(i, NULL, NULL, serial) == 0 && strlen(s) < strlen(serial) && !strcmp(serial + strlen(serial) - strlen(s), s)) {
            return i;
        }
    }

    return -1;
}

bool rtlsdrHandleOption(int key, char *arg) {
    switch (key) {
        case OptRtlSdrEnableAgc:
            RTLSDR.digital_agc = true;
            break;
        case OptRtlSdrPpm:
            RTLSDR.ppm_error = atoi(arg);
            break;
        default:
            return false;
    }
    return true;
}

bool rtlsdrOpen(void) {
    if (!rtlsdr_get_device_count()) {
        fprintf(stderr, "FATAL: rtlsdr: no supported devices found.\n");
        return false;
    }

    int dev_index = 0;
    if (Modes.dev_name) {
        if ((dev_index = find_device_index(Modes.dev_name)) < 0) {
            fprintf(stderr, "FATAL: rtlsdr: no device matching '%s' found.\n", Modes.dev_name);
            show_rtlsdr_devices();
            return false;
        }
    }

    char manufacturer[256];
    char product[256];
    char serial[256];
    if (rtlsdr_get_device_usb_strings(dev_index, manufacturer, product, serial) < 0) {
        fprintf(stderr, "FATAL: rtlsdr: error querying device #%d: %s\n", dev_index, strerror(errno));
        return false;
    }

    fprintf(stderr, "rtlsdr: using device #%d: %s (%s, %s, SN %s)\n",
            dev_index, rtlsdr_get_device_name(dev_index),
            manufacturer, product, serial);

    if (rtlsdr_open(&RTLSDR.dev, dev_index) < 0) {
        fprintf(stderr, "FATAL: rtlsdr: error opening the RTLSDR device: %s\n",
                strerror(errno));
        return false;
    }

    RTLSDR.numgains = rtlsdr_get_tuner_gains(RTLSDR.dev, NULL);
    if (RTLSDR.numgains <= 0) {
        fprintf(stderr, "FATAL: rtlsdr: error getting tuner gains\n");
        return false;
    }

    RTLSDR.gains = cmalloc(RTLSDR.numgains * sizeof (int));
    if (rtlsdr_get_tuner_gains(RTLSDR.dev, RTLSDR.gains) != RTLSDR.numgains) {
        fprintf(stderr, "FATAL: rtlsdr: error getting tuner gains\n");
        free(RTLSDR.gains);
        return false;
    }

    rtlsdrSetGain();

    if (RTLSDR.digital_agc) {
        fprintf(stderr, "rtlsdr: enabling digital AGC\n");
        rtlsdr_set_agc_mode(RTLSDR.dev, 1);
    }

    // Set frequency, sample rate, and reset the device

    rtlsdr_set_freq_correction(RTLSDR.dev, RTLSDR.ppm_error);
    rtlsdr_set_center_freq(RTLSDR.dev, Modes.freq);
    rtlsdr_set_sample_rate(RTLSDR.dev, (unsigned) Modes.sample_rate);
#ifdef ENABLE_RTLSDR_BIASTEE
    // Enable or disable bias tee on GPIO pin 0. (Works only for rtl-sdr.com v3 dongles)
    rtlsdr_set_bias_tee(RTLSDR.dev, Modes.biastee);
#endif

    rtlsdr_reset_buffer(RTLSDR.dev);

    RTLSDR.converter = init_converter(INPUT_UC8,
            Modes.sample_rate,
            Modes.dc_filter,
            &RTLSDR.converter_state);
    if (!RTLSDR.converter) {
        fprintf(stderr, "FATAL: rtlsdr: can't initialize sample converter\n");
        rtlsdrClose();
        return false;
    }

#ifdef USE_BOUNCE_BUFFER
    if (!(RTLSDR.bounce_buffer = cmalloc(Modes.sdr_buf_size))) {
        fprintf(stderr, "FATAL: rtlsdr: can't allocate bounce buffer\n");
        rtlsdrClose();
        return false;
    }
#endif

    return true;
}

static struct timespec rtlsdr_thread_cpu;

void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx) {
    struct mag_buf *outbuf;
    struct mag_buf *lastbuf;
    uint32_t slen;
    unsigned next_free_buffer;
    unsigned free_bufs;
    int64_t block_duration;

    static int dropping = 0;
    static uint64_t sampleCounter = 0;

    static int antiSpam;
    static int antiSpam2;

    int64_t sysMicroseconds = mono_micro_seconds();
    int64_t sysTimestamp = mstime();

    // simulating missed USB packets:
    if (0) {
        static int fail;
        if (fail++ % (35 * 20) == 0) {
            fprintf(stderr, "ignoring rtsdrCallback\n");
            return;
        }
    }

    MODES_NOTUSED(ctx);

    // Lock the data buffer variables before accessing them
    lockReader();

    next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;

    unlockReader();

    if (len != Modes.sdr_buf_size) {
        fprintf(stderr, "weirdness: rtlsdr gave us a block with an unusual size (got %u bytes, expected %u bytes)\n",
                (unsigned) len, (unsigned) Modes.sdr_buf_size);
        if (len > Modes.sdr_buf_size) {
            // wat?! Discard the start.
            unsigned discard = (len - Modes.sdr_buf_size + 1) / 2;
            outbuf->dropped += discard;
            buf += discard * 2;
            len -= discard * 2;
        }
    }
    slen = len / 2; // Drops any trailing odd sample, that's OK

    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS / 2)) {
        // FIFO is full. Drop this block.
        dropping = 1;
        outbuf->dropped += slen;
        sampleCounter += slen;

        // make extra sure that the decode thread isn't sleeping
        wakeDecode();

        if (--antiSpam <= 0 && !Modes.exit) {
            fprintf(stderr, "FIFO dropped, suppressing this message for 30 seconds.\n");
            antiSpam = 300;
        }
        return;
    }
    dropping = 0;

    // Compute the sample timestamp and system timestamp for the start of the block
    outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
    sampleCounter += slen;

    if (Modes.debug_sampleCounter && --antiSpam2 <= 0) {
        fprintf(stderr, "sampleTimestamp: %020llu\n", (unsigned long long) outbuf->sampleTimestamp);
        antiSpam2 = 3000;
    }

    // Get the approx system time for the start of this block
    block_duration = 1e3 * slen / Modes.sample_rate;

    outbuf->sysTimestamp = sysTimestamp;
    outbuf->sysMicroseconds = sysMicroseconds;

    outbuf->sysTimestamp -= block_duration;
    outbuf->sysMicroseconds -= block_duration * 1000;

    // Copy trailing data from last block (or reset if not valid)
    if (outbuf->dropped == 0) {
        memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof (uint16_t));
    } else {
        memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
    }

#ifdef USE_BOUNCE_BUFFER
    // Work around zero-copy slowness on Pis with 5.x kernels
    memcpy(RTLSDR.bounce_buffer, buf, slen * 2);
    buf = RTLSDR.bounce_buffer;
#endif

    // Convert the new data
    outbuf->length = slen;
    RTLSDR.converter(buf, &outbuf->data[Modes.trailing_samples], slen, RTLSDR.converter_state, &outbuf->mean_level, &outbuf->mean_power);

    // Push the new data to the demodulation thread
    lockReader();

    Modes.mag_buffers[next_free_buffer].dropped = 0;
    Modes.mag_buffers[next_free_buffer].length = 0; // just in case
    Modes.first_free_buffer = next_free_buffer;

    // accumulate CPU while holding the mutex, and restart measurement
    end_cpu_timing(&rtlsdr_thread_cpu, &Modes.reader_cpu_accumulator);
    start_cpu_timing(&rtlsdr_thread_cpu);

    wakeDecode();
    unlockReader();
}

void rtlsdrRun() {
    if (!RTLSDR.dev) {
        return;
    }

    start_cpu_timing(&rtlsdr_thread_cpu);

    rtlsdr_read_async(RTLSDR.dev, rtlsdrCallback, NULL, MODES_RTL_BUFFERS, Modes.sdr_buf_size);
    if (!Modes.exit) {
        fprintf(stderr,"FATAL: rtlsdr_read_async returned unexpectedly, probably lost the USB device, bailing out\n");
    }
}

void rtlsdrCancel() {
    rtlsdr_cancel_async(RTLSDR.dev); // interrupt read_async
}

void rtlsdrClose() {
    if (RTLSDR.dev) {
        rtlsdr_close(RTLSDR.dev);
        RTLSDR.dev = NULL;
    }

    if (RTLSDR.converter) {
        cleanup_converter(&RTLSDR.converter_state);
        RTLSDR.converter = NULL;
    }

    free(RTLSDR.gains);
    free(RTLSDR.bounce_buffer);
    RTLSDR.bounce_buffer = NULL;
}

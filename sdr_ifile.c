// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_ifile.c: "file" SDR support
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
#include "sdr_ifile.h"

static struct {
    input_format_t input_format;
    int fd;
    unsigned bytes_per_sample;
    bool throttle;
    uint8_t padding1;
    uint16_t padding2;
    void *readbuf;
    iq_convert_fn converter;
    struct converter_state *converter_state;
    const char *filename;
} ifile;

void ifileInitConfig(void) {
    ifile.filename = NULL;
    ifile.input_format = INPUT_UC8;
    ifile.throttle = false;
    ifile.fd = -1;
    ifile.bytes_per_sample = 0;
    ifile.readbuf = NULL;
    ifile.converter = NULL;
    ifile.converter_state = NULL;
    Modes.synthetic_now = Modes.startup_time;
}

bool ifileHandleOption(int key, char *arg) {
    switch (key) {
        case OptIfileName:
            ifile.filename = strdup(arg);
            Modes.sdr_type = SDR_IFILE;
            break;
        case OptIfileFormat:
            if (!strcasecmp(arg, "uc8")) {
                ifile.input_format = INPUT_UC8;
            } else if (!strcasecmp(arg, "sc16")) {
                ifile.input_format = INPUT_SC16;
            } else if (!strcasecmp(arg, "sc16q11")) {
                ifile.input_format = INPUT_SC16Q11;
            } else {
                fprintf(stderr, "Input format '%s' not understood (supported values: UC8, SC16, SC16Q11)\n",
                        arg);
                return false;
            }
            break;
        case OptIfileThrottle:
            ifile.throttle = true;
            break;
        default:
            return false;
    }
    return true;
}

//
//=========================================================================
//
// This is used when --ifile is specified in order to read data from file
// instead of using an RTLSDR device
//

bool ifileOpen(void) {
    if (!ifile.filename) {
        fprintf(stderr, "SDR type 'ifile' requires an --ifile argument\n");
        return false;
    }

    if (!strcmp(ifile.filename, "-")) {
        ifile.fd = STDIN_FILENO;
    } else if ((ifile.fd = open(ifile.filename, O_RDONLY)) < 0) {
        fprintf(stderr, "ifile: could not open %s: %s\n",
                ifile.filename, strerror(errno));
        return false;
    }

    switch (ifile.input_format) {
        case INPUT_UC8:
            ifile.bytes_per_sample = 2;
            break;
        case INPUT_SC16:
        case INPUT_SC16Q11:
            ifile.bytes_per_sample = 4;
            break;
        default:
            fprintf(stderr, "ifile: unhandled input format\n");
            ifileClose();
            return false;
    }

    if (!(ifile.readbuf = cmalloc(Modes.sdr_buf_samples * ifile.bytes_per_sample))) {
        fprintf(stderr, "ifile: failed to allocate read buffer\n");
        ifileClose();
        return false;
    }

    ifile.converter = init_converter(ifile.input_format,
            Modes.sample_rate,
            Modes.dc_filter,
            &ifile.converter_state);
    if (!ifile.converter) {
        fprintf(stderr, "ifile: can't initialize sample converter\n");
        ifileClose();
        return false;
    }

    return true;
}

void ifileRun() {
    if (ifile.fd < 0)
        return;

    int eof = 0;
    struct timespec next_buffer_delivery;

    struct timespec thread_cpu;
    start_cpu_timing(&thread_cpu);

    uint64_t sampleCounter = 0;

    clock_gettime(CLOCK_MONOTONIC, &next_buffer_delivery);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    lockReader();
    while (!Modes.exit && !eof) {
        ssize_t nread, toread;
        void *r;
        struct mag_buf *outbuf, *lastbuf;
        unsigned next_free_buffer;
        unsigned slen;

        next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
        if (next_free_buffer == Modes.first_filled_buffer) {
            // no space for output yet
            threadTimedWait(&Threads.reader, &ts, 50);
            continue;
        }

        outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
        lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
        unlockReader();

        // Compute the sample timestamp for the start of the block
        outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;

        // Copy trailing data from last block (or reset if not valid)
        if (lastbuf->length >= Modes.trailing_samples) {
            memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof (uint16_t));
        } else {
            memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
        }

        // Get the system time for the start of this block
        outbuf->sysTimestamp = outbuf->sampleTimestamp / 12000U + Modes.startup_time;
        outbuf->sysMicroseconds = outbuf->sampleTimestamp / 12U + Modes.startup_time * 1000;

        //fprintf(stderr, "sysTimestamp %.3f\n", outbuf->sysTimestamp / 1000.0);

        toread = Modes.sdr_buf_samples * ifile.bytes_per_sample;
        r = ifile.readbuf;
        while (toread) {
            nread = read(ifile.fd, r, toread);
            if (nread <= 0) {
                if (nread < 0) {
                    fprintf(stderr, "ifile: error reading input file: %s\n", strerror(errno));
                }
                // Done.
                eof = 1;
                break;
            }
            r += nread;
            toread -= nread;
        }

        slen = outbuf->length = Modes.sdr_buf_samples - toread / ifile.bytes_per_sample;
        sampleCounter += slen;

        // Convert the new data
        ifile.converter(ifile.readbuf, &outbuf->data[Modes.trailing_samples], slen, ifile.converter_state, &outbuf->mean_level, &outbuf->mean_power);

        if (ifile.throttle || Modes.interactive) {
            // Wait until we are allowed to release this buffer to the main thread
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_buffer_delivery, NULL) == EINTR)
                ;

            // compute the time we can deliver the next buffer.
            next_buffer_delivery.tv_nsec += outbuf->length * 1e9 / Modes.sample_rate;
            normalize_timespec(&next_buffer_delivery);
        }

        // Push the new data to the main thread
        lockReader();
        Modes.first_free_buffer = next_free_buffer;
        // accumulate CPU while holding the mutex, and restart measurement
        end_cpu_timing(&thread_cpu, &Modes.reader_cpu_accumulator);
        start_cpu_timing(&thread_cpu);
        wakeDecode();
    }

    // Wait for the main thread to consume all data (reader still locked here)
    while (!Modes.exit && Modes.first_filled_buffer != Modes.first_free_buffer) {
        wakeDecode();
        threadTimedWait(&Threads.reader, &ts, 50);
    }

    Modes.exit = 1;
    unlockReader();
}

void ifileClose() {
    if (ifile.converter) {
        cleanup_converter(&ifile.converter_state);
        ifile.converter = NULL;
    }

    if (ifile.readbuf) {
        free(ifile.readbuf);
        ifile.readbuf = NULL;
    }

    if (ifile.fd >= 0 && ifile.fd != STDIN_FILENO) {
        close(ifile.fd);
        ifile.fd = -1;
    }
}

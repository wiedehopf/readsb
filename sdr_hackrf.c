// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_hackrf.c: HackRF support
//
// Copyright (c) 2023 Timothy Mullican <timothy.j.mullican@gmail.com>
//
// This code is based on dump1090_sdrplus.
//
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
// HackRF One support added by Ilker Temir <ilker@ilkertemir.com>
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

#include <libhackrf/hackrf.h>
#include <inttypes.h>

static struct {
    const char *device_str;
    unsigned block_size;
    hackrf_device *device;
    iq_convert_fn converter;
    struct converter_state *converter_state;
    // HackRF has three gain controls
    //   RF ("amp", 0 or ~11 dB)
    //   IF ("lna", 0 to 40 dB in 8 dB steps)
    //   baseband ("vga", 0 to 62 dB in 2 dB steps)
    bool rf_gain;
    unsigned vga_gain;
} hackRF;

void hackRFInitConfig() {
    hackRF.device_str = NULL;
    hackRF.device = NULL;
    hackRF.rf_gain = false;
    hackRF.vga_gain = 48;
}

bool hackRFHandleOption(int key, char *arg) {
    switch (key) {
	case OptHackRfGainEnable:
            hackRF.rf_gain = true;
	    break;
	case OptHackRfVgaGain:
	    hackRF.vga_gain = atoi(arg);
	    break;
        default:
            return false;
    }
    return true;
}

bool hackRFOpen() {
    if (hackRF.device) {
        return true;
    }

    int status;

    status = hackrf_init();
    if ((status = hackrf_init()) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_init failed: %s\n", hackrf_error_name(status));
	goto error;
    }

    fprintf(stderr, "Opening HackRF: %s\n", Modes.dev_name);
    if (Modes.dev_name) {
	status = hackrf_open_by_serial(Modes.dev_name, &hackRF.device);
    } else {
        status = hackrf_open(&hackRF.device);
    }
    if (status != HACKRF_SUCCESS) {
        fprintf(stderr, "Failed to open hackRF: %s\n", hackrf_error_name(status));
        goto error;
    }

    if ((status = hackrf_set_sample_rate(hackRF.device, Modes.sample_rate)) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_set_sample_rate failed: %s\n", hackrf_error_name(status));
        goto error;
    }

    if ((status = hackrf_set_freq(hackRF.device, Modes.freq)) != HACKRF_SUCCESS ) {
        fprintf(stderr, "hackrf_set_freq failed: %s\n", hackrf_error_name(status));
        goto error;
    }

    if (Modes.gain == MODES_AUTO_GAIN || Modes.gain >= 400) {
        // hackRF doesn't have automatic gain control
        Modes.gain = 400;
    }
    if (Modes.gain < 0) {
	// gain is unsigned
        Modes.gain = 0;
    }

    if (hackRF.rf_gain) {
        if ((status = hackrf_set_amp_enable(hackRF.device, 1)) != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_amp_enable failed: %s\n", hackrf_error_name(status));
	    goto error;
	}
    }

    if ((status = hackrf_set_lna_gain(hackRF.device, Modes.gain / 10)) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_set_lna_gain failed: %s\n", hackrf_error_name(status));
	goto error;
    }

    if ((status = hackrf_set_vga_gain(hackRF.device, hackRF.vga_gain)) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_set_vga_gain failed: %s\n", hackrf_error_name(status));
	goto error;
    }

    if (Modes.biastee) {
        fprintf(stderr, "Enabling Bias Tee\n");
        if ((status = hackrf_set_antenna_enable(hackRF.device, 1)) != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_antenna_enable failed: %s\n", hackrf_error_name(status));
        }
    }

    fprintf (stderr, "HackRF successfully initialized "
                     "(AMP Enable: %i, LNA Gain: %i, VGA Gain: %i).\n",
                     Modes.biastee, Modes.gain / 10, hackRF.vga_gain);

    hackRF.converter = init_converter(INPUT_UC8,
            Modes.sample_rate,
            Modes.dc_filter,
            &hackRF.converter_state);
    if (!hackRF.converter) {
        fprintf(stderr, "can't initialize sample converter\n");
        goto error;
    }

    return true;

error:
    if (hackRF.device) {
        hackrf_close(hackRF.device);
	hackrf_exit();
        hackRF.device = NULL;
    }
    return false;
}

static struct timespec thread_cpu;

static int hackrfCallback(hackrf_transfer *transfer) {
    struct mag_buf *outbuf;
    struct mag_buf *lastbuf;
    uint32_t slen;
    unsigned next_free_buffer;
    unsigned free_bufs;
    int64_t block_duration;

    static int was_odd = 0;
    static int dropping = 0;
    static uint64_t sampleCounter = 0;

    uint8_t *buf = transfer->buffer;
    uint32_t len = transfer->buffer_length;

    int64_t sysMicroseconds = mono_micro_seconds();
    int64_t sysTimestamp = mstime();

    // Lock the data buffer variables before accessing them
    lockReader();

    // HackRF one returns signed IQ values, convert them to unsigned
    for (uint32_t i = 0; i < len; i++) {
        buf[i] ^= 0x80; // Flip the MSB to convert
    }

    next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;

    if (len != Modes.sdr_buf_size) {
        fprintf(stderr, "weirdness: hackRF gave us a block with an unusual size (got %u bytes, expected %u bytes)\n",
                (unsigned) len, (unsigned) Modes.sdr_buf_size);
        if (len > Modes.sdr_buf_size) {
            unsigned discard = (len - Modes.sdr_buf_size + 1) / 2;
            outbuf->dropped += discard;
            buf += discard * 2;
            len -= discard * 2;
        }
    }
    
    if (was_odd) {
        ++buf;
	--len;
	++outbuf->dropped;
    }

    was_odd = (len & 1);
    slen = len / 2; // Drops any trailing odd sample, that's OK

    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS / 2)) {
        // FIFO is full. Drop this block.
        dropping = 1;
        outbuf->dropped += slen;
        sampleCounter += slen;
        // make extra sure that the decode thread isn't sleeping
        unlockReader();
        return 1;
    }

    dropping = 0;
    unlockReader();

    // Compute the sample timestamp and system timestamp for the start of the block
    outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
    sampleCounter += slen;
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

    // Convert the new data
    outbuf->length = slen;
    hackRF.converter(buf, &outbuf->data[Modes.trailing_samples], slen, hackRF.converter_state, &outbuf->mean_level, &outbuf->mean_power);
    // Push the new data to the demodulation thread
    lockReader();

    Modes.mag_buffers[next_free_buffer].dropped = 0;
    Modes.mag_buffers[next_free_buffer].length = 0; // just in case
    Modes.first_free_buffer = next_free_buffer;

    // accumulate CPU while holding the mutex, and restart measurement
    end_cpu_timing(&thread_cpu, &Modes.reader_cpu_accumulator);
    start_cpu_timing(&thread_cpu);

    wakeDecode();
    unlockReader();

    return 0;
}

void hackRFRun() {
    if (!hackRF.device) {
        return;
    }

    start_cpu_timing(&thread_cpu);

    int status;
    if ((status = hackrf_start_rx(hackRF.device, hackrfCallback, NULL)) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_start_rx failed: %s\n", hackrf_error_name(status));
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    while (!Modes.exit) {
        threadTimedWait(&Threads.reader, &ts, 50);
    }
}

void hackRFClose() {
    hackrf_stop_rx(hackRF.device);

    if (hackRF.converter) {
        cleanup_converter(&hackRF.converter_state);
        hackRF.converter = NULL;
    }

    if (hackRF.device) {
        hackrf_close(hackRF.device);
        hackRF.device = NULL;
    }
}

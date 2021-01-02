// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_pluto.c: PlutoSDR support
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
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

#include <iio.h>
#include <ad9361.h>
#include "readsb.h"
#include "sdr_plutosdr.h"

static struct {
    input_format_t input_format;
    int dev_index;
    struct iio_channel *rx0_i;
    struct iio_channel *rx0_q;
    struct iio_buffer  *rxbuf;
    struct iio_context *ctx;
    struct iio_device *dev;
    int16_t *readbuf;
    iq_convert_fn converter;
    struct converter_state *converter_state;
    char *uri;
    char *network;
} PLUTOSDR;

static struct timespec thread_cpu;

void plutosdrInitConfig()
{
    PLUTOSDR.readbuf = NULL;
    PLUTOSDR.converter = NULL;
    PLUTOSDR.converter_state = NULL;
    PLUTOSDR.uri = NULL;
    PLUTOSDR.network = strdup("pluto.local");
}

bool plutosdrHandleOption(int argc, char *argv)
{
    switch (argc) {
        case OptPlutoUri:
            PLUTOSDR.uri = strdup(argv);
            break;
        case OptPlutoNetwork:
            PLUTOSDR.network = strdup(argv);
            break;
    }
    return true;
}

bool plutosdrOpen()
{
    PLUTOSDR.ctx = iio_create_default_context();
    if (PLUTOSDR.ctx == NULL && PLUTOSDR.uri != NULL) {
        PLUTOSDR.ctx = iio_create_context_from_uri(PLUTOSDR.uri);
    }

    else if (PLUTOSDR.ctx == NULL) {
        PLUTOSDR.ctx = iio_create_network_context(PLUTOSDR.network);
    }

    if (PLUTOSDR.ctx == NULL) {
        char buf[1024];
        iio_strerror(errno, buf, sizeof(buf));
        fprintf(stderr, "plutosdr: Failed creating IIO context: %s\n", buf);
        return false;
    }

    struct iio_scan_context *ctx;
    struct iio_context_info **info;
    ctx = iio_create_scan_context(NULL, 0);
    if (ctx) {
        int info_count = iio_scan_context_get_info_list(ctx, &info);
        if(info_count > 0) {
            fprintf(stderr, "plutosdr: %s\n", iio_context_info_get_description(info[0]));
            iio_context_info_list_free(info);
        }
	iio_scan_context_destroy(ctx);
    }

    int device_count = iio_context_get_devices_count(PLUTOSDR.ctx);
    if (!device_count) {
        fprintf(stderr, "plutosdr: No supported PLUTOSDR devices found.\n");
        plutosdrClose();
    }
    fprintf(stderr, "plutosdr: Context has %d device(s).\n", device_count);

    PLUTOSDR.dev = iio_context_find_device(PLUTOSDR.ctx, "cf-ad9361-lpc");

    if (PLUTOSDR.dev == NULL) {
        fprintf(stderr, "plutosdr: Error opening the PLUTOSDR device: %s\n", strerror(errno));
        plutosdrClose();
    }

    struct iio_channel* phy_chn = iio_device_find_channel(iio_context_find_device(PLUTOSDR.ctx, "ad9361-phy"), "voltage0", false);
    iio_channel_attr_write(phy_chn, "rf_port_select", "A_BALANCED");
    iio_channel_attr_write_longlong(phy_chn, "rf_bandwidth", (long long)1750000);
    iio_channel_attr_write_longlong(phy_chn, "sampling_frequency", (long long)Modes.sample_rate);

    if (Modes.gain == MODES_AUTO_GAIN) {
        iio_channel_attr_write(phy_chn, "gain_control_mode", "slow_attack");
    } else {
        // We use 10th of dB here, max is 77dB up to 1300MHz
        if (Modes.gain > 770)
            Modes.gain = 770;
        iio_channel_attr_write(phy_chn, "gain_control_mode", "manual");
        iio_channel_attr_write_longlong(phy_chn, "hardwaregain", Modes.gain / 10);
    }

    iio_channel_attr_write_bool(
        iio_device_find_channel(iio_context_find_device(PLUTOSDR.ctx, "ad9361-phy"), "altvoltage1", true)
        , "powerdown", true); // Turn OFF TX LO

    iio_channel_attr_write_longlong(
            iio_device_find_channel(iio_context_find_device(PLUTOSDR.ctx, "ad9361-phy"), "altvoltage0", true)
            , "frequency", (long long)Modes.freq); // Set RX LO frequency

    PLUTOSDR.rx0_i = iio_device_find_channel(PLUTOSDR.dev, "voltage0", false);
    if (!PLUTOSDR.rx0_i)
        PLUTOSDR.rx0_i = iio_device_find_channel(PLUTOSDR.dev, "altvoltage0", false);

    PLUTOSDR.rx0_q = iio_device_find_channel(PLUTOSDR.dev, "voltage1", false);
    if (!PLUTOSDR.rx0_q)
        PLUTOSDR.rx0_q = iio_device_find_channel(PLUTOSDR.dev, "altvoltage1", false);

    ad9361_set_bb_rate(iio_context_find_device(PLUTOSDR.ctx, "ad9361-phy"), Modes.sample_rate);

    iio_channel_enable(PLUTOSDR.rx0_i);
    iio_channel_enable(PLUTOSDR.rx0_q);

    PLUTOSDR.rxbuf = iio_device_create_buffer(PLUTOSDR.dev, MODES_MAG_BUF_SAMPLES, false);

    if (!PLUTOSDR.rxbuf) {
        perror("plutosdr: Could not create RX buffer");
    }

    if (!(PLUTOSDR.readbuf = malloc(MODES_RTL_BUF_SIZE * 4))) {
        fprintf(stderr, "plutosdr: Failed to allocate read buffer\n");
        plutosdrClose();
        return false;
    }

    PLUTOSDR.converter = init_converter(INPUT_SC16,
            Modes.sample_rate,
            Modes.dc_filter,
            &PLUTOSDR.converter_state);
    if (!PLUTOSDR.converter) {
        fprintf(stderr, "plutosdr: Can't initialize sample converter\n");
        plutosdrClose();
        return false;
    }
    return true;
}

static void plutosdrCallback(int16_t *buf, uint32_t len) {
    struct mag_buf *outbuf;
    struct mag_buf *lastbuf;
    uint32_t slen;
    unsigned next_free_buffer;
    unsigned free_bufs;
    unsigned block_duration;

    static int was_odd = 0;
    static int dropping = 0;
    static uint64_t sampleCounter = 0;

    pthread_mutex_lock(&Modes.data_mutex);

    next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;

    if (len != MODES_RTL_BUF_SIZE) {
        fprintf(stderr, "weirdness: plutosdr gave us a block with an unusual size (got %u bytes, expected %u bytes)\n",
                (unsigned) len, (unsigned) MODES_RTL_BUF_SIZE);

        if (len > MODES_RTL_BUF_SIZE) {
            unsigned discard = (len - MODES_RTL_BUF_SIZE + 1) / 2;
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
    slen = len / 2;

    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS / 2)) {
        dropping = 1;
        outbuf->dropped += slen;
        sampleCounter += slen;
        pthread_mutex_unlock(&Modes.data_mutex);
        return;
    }

    dropping = 0;
    pthread_mutex_unlock(&Modes.data_mutex);

    outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
    sampleCounter += slen;
    block_duration = 1e3 * slen / Modes.sample_rate;
    outbuf->sysTimestamp = mstime() - block_duration;

    if (outbuf->dropped == 0) {
        memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof (uint16_t));
    } else {
        memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
    }

    outbuf->length = slen;
    PLUTOSDR.converter(buf, &outbuf->data[Modes.trailing_samples], slen, PLUTOSDR.converter_state, &outbuf->mean_level, &outbuf->mean_power);

    pthread_mutex_lock(&Modes.data_mutex);

    Modes.mag_buffers[next_free_buffer].dropped = 0;
    Modes.mag_buffers[next_free_buffer].length = 0;
    Modes.first_free_buffer = next_free_buffer;

    end_cpu_timing(&thread_cpu, &Modes.reader_cpu_accumulator);
    start_cpu_timing(&thread_cpu);

    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);
}

void plutosdrRun() {
    void *p_dat, *p_end;
    ptrdiff_t p_inc;

    if (!PLUTOSDR.dev) {
        return;
    }
    start_cpu_timing(&thread_cpu);

    while (!Modes.exit) {
        int16_t *p = PLUTOSDR.readbuf;
        uint32_t len = (uint32_t) iio_buffer_refill(PLUTOSDR.rxbuf) / 2;
        p_inc = iio_buffer_step(PLUTOSDR.rxbuf);
        p_end = iio_buffer_end(PLUTOSDR.rxbuf);
        p_dat = iio_buffer_first(PLUTOSDR.rxbuf, PLUTOSDR.rx0_i);

        for (p_dat = iio_buffer_first(PLUTOSDR.rxbuf, PLUTOSDR.rx0_i); p_dat < p_end; p_dat += p_inc) {
            *p++ = ((int16_t*) p_dat)[0]; // Real (I)
            *p++ = ((int16_t*) p_dat)[1]; // Imag (Q)
        }
        plutosdrCallback(PLUTOSDR.readbuf, len);
    }
}

void plutosdrClose() {
    if(PLUTOSDR.readbuf) {
        free(PLUTOSDR.readbuf);
    }

    if (PLUTOSDR.rxbuf) {
        iio_buffer_destroy(PLUTOSDR.rxbuf);
    }

    if (PLUTOSDR.rx0_i) {
        iio_channel_disable(PLUTOSDR.rx0_i);
    }

    if (PLUTOSDR.rx0_q) {
        iio_channel_disable(PLUTOSDR.rx0_q);
    }

    if (PLUTOSDR.ctx) {
        iio_context_destroy(PLUTOSDR.ctx);
    }

    free(PLUTOSDR.network);
    free(PLUTOSDR.uri);
}

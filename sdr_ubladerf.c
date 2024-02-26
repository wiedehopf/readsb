// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_ubladerf.c: bladeRF 2.0 Micro support
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

#include "readsb.h"
#include "sdr_ubladerf.h"

#include <libbladeRF.h>
#include <inttypes.h>

static struct {
    const char *device_str;
    const char *fpga_path;
    unsigned decimation;
    bladerf_lpf_mode lpf_mode;
    unsigned lpf_bandwidth;
    unsigned block_size;
    struct bladerf *device;
    iq_convert_fn converter;
    struct converter_state *converter_state;
} uBladeRF;

void ubladeRFInitConfig() {
    uBladeRF.device_str = NULL;
    uBladeRF.fpga_path = NULL;
    uBladeRF.decimation = 1;
    uBladeRF.lpf_mode = BLADERF_LPF_NORMAL;
    uBladeRF.lpf_bandwidth = 1750000;
    uBladeRF.device = NULL;
}

bool ubladeRFHandleOption(int key, char *arg) {
    switch (key) {
        case OptBladeFpgaDir:
            uBladeRF.fpga_path = strdup(arg);
            break;
        case OptBladeDecim:
            uBladeRF.decimation = atoi(arg);
            break;
        case OptBladeBw:
            if (!strcasecmp(arg, "bypass")) {
                uBladeRF.lpf_mode = BLADERF_LPF_BYPASSED;
            } else {
                uBladeRF.lpf_mode = BLADERF_LPF_NORMAL;
                uBladeRF.lpf_bandwidth = atoi(arg);
            }
            break;
        default:
            return false;
    }
    return true;
}

static void show_config() {
    int status;

#if defined(LIBBLADERF_API_VERSION) && (LIBBLADERF_API_VERSION >= 0x02020000)
    bladerf_sample_rate rate;
    bladerf_frequency freq;
    bladerf_gain gain;
    bladerf_bandwidth bw;
#else
    unsigned rate;
    unsigned freq;
    unsigned bw;
    int gain;
#endif
    bladerf_lpf_mode lpf_mode;
    int16_t lms_dc_i, lms_dc_q;
    int16_t fpga_phase, fpga_gain;
    struct bladerf_lms_dc_cals dc_cals;
    bool biastee;

    if ((status = bladerf_get_sample_rate(uBladeRF.device, BLADERF_MODULE_RX, &rate)) < 0) {
       fprintf(stderr, "bladeRF: couldn't read back device sample rate: %s\n", bladerf_strerror(status));
       return;
    }

    if ((status = bladerf_get_frequency(uBladeRF.device, BLADERF_MODULE_RX, &freq)) < 0) {
       fprintf(stderr, "bladeRF: couldn't read back device frequency: %s\n", bladerf_strerror(status));
       return;
    }

    if ((status = bladerf_get_bandwidth(uBladeRF.device, BLADERF_MODULE_RX, &bw)) < 0) {
      fprintf(stderr, "bladeRF: couldn't read back device bandwidth: %s\n", bladerf_strerror(status));
       return;
    }

    if ((status = bladerf_get_gain(uBladeRF.device, BLADERF_MODULE_RX, &gain)) < 0) {
        fprintf(stderr, "bladeRF: couldn't read back device gain: %s\n", bladerf_strerror(status));
    }

    if ((status = bladerf_get_correction(uBladeRF.device, BLADERF_MODULE_RX, BLADERF_CORR_LMS_DCOFF_I, &lms_dc_i)) < 0 ||
            (status = bladerf_get_correction(uBladeRF.device, BLADERF_MODULE_RX, BLADERF_CORR_LMS_DCOFF_Q, &lms_dc_q)) < 0 ||
            (status = bladerf_get_correction(uBladeRF.device, BLADERF_MODULE_RX, BLADERF_CORR_FPGA_PHASE, &fpga_phase)) < 0 ||
            (status = bladerf_get_correction(uBladeRF.device, BLADERF_MODULE_RX, BLADERF_CORR_FPGA_GAIN, &fpga_gain)) < 0
            ) {
        fprintf(stderr, "bladeRF: couldn't read back device configuration (correction values)\n");
        //return;
    }

    if (!strcmp("bladerf1", bladerf_get_board_name(uBladeRF.device))) {
        if ((status = bladerf_get_lpf_mode(uBladeRF.device, BLADERF_MODULE_RX, &lpf_mode)) < 0 ||
                (status = bladerf_lms_get_dc_cals(uBladeRF.device, &dc_cals)) < 0) {
            fprintf(stderr, "bladeRF: couldn't read back device configuration (BladeRF 1 values)\n");
            return;
        }
    }

    if (!strcmp("bladerf2", bladerf_get_board_name(uBladeRF.device))) {
        if ((status = bladerf_get_bias_tee(uBladeRF.device, BLADERF_CHANNEL_RX(0), &biastee)) < 0) {
            fprintf(stderr, "bladeRF: couldn't read back BladeRF Micro bias tee configuration\n");
        }
    }

    fprintf(stderr, "bladeRF: sampling rate: %.1f MHz\n", rate / 1e6);
    fprintf(stderr, "bladeRF: frequency:     %.1f MHz\n", freq / 1e6);
    fprintf(stderr, "bladeRF: gain:          %ddB\n", gain);
    fprintf(stderr, "bladeRF: biastee:       %d\n", (int)biastee);

    switch (lpf_mode) {
        case BLADERF_LPF_NORMAL:
            fprintf(stderr, "bladeRF: LPF bandwidth: %.2f MHz\n", bw/1e6);
            break;
        case BLADERF_LPF_BYPASSED:
            fprintf(stderr, "bladeRF: LPF bypassed\n");
            break;
        case BLADERF_LPF_DISABLED:
            fprintf(stderr, "bladeRF: LPF disabled\n");
            break;
        default:
            fprintf(stderr, "bladeRF: LPF in unknown state\n");
            break;
    }

    fprintf(stderr, "bladeRF: calibration settings:\n");
    fprintf(stderr, "  LMS DC adjust:     I=%d Q=%d\n", lms_dc_i, lms_dc_q);
    fprintf(stderr, "  FPGA phase adjust: %+.3f degrees\n", fpga_phase * 10.0 / 4096);
    fprintf(stderr, "  FPGA gain adjust:  %+.3f\n", fpga_gain * 1.0 / 4096);
    fprintf(stderr, "  LMS LPF tuning:    %d\n", dc_cals.lpf_tuning);
    fprintf(stderr, "  LMS RX LPF filter: I=%d Q=%d\n", dc_cals.rx_lpf_i, dc_cals.rx_lpf_q);
    fprintf(stderr, "  LMS RXVGA2 DC ref: %d\n", dc_cals.dc_ref);
    fprintf(stderr, "  LMS RXVGA2A:       I=%d Q=%d\n", dc_cals.rxvga2a_i, dc_cals.rxvga2a_q);
    fprintf(stderr, "  LMS RXVGA2B:       I=%d Q=%d\n", dc_cals.rxvga2b_i, dc_cals.rxvga2b_q);

}

bool ubladeRFOpen() {
    if (uBladeRF.device) {
        return true;
    }

    int status;

    bladerf_set_usb_reset_on_open(true);
    fprintf(stderr, "Opening BladeRF: %s\n", Modes.dev_name);
    if ((status = bladerf_open(&uBladeRF.device, Modes.dev_name)) < 0) {
        fprintf(stderr, "Failed to open bladeRF: %s\n", bladerf_strerror(status));
        goto error;
    }

    const char *fpga_path;
    if (uBladeRF.fpga_path) {
        fpga_path = uBladeRF.fpga_path;
    } else {
        bladerf_fpga_size size;
        if ((status = bladerf_get_fpga_size(uBladeRF.device, &size)) < 0) {
            fprintf(stderr, "bladerf_get_fpga_size failed: %s\n", bladerf_strerror(status));
            goto error;
        }

        switch (size) {
            case BLADERF_FPGA_40KLE:
                fpga_path = "/usr/share/Nuand/bladeRF/hostedx40.rbf";
                break;
            case BLADERF_FPGA_115KLE:
                fpga_path = "/usr/share/Nuand/bladeRF/hostedx115.rbf";
                break;
            case BLADERF_FPGA_A4:
                fpga_path = "/usr/share/Nuand/bladeRF/hostedxA4.rbf";
                break;
            default:
                fprintf(stderr, "bladeRF: unknown FPGA size, skipping FPGA load");
                fpga_path = NULL;
                break;
        }
    }

    if (fpga_path && fpga_path[0]) {
        fprintf(stderr, "bladeRF: loading FPGA bitstream from %s\n", fpga_path);
        if ((status = bladerf_load_fpga(uBladeRF.device, fpga_path)) < 0) {
            fprintf(stderr, "bladerf_load_fpga() failed: %s\n", bladerf_strerror(status));
            goto error;
        }
    }

    switch (bladerf_device_speed(uBladeRF.device)) {
        case BLADERF_DEVICE_SPEED_HIGH:
            uBladeRF.block_size = 1024;
            break;
        case BLADERF_DEVICE_SPEED_SUPER:
            uBladeRF.block_size = 2048;
            break;
        default:
            fprintf(stderr, "couldn't determine bladerf device speed\n");
            goto error;
    }

    // Close and re-open the bladeRF, otherwise we get "An unexpected error occurred" in later calls.
    bladerf_close(uBladeRF.device);
    if ((status = bladerf_open(&uBladeRF.device, Modes.dev_name)) < 0) {
        fprintf(stderr, "Failed to open bladeRF: %s\n", bladerf_strerror(status));
        goto error;
    }

    if ((status = bladerf_set_sample_rate(uBladeRF.device, BLADERF_MODULE_RX, Modes.sample_rate * uBladeRF.decimation, NULL)) < 0) {
        fprintf(stderr, "bladerf_set_sample_rate failed: %s\n", bladerf_strerror(status));
        goto error;
    }

    if ((status = bladerf_set_frequency(uBladeRF.device, BLADERF_MODULE_RX, Modes.freq)) < 0) {
        fprintf(stderr, "bladerf_set_frequency failed: %s\n", bladerf_strerror(status));
        goto error;
    }

    if (!strcmp("bladerf1", bladerf_get_board_name(uBladeRF.device))) {
        if ((status = bladerf_set_lpf_mode(uBladeRF.device, BLADERF_MODULE_RX, uBladeRF.lpf_mode)) < 0) {
            fprintf(stderr, "bladerf_set_lpf_mode failed: %s\n", bladerf_strerror(status));
            goto error;
        }
    }

    if ((status = bladerf_set_bandwidth(uBladeRF.device, BLADERF_MODULE_RX, uBladeRF.lpf_bandwidth, NULL)) < 0) {
        fprintf(stderr, "bladerf_set_bandwidth failed: %s\n", bladerf_strerror(status));
        goto error;
    }

    /* turn the tx gain right off, just in case */
    if ((status = bladerf_set_gain(uBladeRF.device, BLADERF_MODULE_TX, -100)) < 0) {
        fprintf(stderr, "bladerf_set_gain(TX) failed: %s\n", bladerf_strerror(status));
        goto error;
    }

    /* Gain = -100 is AGC */
    if (Modes.gain == -100) {
        fprintf(stderr, "BladeRF: using AGC\n");
        /* Note: we should really query the BladeRF library to find out what modes we are allowed to use */
        if ((status = bladerf_set_gain_mode(uBladeRF.device, BLADERF_MODULE_RX, BLADERF_GAIN_DEFAULT)) < 0) {
            fprintf(stderr, "bladerf_set_gain_mode to default/AGC failed: %s\n", bladerf_strerror(status));
        }
    } else {
        if ((status = bladerf_set_gain_mode(uBladeRF.device, BLADERF_MODULE_RX, BLADERF_GAIN_MGC)) < 0) {
            fprintf(stderr, "bladerf_set_gain_mode to manual failed: %s\n", bladerf_strerror(status));
        }
        fprintf(stderr, "BladeRF: setting manual gain to %d\n", Modes.gain / 10);
        if ((status = bladerf_set_gain(uBladeRF.device, BLADERF_MODULE_RX, Modes.gain / 10)) < 0) {
            fprintf(stderr, "bladerf_set_gain(RX) failed: %s\n", bladerf_strerror(status));
            goto error;
        }
    }

    if (!strcmp("bladerf2", bladerf_get_board_name(uBladeRF.device))) {
        if (Modes.biastee) {
            // Note: the BladeRF micro enables/disables on both RX channels at the same time
            fprintf(stderr, "Enabling Bias on RX channels\n");
            if ((status = bladerf_set_bias_tee(uBladeRF.device, BLADERF_CHANNEL_RX(0), true)) < 0) {
                fprintf(stderr, "bladerf_set_bias_tee failed for channel 0: %s\n", bladerf_strerror(status));
            }
        }
    }

    if (!strcmp("bladerf1", bladerf_get_board_name(uBladeRF.device))) {
        if ((status = bladerf_set_loopback(uBladeRF.device, BLADERF_LB_NONE)) < 0) {
            fprintf(stderr, "bladerf_set_loopback() failed: %s\n", bladerf_strerror(status));
            goto error;
        }

        if ((status = bladerf_calibrate_dc(uBladeRF.device, BLADERF_DC_CAL_LPF_TUNING)) < 0) {
            fprintf(stderr, "bladerf_calibrate_dc(LPF_TUNING) failed: %s\n", bladerf_strerror(status));
            goto error;
        }

        if ((status = bladerf_calibrate_dc(uBladeRF.device, BLADERF_DC_CAL_RX_LPF)) < 0) {
            fprintf(stderr, "bladerf_calibrate_dc(RX_LPF) failed: %s\n", bladerf_strerror(status));
            goto error;
        }

        if ((status = bladerf_calibrate_dc(uBladeRF.device, BLADERF_DC_CAL_RXVGA2)) < 0) {
            fprintf(stderr, "bladerf_calibrate_dc(RXVGA2) failed: %s\n", bladerf_strerror(status));
            goto error;
        }
    }

    show_config();

    uBladeRF.converter = init_converter(INPUT_SC16Q11,
            Modes.sample_rate,
            Modes.dc_filter,
            &uBladeRF.converter_state);
    if (!uBladeRF.converter) {
        fprintf(stderr, "can't initialize sample converter\n");
        goto error;
    }

    return true;

error:
    if (uBladeRF.device) {
        bladerf_close(uBladeRF.device);
        uBladeRF.device = NULL;
    }
    return false;
}

static struct timespec thread_cpu;
static unsigned timeouts = 0;

static void *handle_bladerf_samples(struct bladerf *dev,
        struct bladerf_stream *stream,
        struct bladerf_metadata *meta,
        void *samples,
        size_t num_samples,
        void *user_data) {
    static uint64_t nextTimestamp = 0;
    static bool dropping = false;

    MODES_NOTUSED(dev);
    MODES_NOTUSED(stream);
    MODES_NOTUSED(meta);
    MODES_NOTUSED(user_data);
    MODES_NOTUSED(num_samples);

    lockReader();
    if (Modes.exit) {
        unlockReader();
        return BLADERF_STREAM_SHUTDOWN;
    }

    unsigned next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    struct mag_buf *outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    struct mag_buf *lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    unsigned free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;

    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS / 2)) {
        // FIFO is full. Drop this block.
        dropping = true;
        unlockReader();
        return samples;
    }

    dropping = false;
    unlockReader();

    outbuf->sysTimestamp = mstime();
    outbuf->sysMicroseconds = mono_micro_seconds();

    // Copy trailing data from last block (or reset if not valid)
    if (outbuf->dropped == 0) {
        memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof (uint16_t));
    } else {
        memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
    }

    // start handling metadata blocks
    outbuf->dropped = 0;
    outbuf->length = 0;
    outbuf->mean_level = outbuf->mean_power = 0;

    unsigned blocks_processed = 0;
    unsigned samples_per_block = (uBladeRF.block_size - 16) / 4;

    static bool overrun = true; // ignore initial overruns as we get up to speed
    static bool first_buffer = true;
    for (unsigned offset = 0; offset < Modes.sdr_buf_samples * 4; offset += uBladeRF.block_size) {
        // read the next metadata header
        uint8_t *header = ((uint8_t*) samples) + offset;
        uint64_t metadata_magic = le32toh(*(uint32_t*) (header));
        uint64_t metadata_timestamp = le64toh(*(uint64_t*) (header + 4));
        uint32_t metadata_flags = le32toh(*(uint32_t*) (header + 12));
        void *sample_data = header + 16;

        if (metadata_magic != 0x12344321) {
            // first buffer is often in the wrong mode
            if (!first_buffer) {
                fprintf(stderr, "bladeRF: wrong metadata header magic value, skipping rest of buffer\n");
            }
            break;
        }

        if (metadata_flags & BLADERF_META_STATUS_OVERRUN) {
            if (!overrun) {
                fprintf(stderr, "bladeRF: receive overrun\n");
            }
            overrun = true;
        } else {
            overrun = false;
        }

#ifndef BROKEN_FPGA_METADATA
        // this needs a fixed decimating FPGA image that handles the timestamp correctly
        if (nextTimestamp && nextTimestamp != metadata_timestamp) {
            // dropped data or lost sync. start again.
            if (metadata_timestamp > nextTimestamp)
                outbuf->dropped += (metadata_timestamp - nextTimestamp);
            outbuf->dropped += outbuf->length;
            outbuf->length = 0;
            blocks_processed = 0;
            outbuf->mean_level = outbuf->mean_power = 0;
            nextTimestamp = metadata_timestamp;
        }
#else
        MODES_NOTUSED(metadata_timestamp);
#endif

        if (!blocks_processed) {
            // Compute the sample timestamp for the start of the block
            outbuf->sampleTimestamp = nextTimestamp * 12e6 / Modes.sample_rate / uBladeRF.decimation;
        }

        // Convert a block of data
        double mean_level, mean_power;
        uBladeRF.converter(sample_data, &outbuf->data[Modes.trailing_samples + outbuf->length], samples_per_block, uBladeRF.converter_state, &mean_level, &mean_power);
        outbuf->length += samples_per_block;
        outbuf->mean_level += mean_level;
        outbuf->mean_power += mean_power;
        nextTimestamp += samples_per_block * uBladeRF.decimation;
        ++blocks_processed;
        timeouts = 0;
    }

    first_buffer = false;

    if (blocks_processed) {
        // Get the approx system time for the start of this block
        int64_t block_duration = 1e3 * outbuf->length / Modes.sample_rate;
        outbuf->sysTimestamp -= block_duration;
        outbuf->sysMicroseconds -= 1000 * block_duration;

        outbuf->mean_level /= blocks_processed;
        outbuf->mean_power /= blocks_processed;

        // Push the new data to the demodulation thread
        lockReader();

        // accumulate CPU while holding the mutex, and restart measurement
        end_cpu_timing(&thread_cpu, &Modes.reader_cpu_accumulator);
        start_cpu_timing(&thread_cpu);

        Modes.mag_buffers[next_free_buffer].dropped = 0;
        Modes.mag_buffers[next_free_buffer].length = 0; // just in case
        Modes.first_free_buffer = next_free_buffer;

        wakeDecode();
        unlockReader();
    }

    return samples;
}

void ubladeRFRun() {
    if (!uBladeRF.device) {
        return;
    }

    unsigned transfers = 7;

    int status;
    struct bladerf_stream *stream = NULL;
    void **buffers = NULL;

    if ((status = bladerf_init_stream(&stream,
            uBladeRF.device,
            handle_bladerf_samples,
            &buffers,
            /* num_buffers */ transfers,
            BLADERF_FORMAT_SC16_Q11_META,
            /* samples_per_buffer */ Modes.sdr_buf_samples,
            /* num_transfers */ transfers,
            /* user_data */ NULL)) < 0) {
        fprintf(stderr, "bladerf_init_stream() failed: %s\n", bladerf_strerror(status));
        goto out;
    }

    unsigned ms_per_transfer = 1000 * Modes.sdr_buf_samples / Modes.sample_rate;
    if ((status = bladerf_set_stream_timeout(uBladeRF.device, BLADERF_MODULE_RX, ms_per_transfer * (transfers + 2))) < 0) {
        fprintf(stderr, "bladerf_set_stream_timeout() failed: %s\n", bladerf_strerror(status));
        goto out;
    }

    if ((status = bladerf_enable_module(uBladeRF.device, BLADERF_MODULE_RX, true) < 0)) {
        fprintf(stderr, "bladerf_enable_module(RX, true) failed: %s\n", bladerf_strerror(status));
        goto out;
    }

    start_cpu_timing(&thread_cpu);

    timeouts = 0; // reset to zero when we get a callback with some data
retry:
    if ((status = bladerf_stream(stream, BLADERF_MODULE_RX)) < 0) {
        fprintf(stderr, "bladerf_stream() failed: %s\n", bladerf_strerror(status));
        if (status == BLADERF_ERR_TIMEOUT) {
            if (++timeouts < 5)
                goto retry;
            fprintf(stderr, "bladerf is wedged, giving up.\n");
        }
        goto out;
    }

out:
    if ((status = bladerf_enable_module(uBladeRF.device, BLADERF_MODULE_RX, false) < 0)) {
        fprintf(stderr, "bladerf_enable_module(RX, false) failed: %s\n", bladerf_strerror(status));
    }

    if (stream) {
        bladerf_deinit_stream(stream);
    }
}

void ubladeRFClose() {
    if (uBladeRF.converter) {
        cleanup_converter(&uBladeRF.converter_state);
        uBladeRF.converter = NULL;
    }

    if (uBladeRF.device) {
        bladerf_close(uBladeRF.device);
        uBladeRF.device = NULL;
    }
}

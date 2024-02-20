// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// stats.c: statistics helpers.
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2015 Oliver Jowett <oliver@mutability.co.uk>
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

void add_timespecs(const struct timespec *x, const struct timespec *y, struct timespec *z) {
    z->tv_sec = x->tv_sec + y->tv_sec;
    z->tv_nsec = x->tv_nsec + y->tv_nsec;
    z->tv_sec += z->tv_nsec / 1000000000L;
    z->tv_nsec = z->tv_nsec % 1000000000L;
}

static void display_range_histogram(struct stats *st);

void display_stats(struct stats *st) {
    int j;
    time_t tt_start, tt_end;
    struct tm tm_start, tm_end;
    char tb_start[30], tb_end[30];

    printf("\n\n");

    tt_start = st->start / 1000;
    localtime_r(&tt_start, &tm_start);
    strftime(tb_start, sizeof (tb_start), "%c %Z", &tm_start);
    tt_end = st->end / 1000;
    localtime_r(&tt_end, &tm_end);
    strftime(tb_end, sizeof (tb_end), "%c %Z", &tm_end);

    printf("Statistics: %s - %s\n", tb_start, tb_end);

    if (Modes.sdr_type != SDR_NONE) {
        printf("Local receiver:\n");
        printf("  %llu samples processed\n", (unsigned long long) st->samples_processed);
        printf("  %llu samples dropped\n", (unsigned long long) st->samples_dropped);
        printf("  %llu samples lost\n", (unsigned long long) st->samples_lost);

        printf("  %u Mode A/C messages received\n", st->demod_modeac);
        printf("  %u Mode-S message preambles received\n", st->demod_preambles);
        printf("    %u with bad message format or invalid CRC\n", st->demod_rejected_bad);
        printf("    %u with unrecognized ICAO address\n", st->demod_rejected_unknown_icao);
        printf("    %u accepted with correct CRC\n", st->demod_accepted[0]);
        for (j = 1; j <= Modes.nfix_crc; ++j)
            printf("    %u accepted with %d-bit error repaired\n", st->demod_accepted[j], j);

        if (st->noise_power_sum > 0 && st->noise_power_count > 0) {
            printf("  %.1f dBFS noise power\n",
                    10 * log10(st->noise_power_sum / st->noise_power_count));
        }

        if (st->signal_power_sum > 0 && st->signal_power_count > 0) {
            printf("  %.1f dBFS mean signal power\n",
                    10 * log10(st->signal_power_sum / st->signal_power_count));
        }

        if (st->peak_signal_power > 0) {
            printf("  %.1f dBFS peak signal power\n",
                    10 * log10(st->peak_signal_power));
        }

        printf("  %u messages with signal power above -3dBFS\n",
                st->strong_signal_count);

        printf("\n Phase stats");
        printf("\n ");
        for (int i = 0; i < 5; i++) printf(" %8u", i + 3);
        printf("\n ");
        for (int i = 0; i < 5; i++) printf(" %8u", st->demod_preamblePhase[i]);
        printf("\n ");
        for (int i = 0; i < 5; i++) printf(" %8u", i + 4);
        printf("\n ");
        for (int i = 0; i < 5; i++) printf(" %8u", st->demod_bestPhase[i]);
        printf("\n\n");

    }

    if (Modes.net) {
        printf("Network:\n");
        printf("  %.6f MBytes received\n", st->network_bytes_in / 1e6);
        printf("  %.6f MBytes sent\n", st->network_bytes_out / 1e6);
        printf("Messages from network clients:\n");
        printf("  %u Mode A/C messages received\n", st->remote_received_modeac);
        printf("  %u Mode A/C messages received\n", st->remote_received_modeac);
        printf("  %u Mode S messages received\n", st->remote_received_modes);
        printf("    %u with bad message format or invalid CRC\n", st->remote_rejected_bad);
        printf("    %u with unrecognized ICAO address\n", st->remote_rejected_unknown_icao);
        printf("    %u accepted with correct CRC\n", st->remote_accepted[0]);
        for (j = 1; j <= Modes.nfix_crc; ++j)
            printf("    %u accepted with %d-bit error repaired\n", st->remote_accepted[j], j);
    }

    printf("%u total usable messages\n",
            st->messages_total);

    printf("%u surface position messages received\n"
            "%u airborne position messages received\n"
            "%u global CPR attempts with valid positions\n"
            "%u global CPR attempts with bad data\n"
            "  %u global CPR attempts that failed the range check\n"
            "  %u global CPR attempts that failed the speed check\n"
            "%u global CPR attempts with insufficient data\n"
            "%u local CPR attempts with valid positions\n"
            "  %u aircraft-relative positions\n"
            "  %u receiver-relative positions\n"
            "%u local CPR attempts that did not produce useful positions\n"
            "  %u local CPR attempts that failed the range check\n"
            "  %u local CPR attempts that failed the speed check\n"
            "%u CPR messages that look like transponder failures filtered\n",
            st->cpr_surface,
            st->cpr_airborne,
            st->cpr_global_ok,
            st->cpr_global_bad,
            st->cpr_global_range_checks,
            st->cpr_global_speed_checks,
            st->cpr_global_skipped,
            st->cpr_local_ok,
            st->cpr_local_aircraft_relative,
            st->cpr_local_receiver_relative,
            st->cpr_local_skipped,
            st->cpr_local_range_checks,
            st->cpr_local_speed_checks,
            st->cpr_filtered);

    printf("%u non-ES altitude messages from ES-equipped aircraft ignored\n", st->suppressed_altitude_messages);
    printf("%u unique aircraft tracks\n", st->unique_aircraft);
    printf("%u aircraft tracks where only one message was seen\n", st->single_message_aircraft);

    {
        int64_t demod_cpu_millis = (int64_t) st->demod_cpu.tv_sec * 1000LL + st->demod_cpu.tv_nsec / 1000000LL;
        int64_t reader_cpu_millis = (int64_t) st->reader_cpu.tv_sec * 1000LL + st->reader_cpu.tv_nsec / 1000000LL;
        int64_t background_cpu_millis = (int64_t) st->background_cpu.tv_sec * 1000LL + st->background_cpu.tv_nsec / 1000000LL;

        printf("CPU load: %.1f%%\n"
                "  %llu ms for demodulation\n"
                "  %llu ms for reading from USB\n"
                "  %llu ms for network input and background tasks\n",
                100.0 * (demod_cpu_millis + reader_cpu_millis + background_cpu_millis) / (st->end - st->start + 1),
                (unsigned long long) demod_cpu_millis,
                (unsigned long long) reader_cpu_millis,
                (unsigned long long) background_cpu_millis);
    }

    if (Modes.stats_range_histo)
        display_range_histogram(st);

    fflush(stdout);
}

static void display_range_histogram(struct stats *st) {
    uint32_t peak;
    int i, j;
    int heights[RANGE_BUCKET_COUNT];

#if 0
#define NPIXELS 4
    char *pixels[NPIXELS] = {".", "o", "O", "|"};
#else
    // UTF-8 bar symbols
#define NPIXELS 8
    char *pixels[NPIXELS] = {
        "\xE2\x96\x81",
        "\xE2\x96\x82",
        "\xE2\x96\x83",
        "\xE2\x96\x84",
        "\xE2\x96\x85",
        "\xE2\x96\x86",
        "\xE2\x96\x87",
        "\xE2\x96\x88"
    };
#endif

    printf("Range histogram:\n\n");

    for (i = 0, peak = 0; i < RANGE_BUCKET_COUNT; ++i) {
        if (st->range_histogram[i] > peak)
            peak = st->range_histogram[i];
    }

    for (i = 0; i < RANGE_BUCKET_COUNT; ++i) {
        heights[i] = st->range_histogram[i] * 20.0 * NPIXELS / peak;
        if (st->range_histogram[i] > 0 && heights[i] == 0)
            heights[i] = 1;
    }

    for (j = 0; j < 20; ++j) {
        for (i = 0; i < RANGE_BUCKET_COUNT; ++i) {
            int pheight = heights[i] - ((19 - j) * NPIXELS);
            if (pheight <= 0)
                printf(" ");
            else if (pheight >= NPIXELS)
                printf("%s", pixels[NPIXELS - 1]);
            else
                printf("%s", pixels[pheight]);
        }
        printf("\n");
    }

    for (i = 0; i < RANGE_BUCKET_COUNT / 4; ++i) {
        printf("----");
    }
    printf("\n");

    for (i = 0; i < RANGE_BUCKET_COUNT / 4; ++i) {
        printf(" '  ");
    }
    printf("\n");

    for (i = 0; i < RANGE_BUCKET_COUNT / 4; ++i) {
        int midpoint = round((i * 4 + 1.5) * Modes.maxRange / RANGE_BUCKET_COUNT / 1000);
        printf("%03d ", midpoint);
    }
    printf("km\n");
}

void reset_stats(struct stats *st) {
    static struct stats st_zero;
    *st = st_zero;
    st->distance_min = 2E42;
}

void add_stats(const struct stats *st1, const struct stats *st2, struct stats *target) {
    int i;

    if (st1->start == 0)
        target->start = st2->start;
    else if (st2->start == 0)
        target->start = st1->start;
    else if (st1->start < st2->start)
        target->start = st1->start;
    else
        target->start = st2->start;

    target->end = st1->end > st2->end ? st1->end : st2->end;

    target->demod_preambles = st1->demod_preambles + st2->demod_preambles;
    target->demod_rejected_bad = st1->demod_rejected_bad + st2->demod_rejected_bad;
    target->demod_rejected_unknown_icao = st1->demod_rejected_unknown_icao + st2->demod_rejected_unknown_icao;
    for (i = 0; i < MODES_MAX_BITERRORS + 1; ++i)
        target->demod_accepted[i] = st1->demod_accepted[i] + st2->demod_accepted[i];
    target->demod_modeac = st1->demod_modeac + st2->demod_modeac;

    for (int i = 0; i < 5; i++) {
        target->demod_preamblePhase[i] = st1->demod_preamblePhase[i] + st2->demod_preamblePhase[i];
        target->demod_bestPhase[i] = st1->demod_bestPhase[i] + st2->demod_bestPhase[i];
    }

    target->samples_processed = st1->samples_processed + st2->samples_processed;
    target->samples_dropped = st1->samples_dropped + st2->samples_dropped;
    target->samples_lost = st1->samples_lost + st2->samples_lost;

    add_timespecs(&st1->demod_cpu, &st2->demod_cpu, &target->demod_cpu);
    add_timespecs(&st1->reader_cpu, &st2->reader_cpu, &target->reader_cpu);
    add_timespecs(&st1->background_cpu, &st2->background_cpu, &target->background_cpu);
    add_timespecs(&st1->aircraft_json_cpu, &st2->aircraft_json_cpu, &target->aircraft_json_cpu);
    add_timespecs(&st1->globe_json_cpu, &st2->globe_json_cpu, &target->globe_json_cpu);
    add_timespecs(&st1->bin_cpu, &st2->bin_cpu, &target->bin_cpu);
    add_timespecs(&st1->heatmap_and_state_cpu, &st2->heatmap_and_state_cpu, &target->heatmap_and_state_cpu);
    add_timespecs(&st1->remove_stale_cpu, &st2->remove_stale_cpu, &target->remove_stale_cpu);
    add_timespecs(&st1->api_update_cpu, &st2->api_update_cpu, &target->api_update_cpu);
    add_timespecs(&st1->api_worker_cpu, &st2->api_worker_cpu, &target->api_worker_cpu);
    add_timespecs(&st1->trace_json_cpu, &st2->trace_json_cpu, &target->trace_json_cpu);

    target->pos_all = st1->pos_all + st2->pos_all;
    target->pos_duplicate = st1->pos_duplicate + st2->pos_duplicate;
    target->pos_garbage = st1->pos_garbage + st2->pos_garbage;
    for (i = 0; i < NUM_TYPES; i ++) {
        target->pos_by_type[i] = st1->pos_by_type[i] + st2->pos_by_type[i];
    }

    target->api_request_count = st1->api_request_count + st2->api_request_count;

    target->recentTraceWrites = st1->recentTraceWrites + st2->recentTraceWrites;
    target->fullTraceWrites = st1->fullTraceWrites + st2->fullTraceWrites;
    target->permTraceWrites = st1->permTraceWrites + st2->permTraceWrites;

    // noise power:
    target->noise_power_sum = st1->noise_power_sum + st2->noise_power_sum;
    target->noise_power_count = st1->noise_power_count + st2->noise_power_count;

    // mean signal power:
    target->signal_power_sum = st1->signal_power_sum + st2->signal_power_sum;
    target->signal_power_count = st1->signal_power_count + st2->signal_power_count;

    // peak signal power seen
    if (st1->peak_signal_power > st2->peak_signal_power)
        target->peak_signal_power = st1->peak_signal_power;
    else
        target->peak_signal_power = st2->peak_signal_power;

    // strong signals
    target->strong_signal_count = st1->strong_signal_count + st2->strong_signal_count;

    // remote messages:
    target->remote_received_modeac = st1->remote_received_modeac + st2->remote_received_modeac;
    target->remote_received_modes = st1->remote_received_modes + st2->remote_received_modes;
    target->remote_received_basestation_valid = st1->remote_received_basestation_valid + st2->remote_received_basestation_valid;
    target->remote_received_basestation_invalid = st1->remote_received_basestation_invalid + st2->remote_received_basestation_invalid;
    target->remote_rejected_bad = st1->remote_rejected_bad + st2->remote_rejected_bad;
    target->remote_rejected_delayed = st1->remote_rejected_delayed + st2->remote_rejected_delayed;
    target->remote_malformed_beast = st1->remote_malformed_beast + st2->remote_malformed_beast;

    if (Modes.ping) {
        for (int i = 0; i < PING_BUCKETS; i++) {
            target->remote_ping_rtt[i] = st1->remote_ping_rtt[i] + st2->remote_ping_rtt[i];
        }
    }

    target->network_bytes_in = st1->network_bytes_in + st2->network_bytes_in;
    target->network_bytes_out = st1->network_bytes_out + st2->network_bytes_out;

    target->remote_rejected_unknown_icao = st1->remote_rejected_unknown_icao + st2->remote_rejected_unknown_icao;
    for (i = 0; i < MODES_MAX_BITERRORS + 1; ++i)
        target->remote_accepted[i] = st1->remote_accepted[i] + st2->remote_accepted[i];

    // total messages:
    target->messages_total = st1->messages_total + st2->messages_total;

    // CPR decoding:
    target->cpr_surface = st1->cpr_surface + st2->cpr_surface;
    target->cpr_airborne = st1->cpr_airborne + st2->cpr_airborne;
    target->cpr_global_ok = st1->cpr_global_ok + st2->cpr_global_ok;
    target->cpr_global_bad = st1->cpr_global_bad + st2->cpr_global_bad;
    target->cpr_global_skipped = st1->cpr_global_skipped + st2->cpr_global_skipped;
    target->cpr_global_range_checks = st1->cpr_global_range_checks + st2->cpr_global_range_checks;
    target->cpr_global_speed_checks = st1->cpr_global_speed_checks + st2->cpr_global_speed_checks;
    target->cpr_local_ok = st1->cpr_local_ok + st2->cpr_local_ok;
    target->cpr_local_aircraft_relative = st1->cpr_local_aircraft_relative + st2->cpr_local_aircraft_relative;
    target->cpr_local_receiver_relative = st1->cpr_local_receiver_relative + st2->cpr_local_receiver_relative;
    target->cpr_local_skipped = st1->cpr_local_skipped + st2->cpr_local_skipped;
    target->cpr_local_range_checks = st1->cpr_local_range_checks + st2->cpr_local_range_checks;
    target->cpr_local_speed_checks = st1->cpr_local_speed_checks + st2->cpr_local_speed_checks;
    target->cpr_filtered = st1->cpr_filtered + st2->cpr_filtered;

    target->suppressed_altitude_messages = st1->suppressed_altitude_messages + st2->suppressed_altitude_messages;

    // aircraft
    target->unique_aircraft = st1->unique_aircraft + st2->unique_aircraft;
    target->single_message_aircraft = st1->single_message_aircraft + st2->single_message_aircraft;

    // range histogram
    for (i = 0; i < RANGE_BUCKET_COUNT; ++i)
        target->range_histogram[i] = st1->range_histogram[i] + st2->range_histogram[i];

    // Longest Distance observed
    if (st1->distance_max > st2->distance_max)
        target->distance_max = st1->distance_max;
    else
        target->distance_max = st2->distance_max;

    if (st1->distance_min < st2->distance_min)
        target->distance_min = st1->distance_min;
    else
        target->distance_min = st2->distance_min;
}

static void lockCurrent() {
    int micro = atomic_exchange(&Modes.apiWorkerCpuMicro, 0);
    Modes.stats_current.api_worker_cpu.tv_sec += micro / (1000LL * 1000LL);
    Modes.stats_current.api_worker_cpu.tv_nsec += 1000LL * (micro % (1000LL * 1000LL));
    normalize_timespec(&Modes.stats_current.api_worker_cpu);

    Modes.stats_current.api_request_count += atomic_exchange(&Modes.apiRequestCounter, 0);

    Modes.stats_current.recentTraceWrites += atomic_exchange(&Modes.recentTraceWrites, 0);
    Modes.stats_current.fullTraceWrites += atomic_exchange(&Modes.fullTraceWrites, 0);
    Modes.stats_current.permTraceWrites += atomic_exchange(&Modes.permTraceWrites, 0);
}
static void unlockCurrent() {
}

void display_total_stats(void) {
    struct stats added;
    lockCurrent();
    add_stats(&Modes.stats_alltime, &Modes.stats_current, &added);
    unlockCurrent();
    display_stats(&added);
}


void display_total_short_range_stats() {
    struct stats added;
    lockCurrent();
    add_stats(&Modes.stats_alltime, &Modes.stats_current, &added);
    unlockCurrent();

#define buckets 16
#define stride 50.0e3 // 100 km
    int counts[buckets];
    memset(counts, 0, sizeof(counts));
    int bucket = 0;
    for (int i = 0; i < RANGE_BUCKET_COUNT; i++) {
        double range = ((double) i + 0.5) * Modes.maxRange / (double) RANGE_BUCKET_COUNT;
        if (range > (bucket + 1) * stride && bucket < buckets - 1) {
            bucket++;
        }
        counts[bucket] += added.range_histogram[i];
    }
    fprintf(stderr, "range buckets (%.0f km):", stride / 1000.0);
    for (int i = 0; i < buckets; i++) {
        fprintf(stderr, " %d", counts[i]);
    }
    fprintf(stderr, "\n");
#undef buckets
#undef stride
}


void checkDisplayStats(int64_t now) {
    Modes.stats_current.end = now;

    //fprintf(stderr, "next_stats_display %.1f now %.1f", Modes.next_stats_display / 1000.0, now / 1000.0);
    if (Modes.stats_display_interval && now + 5 * SECONDS >= Modes.next_stats_display) {
        Modes.next_stats_display = now + Modes.stats_display_interval;

        display_stats(&Modes.stats_periodic);
        reset_stats(&Modes.stats_periodic);
    }
}

void statsUpdate(int64_t now) {
    Modes.stats_current.end = now;

    Modes.next_stats_update = roundSeconds(10, 5, now + 10 * SECONDS);

    lockCurrent();
    Modes.stats_bucket = (Modes.stats_bucket + 1) % STAT_BUCKETS;
    Modes.stats_10[Modes.stats_bucket] = Modes.stats_current;
    add_stats(&Modes.stats_current, &Modes.stats_alltime, &Modes.stats_alltime);
    add_stats(&Modes.stats_current, &Modes.stats_periodic, &Modes.stats_periodic);

    reset_stats(&Modes.stats_current);
    Modes.stats_current.start = Modes.stats_current.end = now;
    unlockCurrent();
}

static char * appendTypeCounts(char *p, char *end) {
    struct statsCount *sC = &(Modes.globalStatsCount);
    p = safe_snprintf(p, end, ",\n\"aircraft_with_pos\": %d", sC->readsb_aircraft_with_position);
    p = safe_snprintf(p, end, ", \"aircraft_without_pos\": %d", sC->readsb_aircraft_no_position);
    p = safe_snprintf(p, end, ", \"aircraft_count_by_type\": {");
    for (int i = 0; i < NUM_TYPES; i++) {
        p = safe_snprintf(p, end, " \"%s\": %d,", addrtype_enum_string(i), sC->type_counts[i]);
    }
    p--;
    p = safe_snprintf(p, end, "}");
    return p;
}

static char * appendStatsJson(char *p, char *end, struct stats *st, const char *key) {
    int i;

    p = safe_snprintf(p, end,
            ",\n\"%s\":{\"start\":%.1f,\"end\":%.1f",
            key,
            st->start / 1000.0,
            st->end / 1000.0);

    if (Modes.sdr_type != SDR_NONE) {
        p = safe_snprintf(p, end,
                ",\"local\":{\"samples_processed\":%llu"
                ",\"samples_dropped\":%llu"
                ",\"samples_lost\":%llu"
                ",\"modeac\":%u"
                ",\"modes\":%u"
                ",\"bad\":%u"
                ",\"unknown_icao\":%u",
                (unsigned long long) st->samples_processed,
                (unsigned long long) st->samples_dropped,
                (unsigned long long) st->samples_lost,
                st->demod_modeac,
                st->demod_preambles,
                st->demod_rejected_bad,
                st->demod_rejected_unknown_icao);

        for (i = 0; i <= Modes.nfix_crc; ++i) {
            if (i == 0) p = safe_snprintf(p, end, ",\"accepted\":[%u", st->demod_accepted[i]);
            else p = safe_snprintf(p, end, ",%u", st->demod_accepted[i]);
        }

        p = safe_snprintf(p, end, "]");

        if (st->signal_power_sum > 0 && st->signal_power_count > 0)
            p = safe_snprintf(p, end, ",\"signal\":%.1f", 10 * log10(st->signal_power_sum / st->signal_power_count));
        if (st->noise_power_sum > 0 && st->noise_power_count > 0)
            p = safe_snprintf(p, end, ",\"noise\":%.1f", 10 * log10(st->noise_power_sum / st->noise_power_count));
        if (st->peak_signal_power > 0)
            p = safe_snprintf(p, end, ",\"peak_signal\":%.1f", 10 * log10(st->peak_signal_power));

        p = safe_snprintf(p, end, ",\"strong_signals\":%d", st->strong_signal_count);

        p = safe_snprintf(p, end, ",\n\"pre_phase_1\":[");
        for (int i = 0; i < 5; i++) p = safe_snprintf(p, end, "%9u,", st->demod_preamblePhase[i]);
        p--; p = safe_snprintf(p, end, "],\"best_phase\" :[");
        for (int i = 0; i < 5; i++) p = safe_snprintf(p, end, "%9u,", st->demod_bestPhase[i]);
        p--; p = safe_snprintf(p, end, "]");

        p = safe_snprintf(p, end, "}");
    }

    p = safe_snprintf(p, end, ",\"messages_valid\": %u", st->messages_total);
    p = safe_snprintf(p, end, ",\"position_count_total\": %u", st->pos_all);

    p = safe_snprintf(p, end, ",\"position_count_by_type\": {");
    for (int i = 0; i < NUM_TYPES; i++) {
        p = safe_snprintf(p, end, "\"%s\": %d,", addrtype_enum_string(i), st->pos_by_type[i]);
    }
    p--;
    p = safe_snprintf(p, end, "}");

    if (Modes.net) {
        p = safe_snprintf(p, end,
                ",\"remote\":{\"modeac\":%u"
                ",\"modes\":%u"
                ",\"basestation\": %u"
                ",\"bad\":%u"
                ",\"unknown_icao\":%u",
                st->remote_received_modeac,
                st->remote_received_modes,
                st->remote_received_basestation_valid,
                st->remote_rejected_bad,
                st->remote_rejected_unknown_icao);

        for (i = 0; i <= Modes.nfix_crc; ++i) {
            if (i == 0) p = safe_snprintf(p, end, ",\"accepted\":[%u", st->remote_accepted[i]);
            else p = safe_snprintf(p, end, ",%u", st->remote_accepted[i]);
        }
        p = safe_snprintf(p, end, "]");

        p = safe_snprintf(p, end, ",\"bytes_in\": %lu", (long) st->network_bytes_in);
        p = safe_snprintf(p, end, ",\"bytes_out\": %lu", (long) st->network_bytes_out);

        p = safe_snprintf(p, end, "}");
    }

    {
        long long trace_json_cpu_millis_sum = 0;
        trace_json_cpu_millis_sum += (int64_t) st->trace_json_cpu.tv_sec * 1000UL + st->trace_json_cpu.tv_nsec / 1000000UL;

        p = safe_snprintf(p, end,
                ",\"cpr\":{\"surface\":%u"
                ",\"airborne\":%u"
                ",\"global_ok\":%u"
                ",\"global_bad\":%u"
                ",\"global_range\":%u"
                ",\"global_speed\":%u"
                ",\"global_skipped\":%u"
                ",\"local_ok\":%u"
                ",\"local_aircraft_relative\":%u"
                ",\"local_receiver_relative\":%u"
                ",\"local_skipped\":%u"
                ",\"local_range\":%u"
                ",\"local_speed\":%u"
                ",\"filtered\":%u}"
                ",\"altitude_suppressed\":%u"
                ",\"cpu\":{\"demod\":%lld,\"reader\":%lld,\"background\":%lld"
                ",\"aircraft_json\":%lld"
                ",\"globe_json\":%lld"
                ",\"binCraft\":%lld"
                ",\"trace_json\":%lld"
                ",\"heatmap_and_state\":%lld"
                ",\"api_workers\":%lld"
                ",\"api_update\":%lld"
                ",\"remove_stale\":%lld}"
                ",\"tracks\":{\"all\":%u"
                ",\"single_message\":%u}"
                ",\"messages\":%u"
                ",\"max_distance\":%ld"
                "}",
            st->cpr_surface,
            st->cpr_airborne,
            st->cpr_global_ok,
            st->cpr_global_bad,
            st->cpr_global_range_checks,
            st->cpr_global_speed_checks,
            st->cpr_global_skipped,
            st->cpr_local_ok,
            st->cpr_local_aircraft_relative,
            st->cpr_local_receiver_relative,
            st->cpr_local_skipped,
            st->cpr_local_range_checks,
            st->cpr_local_speed_checks,
            st->cpr_filtered,
            st->suppressed_altitude_messages,
#define CPU_MILLIS(x) ((unsigned long long) st->x##_cpu.tv_sec * 1000UL + st->x##_cpu.tv_nsec / 1000000UL)
            CPU_MILLIS(demod),
            CPU_MILLIS(reader),
            CPU_MILLIS(background),
            CPU_MILLIS(aircraft_json),
            CPU_MILLIS(globe_json),
            CPU_MILLIS(bin),
            trace_json_cpu_millis_sum,
            CPU_MILLIS(heatmap_and_state),
            CPU_MILLIS(api_worker),
            CPU_MILLIS(api_update),
            CPU_MILLIS(remove_stale),
#undef CPU_MILLIS
            st->unique_aircraft,
            st->single_message_aircraft,
            st->messages_total,
            (long) st->distance_max);
    }

    return p;
}

struct char_buffer generateStatusProm(int64_t now) {
    struct char_buffer cb;
    size_t buflen = 8192;
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;

    struct statsCount *sC = &(Modes.globalStatsCount);

    int64_t uptime = now - Modes.startup_time;
    if (now < Modes.startup_time)
        uptime = 0;

    p = safe_snprintf(p, end, "readsb_now %"PRIu64"\n", now);
    p = safe_snprintf(p, end, "readsb_uptime %"PRIu64"\n", uptime);

    p = safe_snprintf(p, end, "readsb_aircraft_with_position %u\n", sC->readsb_aircraft_with_position);
    p = safe_snprintf(p, end, "readsb_aircraft_without_position %u\n", sC->readsb_aircraft_total - sC->readsb_aircraft_with_position);
    for (int i = 0; i < NUM_TYPES; i++) {
        p = safe_snprintf(p, end, "readsb_aircraft_%s %u\n", addrtype_enum_string(i), sC->type_counts[i]);
    }

    if (p >= end)
        fprintf(stderr, "buffer overrun status json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generateStatusJson(int64_t now) {
    struct char_buffer cb;
    size_t buflen = 8192;
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;

    int64_t uptime = now - Modes.startup_time;
    if (now < Modes.startup_time)
        uptime = 0;

    p = safe_snprintf(p, end, "{ \"now\": %.1f, \"uptime\": %.1f", now / 1000.0, uptime / 1000.0);

    p = appendTypeCounts(p, end);

    p = safe_snprintf(p, end, " }\n");

    if (p >= end)
        fprintf(stderr, "buffer overrun status json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}


struct char_buffer generateStatsJson(int64_t now) {
    struct char_buffer cb;
    int bufsize = 64 * 1024;
    char *buf = (char *) cmalloc(bufsize), *p = buf, *end = buf + bufsize;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f",
            now / 1000.0);

    if (!Modes.net_only) {
        p = safe_snprintf(p, end, ", \"gain_db\" : %.1f", Modes.gain / 10.0);
        p = safe_snprintf(p, end, ", \"estimated_ppm\" : %.1f", Modes.estimated_ppm);
    }

    p = appendTypeCounts(p, end);

    p = appendStatsJson(p, end, &Modes.stats_1min, "last1min");

    p = appendStatsJson(p, end, &Modes.stats_5min, "last5min");

    p = appendStatsJson(p, end, &Modes.stats_15min, "last15min");

    p = appendStatsJson(p, end, &Modes.stats_alltime, "total");
    p = safe_snprintf(p, end, "\n}\n");

    if (p >= end)
        fprintf(stderr, "buffer overrun stats json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generatePromFile(int64_t now) {
    struct char_buffer cb;
    int bufsize = 64 * 1024;
    char *buf = (char *) cmalloc(bufsize), *p = buf, *end = buf + bufsize;

    struct stats *st = &Modes.stats_1min;

    unsigned long long trace_json_cpu_millis_sum = 0;
    trace_json_cpu_millis_sum += (int64_t) st->trace_json_cpu.tv_sec * 1000UL + st->trace_json_cpu.tv_nsec / 1000000UL;

    struct statsCount *sC = &(Modes.globalStatsCount);

    p = safe_snprintf(p, end, "readsb_aircraft_adsb_version_zero %u\n", sC->readsb_aircraft_adsb_version_0);
    p = safe_snprintf(p, end, "readsb_aircraft_adsb_version_one %u\n", sC->readsb_aircraft_adsb_version_1);
    p = safe_snprintf(p, end, "readsb_aircraft_adsb_version_two %u\n", sC->readsb_aircraft_adsb_version_2);
    p = safe_snprintf(p, end, "readsb_aircraft_emergency %u\n", sC->readsb_aircraft_emergency);
    p = safe_snprintf(p, end, "readsb_aircraft_rssi_average %.1f\n", sC->readsb_aircraft_rssi_average);
    p = safe_snprintf(p, end, "readsb_aircraft_rssi_min %.1f\n", sC->readsb_aircraft_rssi_min);
    p = safe_snprintf(p, end, "readsb_aircraft_rssi_quart1 %.1f\n", sC->readsb_aircraft_rssi_quart1);
    p = safe_snprintf(p, end, "readsb_aircraft_rssi_median %.1f\n", sC->readsb_aircraft_rssi_median);
    p = safe_snprintf(p, end, "readsb_aircraft_rssi_quart3 %.1f\n", sC->readsb_aircraft_rssi_quart3);
    p = safe_snprintf(p, end, "readsb_aircraft_rssi_max %.1f\n", sC->readsb_aircraft_rssi_max);

    p = safe_snprintf(p, end, "readsb_aircraft_total %u\n", sC->readsb_aircraft_total);
    p = safe_snprintf(p, end, "readsb_aircraft_with_flight_number %u\n", sC->readsb_aircraft_with_flight_number);
    p = safe_snprintf(p, end, "readsb_aircraft_without_flight_number %u\n", sC->readsb_aircraft_without_flight_number);
    p = safe_snprintf(p, end, "readsb_aircraft_with_position %u\n", sC->readsb_aircraft_with_position);
    p = safe_snprintf(p, end, "readsb_aircraft_without_position %u\n", sC->readsb_aircraft_total - sC->readsb_aircraft_with_position);

    for (int i = 0; i < NUM_TYPES; i++) {
        p = safe_snprintf(p, end, "readsb_aircraft_%s %u\n", addrtype_enum_string(i), sC->type_counts[i]);
    }

    p = safe_snprintf(p, end, "readsb_cpr_airborne %u\n", st->cpr_airborne);
    p = safe_snprintf(p, end, "readsb_cpr_surface %u\n", st->cpr_surface);

    p = safe_snprintf(p, end, "readsb_cpr_global_ok %u\n", st->cpr_global_ok);
    p = safe_snprintf(p, end, "readsb_cpr_global_bad %u\n", st->cpr_global_bad);
    p = safe_snprintf(p, end, "readsb_cpr_global_bad_range %u\n", st->cpr_global_range_checks);
    p = safe_snprintf(p, end, "readsb_cpr_global_bad_speed %u\n", st->cpr_global_speed_checks);
    p = safe_snprintf(p, end, "readsb_cpr_global_skipped %u\n", st->cpr_global_skipped);

    p = safe_snprintf(p, end, "readsb_cpr_local_ok %u\n", st->cpr_local_ok);
    p = safe_snprintf(p, end, "readsb_cpr_local_aircraft_relative %u\n", st->cpr_local_aircraft_relative);
    p = safe_snprintf(p, end, "readsb_cpr_local_receiver_relative %u\n", st->cpr_local_receiver_relative);
    p = safe_snprintf(p, end, "readsb_cpr_local_bad_range %u\n", st->cpr_local_range_checks);
    p = safe_snprintf(p, end, "readsb_cpr_local_bad_speed %u\n", st->cpr_local_speed_checks);
    p = safe_snprintf(p, end, "readsb_cpr_local_skipped %u\n", st->cpr_local_skipped);

    p = safe_snprintf(p, end, "readsb_cpr_filtered %u\n", st->cpr_filtered);

#define CPU_MILLIS(x) ((unsigned long long) st->x##_cpu.tv_sec * 1000UL + st->x##_cpu.tv_nsec / 1000000UL)
    p = safe_snprintf(p, end, "readsb_cpu_background %llu\n", CPU_MILLIS(background));
    p = safe_snprintf(p, end, "readsb_cpu_demod %llu\n", CPU_MILLIS(demod));
    p = safe_snprintf(p, end, "readsb_cpu_reader %llu\n", CPU_MILLIS(reader));
    p = safe_snprintf(p, end, "readsb_cpu_aircraft_json %llu\n", CPU_MILLIS(aircraft_json));
    p = safe_snprintf(p, end, "readsb_cpu_globe_json %llu\n", CPU_MILLIS(globe_json));
    p = safe_snprintf(p, end, "readsb_cpu_binCraft %llu\n", CPU_MILLIS(bin));
    p = safe_snprintf(p, end, "readsb_cpu_heatmap_and_state %llu\n", CPU_MILLIS(heatmap_and_state));
    p = safe_snprintf(p, end, "readsb_cpu_remove_stale %llu\n", CPU_MILLIS(remove_stale));
    p = safe_snprintf(p, end, "readsb_cpu_trace_json %llu\n", trace_json_cpu_millis_sum);
    p = safe_snprintf(p, end, "readsb_cpu_api_update %llu\n", CPU_MILLIS(api_update));
    p = safe_snprintf(p, end, "readsb_cpu_api_workers %llu\n", CPU_MILLIS(api_worker));
#undef CPU_MILLIS

    p = safe_snprintf(p, end, "readsb_api_request_count %llu\n", (unsigned long long) st->api_request_count);
    p = safe_snprintf(p, end, "readsb_tracewrites_recent %u\n", st->recentTraceWrites);
    p = safe_snprintf(p, end, "readsb_tracewrites_full %u\n", st->fullTraceWrites);
    p = safe_snprintf(p, end, "readsb_tracewrites_perm %u\n", st->permTraceWrites);
    p = safe_snprintf(p, end, "readsb_tracewrites_cycle_duration %lld\n", (long long) Modes.writeTracesActualDuration);


    p = safe_snprintf(p, end, "readsb_distance_max %u\n", (uint32_t) st->distance_max);
    if (st->distance_min < 1E42)
        p = safe_snprintf(p, end, "readsb_distance_min %u\n", (uint32_t) st->distance_min);
    else
        p = safe_snprintf(p, end, "readsb_distance_min 0\n");

    p = safe_snprintf(p, end, "readsb_messages_valid %u\n", st->messages_total);
    p = safe_snprintf(p, end, "readsb_messages_invalid %u\n",
            st->remote_received_basestation_invalid +
            st->remote_rejected_bad + st->demod_rejected_bad +
            st->remote_rejected_unknown_icao + st->demod_rejected_unknown_icao);

    p = safe_snprintf(p, end, "readsb_messages_modes_valid %u\n", st->remote_accepted[0] + st->demod_accepted[0]);
    p = safe_snprintf(p, end, "readsb_messages_modes_valid_fixed_bit %u\n", st->remote_accepted[1] + st->demod_accepted[1]);
    p = safe_snprintf(p, end, "readsb_messages_modes_invalid_bad %u\n", st->remote_rejected_bad + st->demod_rejected_bad);
    p = safe_snprintf(p, end, "readsb_messages_modes_invalid_unknown_icao %u\n", st->remote_rejected_unknown_icao + st->demod_rejected_unknown_icao);
    p = safe_snprintf(p, end, "readsb_messages_modes_rejected_delayed %u\n", st->remote_rejected_delayed);

    p = safe_snprintf(p, end, "readsb_messages_basestation_valid %u\n", st->remote_received_basestation_valid);
    p = safe_snprintf(p, end, "readsb_messages_basestation_invalid %u\n", st->remote_received_basestation_invalid);

    p = safe_snprintf(p, end, "readsb_messages_modeac_valid %u\n", st->remote_received_modeac + st->demod_modeac);

    p = safe_snprintf(p, end, "readsb_network_bytes_in %lu\n", (long) st->network_bytes_in);
    p = safe_snprintf(p, end, "readsb_network_bytes_out %lu\n", (long) st->network_bytes_out);
    p = safe_snprintf(p, end, "readsb_network_malformed_beast_bytes %u\n", st->remote_malformed_beast);

    if (Modes.ping) {
        float bucketsize = PING_BUCKETBASE;
        float bucketmax = 0;
        for (int i = 0; i < PING_BUCKETS; i++) {
            bucketmax += bucketsize;
            bucketmax = nearbyint(bucketmax / 10) * 10;
            bucketsize *= PING_BUCKETMULT;
            p = safe_snprintf(p, end, "readsb_network_packets_rtt_%d %u\n", (int) bucketmax, st->remote_ping_rtt[i]);
        }
    }

    p = safe_snprintf(p, end, "readsb_tracks_all %u\n", st->unique_aircraft);
    p = safe_snprintf(p, end, "readsb_tracks_single_message %u\n", st->single_message_aircraft);

    p = safe_snprintf(p, end, "readsb_position_count_total %u\n", st->pos_all);
    p = safe_snprintf(p, end, "readsb_position_count_duplicate %u\n", st->pos_duplicate);
    p = safe_snprintf(p, end, "readsb_position_count_garbage %u\n", st->pos_garbage);
    for (int i = 0; i < NUM_TYPES; i++) {
        p = safe_snprintf(p, end, "readsb_position_count_%s %u\n", addrtype_enum_string(i), st->pos_by_type[i]);
    }


    for (int i = 0; i < Modes.net_connectors_count; i++) {
        struct net_connector *con = &Modes.net_connectors[i];
        int64_t value = 0;
        if (con->connected) {
            value = (now - con->lastConnect) / 1000.0;
        }
        p = safe_snprintf(p, end, "readsb_net_connector_status{host=\"%s\",port=\"%s\"} %"PRIu64"\n",
                con->address, con->port, value);
    }

    if (Modes.sdr_type != SDR_NONE) {
        if (!Modes.net_only) {
            p = safe_snprintf(p, end, "readsb_sdr_gain %.1f\n", Modes.gain / 10.0);
        }

        if (st->signal_power_sum > 0 && st->signal_power_count > 0)
            p = safe_snprintf(p, end, "readsb_signal_avg %.1f\n", 10 * log10(st->signal_power_sum / st->signal_power_count));
        else
            p = safe_snprintf(p, end, "readsb_signal_avg -50.0\n");
        if (st->noise_power_sum > 0 && st->noise_power_count > 0)
            p = safe_snprintf(p, end, "readsb_signal_noise %.1f\n", 10 * log10(st->noise_power_sum / st->noise_power_count));
        else
            p = safe_snprintf(p, end, "readsb_signal_noise -50.0\n");
        if (st->peak_signal_power > 0)
            p = safe_snprintf(p, end, "readsb_signal_peak %.1f\n", 10 * log10(st->peak_signal_power));
        else
            p = safe_snprintf(p, end, "readsb_signal_peak -50.0\n");

        p = safe_snprintf(p, end, "readsb_signal_strong %d\n", st->strong_signal_count);

        if (!Modes.net_only) {
            p = safe_snprintf(p, end, "readsb_demod_samples_processed %"PRIu64"\n", st->samples_processed);
            p = safe_snprintf(p, end, "readsb_demod_samples_dropped %"PRIu64"\n", st->samples_dropped);
            p = safe_snprintf(p, end, "readsb_demod_samples_lost %"PRIu64"\n", st->samples_lost);
            p = safe_snprintf(p, end, "readsb_demod_estimated_ppm %.1f\n", Modes.estimated_ppm);

            p = safe_snprintf(p, end, "readsb_demod_preambles %"PRIu32"\n", st->demod_preambles);
        }
    }
    if (Modes.json_globe_index) {
        p = safe_snprintf(p, end, "readsb_trace_current_memory %"PRIu64"\n", Modes.trace_current_size);
        p = safe_snprintf(p, end, "readsb_trace_chunk_memory %"PRIu64"\n", Modes.trace_chunk_size);
        p = safe_snprintf(p, end, "readsb_trace_cache_memory %"PRIu64"\n", Modes.trace_cache_size);
    }
    int64_t uptime = now - Modes.startup_time;
    if (now < Modes.startup_time)
        uptime = 0;
    p = safe_snprintf(p, end, "readsb_uptime %"PRIu64"\n", uptime);

    if (p >= end)
        fprintf(stderr, "buffer overrun stats prom\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

void statsResetCount() {
    struct statsCount *s = &(Modes.globalStatsCount);
    memset(&s->type_counts, 0, sizeof(s->type_counts));

    s->rssi_table_len = 0;

    s->readsb_aircraft_with_position = 0;
    s->readsb_aircraft_no_position = 0;

    s->readsb_aircraft_adsb_version_0 = 0;
    s->readsb_aircraft_adsb_version_1 = 0;
    s->readsb_aircraft_adsb_version_2 = 0;
    s->readsb_aircraft_emergency = 0;
    s->readsb_aircraft_rssi_average = 0;
    s->readsb_aircraft_rssi_max = -50;
    s->readsb_aircraft_rssi_min = 42;
    s->readsb_aircraft_total = 0;
    s->readsb_aircraft_with_flight_number = 0;
    s->readsb_aircraft_without_flight_number = 0;
}

void statsCountAircraft(int64_t now) {
    struct statsCount *s = &(Modes.globalStatsCount);
    uint32_t total_aircraft_count = 0;
    uint64_t trace_chunk_size = 0;
    uint64_t trace_cache_size = 0;
    uint64_t trace_current_size = 0;
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            total_aircraft_count++;

            if (Modes.json_globe_index) {
                trace_current_size += stateBytes(a->trace_current_max);
                trace_chunk_size += a->trace_chunk_overall_bytes;
                struct traceCache *tCache = &a->traceCache;
                if (tCache->entries) {
                    trace_cache_size += tCache->totalAlloc;
                }
            }

            if (!(a->messages >= 2 && (now < a->seen + TRACK_EXPIRE || trackDataValid(&a->position_valid))))
                continue;

            if (trackDataValid(&a->position_valid))
                s->readsb_aircraft_with_position++;
            else
                s->readsb_aircraft_no_position++;

            s->type_counts[a->addrtype]++;

            if (a->adsb_version == 0)
                s->readsb_aircraft_adsb_version_0++;
            else if (a->adsb_version == 1)
                s->readsb_aircraft_adsb_version_1++;
            else if (a->adsb_version == 2)
                s->readsb_aircraft_adsb_version_2++;

            if (trackDataValid(&a->emergency_valid) && a->emergency)
                s->readsb_aircraft_emergency++;

            double signal = 10 * log10((a->signalLevel[0] + a->signalLevel[1] + a->signalLevel[2] + a->signalLevel[3] +
                        a->signalLevel[4] + a->signalLevel[5] + a->signalLevel[6] + a->signalLevel[7] + 1e-5) / 8);

            if ((
                        a->addrtype == ADDR_MODE_S
                        || a->addrtype == ADDR_ADSB_ICAO
                        || a->addrtype == ADDR_ADSB_ICAO_NT
                        || a->addrtype == ADDR_ADSR_ICAO
                        || a->addrtype == ADDR_MLAT
                        || a->addrtype == ADDR_MODE_S
                ) && signal > -49.4 && signal < 1) {
                if (s->rssi_table_alloc < s->rssi_table_len + 1) {
                    s->rssi_table_alloc = 2 * s->rssi_table_len + 1024;
                    s->rssi_table = realloc(s->rssi_table, sizeof(float) * s->rssi_table_alloc);
                }
                s->rssi_table[s->rssi_table_len] = signal;
                s->rssi_table_len++;
            }

            if (trackDataValid(&a->callsign_valid))
                s->readsb_aircraft_with_flight_number++;
            else
                s->readsb_aircraft_without_flight_number++;
        }
    }

    Modes.total_aircraft_count = total_aircraft_count;
    Modes.trace_chunk_size = trace_chunk_size;
    Modes.trace_cache_size = trace_cache_size;
    Modes.trace_current_size = trace_current_size;

    static int64_t antiSpam2;
    if (total_aircraft_count > 2 * AIRCRAFT_BUCKETS && now > antiSpam2 + 12 * HOURS) {
        fprintf(stderr, "<3>increase AIRCRAFT_HASH_BITS, aircraft hash table fill: %0.1f\n", total_aircraft_count / (double) AIRCRAFT_BUCKETS);
        antiSpam2 = now;
    }
}


static float percentile(float p, float *values, int len) {

    float x = p * (len - 1);
    float d = x - ((int) x);
    int index = (int) x;

    float res;
    if (index + 1 < len)
        res = values[index] + d * (values[index + 1] - values[index]);
    else
        res = values[index];
    //printf("p: %.2f x: %.4f d: %.4f index: %d value: %.4f value_i1: %.4f\n", p, x, d, index, values[index], values[index+1]);
    return res;
}

static int compareFloat(const void *p1, const void *p2) {
    float a1 = *(const float *) p1;
    float a2 = *(const float *) p2;

    return (a1 > a2) - (a1 < a2);
}

static void statsCalc() {

    // add up the buckets for the 1 min, 5 min and 15 min stats
    reset_stats(&Modes.stats_1min);
    for (int i = 0; i < 6; ++i) {
        int index = (Modes.stats_bucket - i + STAT_BUCKETS) % STAT_BUCKETS;
        add_stats(&Modes.stats_10[index], &Modes.stats_1min, &Modes.stats_1min);
    }

    reset_stats(&Modes.stats_5min);
    for (int i = 0; i < 30; ++i) {
        int index = (Modes.stats_bucket - i + STAT_BUCKETS) % STAT_BUCKETS;
        add_stats(&Modes.stats_10[index], &Modes.stats_5min, &Modes.stats_5min);
    }

    reset_stats(&Modes.stats_15min);
    for (int i = 0; i < 90; ++i) {
        int index = (Modes.stats_bucket - i + STAT_BUCKETS) % STAT_BUCKETS;
        add_stats(&Modes.stats_10[index], &Modes.stats_15min, &Modes.stats_15min);
    }

    struct statsCount *s = &(Modes.globalStatsCount);
    s->readsb_aircraft_total = s->readsb_aircraft_with_position + s->readsb_aircraft_no_position;

    if (s->rssi_table_len > 0) {

        qsort(s->rssi_table, s->rssi_table_len, sizeof(float), compareFloat);

        //printf("after sort\n");
        //for (int i = 0; i < s->rssi_table_len; i++)
        //    printf("%.2f\n", s->rssi_table[i]);

        s->readsb_aircraft_rssi_min = s->rssi_table[0];
        s->readsb_aircraft_rssi_quart1 = percentile(0.25, s->rssi_table, s->rssi_table_len);
        s->readsb_aircraft_rssi_median = percentile(0.5, s->rssi_table, s->rssi_table_len);
        s->readsb_aircraft_rssi_quart3 = percentile(0.75, s->rssi_table, s->rssi_table_len);
        s->readsb_aircraft_rssi_max = s->rssi_table[s->rssi_table_len - 1];

        for (int i = 0; i < s->rssi_table_len; i++) {
            s->readsb_aircraft_rssi_average += s->rssi_table[i];
        }
        s->readsb_aircraft_rssi_average /= s->rssi_table_len;
    } else {
        s->readsb_aircraft_rssi_average = -50;
        s->readsb_aircraft_rssi_max = -50;
        s->readsb_aircraft_rssi_min = -50;
        s->readsb_aircraft_rssi_quart1 = -50;
        s->readsb_aircraft_rssi_median = -50;
        s->readsb_aircraft_rssi_quart3 = -50;
    }
    if (s->readsb_aircraft_rssi_min == 42)
        s->readsb_aircraft_rssi_min = -50;
}

void statsProcess(int64_t now) {
        statsCalc(); // calculate statistics stuff

        checkDisplayStats(now); // display stats if requested

        struct char_buffer prom = { 0 };
        if (Modes.json_dir || Modes.prom_file) {
            prom = generatePromFile(now);
        }
        if (Modes.json_dir) {
            free(writeJsonToFile(Modes.json_dir, "stats.json", generateStatsJson(now)).buffer);
            writeJsonToFile(Modes.json_dir, "stats.prom", prom);
        }

        if (Modes.prom_file) {
            writeJsonToFile(NULL, Modes.prom_file, prom);
        }

        sfree(prom.buffer);
}

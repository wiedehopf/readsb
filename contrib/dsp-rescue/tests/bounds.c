/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Standalone sanitizer check; no Python interpreter or RF hardware required. */
#include "../dsp_rescue/frontend.c"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
int main(void) {
    float iq[2048] = {0}, power[1024], weights[48] = {0}, taps[102] = {0}, out[128];
    double scores[1024];
    int64_t starts[] = {-1, 0, 8, 1020, INT64_MAX}, ids[35], methods[35];
    int64_t integers[6] = {-1, -1, -1, 0, 0, 1};
    uint8_t rows[35 * 14], valid[5];
    for (int64_t n = 0; n <= 1024; n++) {
        rescue_scores(iq, n, power, scores);
        int64_t count = rescue_header_ids(iq, n, starts, 5, ids);
        assert(count >= 0 && count <= 5);
        rescue_baseline(iq, n, starts, 5, rows, valid);
        count = rescue_dsp(iq, n, starts, 5, integers, weights, 1, 1, ids, methods, rows);
        assert(count >= 0 && count <= 35);
        rescue_resample_float(iq, n, 0, 64, taps, out);
    }
    return 0;
}

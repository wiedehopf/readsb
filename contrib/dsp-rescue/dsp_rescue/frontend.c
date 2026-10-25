/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fixed 2.4 -> 4 MS/s UC8 FIR and energy preamble score. No device access. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

void rescue_resample(const uint8_t *raw, int64_t n, int64_t first, int64_t count, const float *h,
                     float *out) {
    for (int64_t k = 0; k < count; k++) {
        int64_t time = (first + k) * 3 + 51;
        int phase = (int)(time % 5);
        float real = 0, imag = 0;
        for (int j = phase; j < 102; j += 5) {
            int64_t index = (time - j) / 5;
            if (index >= 0 && index < n) {
                float tap = h[j];
                real += tap * ((float)raw[2 * index] - 127.5f);
                imag += tap * ((float)raw[2 * index + 1] - 127.5f);
            }
        }
        out[2 * k] = real * (1.f / 128.f);
        out[2 * k + 1] = imag * (1.f / 128.f);
    }
}

void rescue_scores(const float *iq, int64_t n, float *power, double *score) {
    for (int64_t k = 0; k < n; k++)
        power[k] = iq[2 * k] * iq[2 * k] + iq[2 * k + 1] * iq[2 * k + 1];
    if (n < 32)
        return;
    double sum = 0, squared = 0;
    for (int k = 0; k < 32; k++) {
        sum += power[k];
        squared += (double)power[k] * power[k];
    }
    for (int64_t k = 0; k <= n - 32; k++) {
        double on = power[k] + power[k + 1] + power[k + 4] + power[k + 5] + power[k + 14] +
                    power[k + 15] + power[k + 18] + power[k + 19];
        score[k] = (on - .25 * sum) / sqrt(fmax(squared, 1.e-12) * 6.);
        if (k < n - 32) {
            sum += (double)power[k + 32] - power[k];
            squared += (double)power[k + 32] * power[k + 32] - (double)power[k] * power[k];
        }
    }
}

#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#endif

/* Padded, normalized float I/Q input; no per-tap bounds/conversion overhead. */
void rescue_resample_float(const float *src, int64_t n, int64_t first, int64_t count,
                           const float *h, float *out) {
    float packed[5][24] = {{0}};
    for (int p = 0; p < 5; p++)
        for (int j = p, k = 0; j < 102; j += 5, k++)
            packed[p][k] = h[j];
    for (int64_t k = 0; k < count; k++) {
        int64_t time = (first + k) * 3 + 51;
        int phase = (int)(time % 5);
        int64_t index = (time - phase) / 5;
        /* Python pads 600 samples on both sides. Guard also supports safe
           standalone use with a different valid input shape. */
        if (index < 23 || index >= n) {
            float r = 0, i = 0;
            for (int j = phase; j < 102; j += 5, index--)
                if (index >= 0 && index < n) {
                    r += h[j] * src[2 * index];
                    i += h[j] * src[2 * index + 1];
                }
            out[2 * k] = r;
            out[2 * k + 1] = i;
            continue;
        }
#if defined(__ARM_NEON) && defined(__aarch64__)
        float32x4_t ar = vdupq_n_f32(0), ai = vdupq_n_f32(0);
        for (int j = 0; j < 24; j += 4, index -= 4) {
            float32x4_t coeff = vld1q_f32(packed[phase] + j);
            coeff = vrev64q_f32(coeff);
            coeff = vextq_f32(coeff, coeff, 2);
            float32x4x2_t iq = vld2q_f32(src + 2 * (index - 3));
            ar = vmlaq_f32(ar, iq.val[0], coeff);
            ai = vmlaq_f32(ai, iq.val[1], coeff);
        }
        out[2 * k] = vaddvq_f32(ar);
        out[2 * k + 1] = vaddvq_f32(ai);
#else
        float ar[4] = {0}, ai[4] = {0};
        for (int j = 0; j < 24; j += 4, index -= 4)
            for (int q = 0; q < 4; q++) {
                ar[q] += packed[phase][j + q] * src[2 * (index - q)];
                ai[q] += packed[phase][j + q] * src[2 * (index - q) + 1];
            }
        out[2 * k] = (ar[0] + ar[1]) + (ar[2] + ar[3]);
        out[2 * k + 1] = (ai[0] + ai[1]) + (ai[2] + ai[3]);
#endif
    }
}

static inline float power_at(const float *iq, int64_t k) {
    return iq[2 * k] * iq[2 * k] + iq[2 * k + 1] * iq[2 * k + 1];
}
int64_t rescue_header_ids(const float *iq, int64_t size, const int64_t *starts, int64_t n,
                          int64_t *ids) {
    const int on[8] = {0, 1, 4, 5, 14, 15, 18, 19};
    const int df17[5] = {1, 0, 0, 0, 1};
    int64_t count = 0;
    for (int64_t k = 0; k < n; k++) {
        int64_t s = starts[k];
        if (s < 0 || size < 52 || s > size - 52)
            continue;
        float scale = 0;
        for (int i = 0; i < 8; i++)
            scale += power_at(iq, s + on[i]);
        scale = fmaxf(scale * (1.f / 8.f), 1.e-5f);
        int valid = 1;
        for (int b = 0; b < 5; b++) {
            int64_t j = s + 32 + 4 * b;
            /* Divide and clip exactly as the Python feature transform. */
            float left =
                fminf(power_at(iq, j) / scale, 12.f) + fminf(power_at(iq, j + 1) / scale, 12.f);
            float right =
                fminf(power_at(iq, j + 2) / scale, 12.f) + fminf(power_at(iq, j + 3) / scale, 12.f);
            if ((left > right) != df17[b]) {
                valid = 0;
                break;
            }
        }
        if (valid)
            ids[count++] = k;
    }
    return count;
}

/* Native bounded timing and matched-filter retries; never sends network data.
 * Outputs have capacity n*7 (six timing shifts and one matched hypothesis).
 * Interpolation uses the same float32 accumulation order as the reference. */
#include <complex.h>
static int rescue_row_ok(const uint8_t *row) {
    if ((row[0] >> 3) != 17)
        return 0;
    uint32_t crc = 0;
    for (int b = 0; b < 112; b++) {
        int top = ((crc >> 23) & 1) ^ ((row[b / 8] >> (7 - b % 8)) & 1);
        crc = (crc << 1) & 0xffffff;
        if (top)
            crc ^= 0xfff409;
    }
    return crc == 0;
}
int64_t rescue_dsp(const float *iq, int64_t size, const int64_t *starts, int64_t n,
                   const int64_t *integer, const float *weights, int timing, int matched,
                   int64_t *ids, int64_t *methods, uint8_t *rows) {
    int64_t count = 0;
    const int pulses[8] = {0, 1, 4, 5, 14, 15, 18, 19};
    const double tau = 6.283185307179586476925286766559;
    for (int64_t k = 0; k < n; k++) {
        int64_t s = starts[k];
        if (s < 8 || size <= 488 || s >= size - 488)
            continue;
        if (timing)
            for (int phase = 0; phase < 6; phase++) {
                uint8_t row[14] = {0};
                for (int b = 0; b < 112; b++) {
                    float power[4];
                    for (int q = 0; q < 4; q++) {
                        int64_t at = s + 32 + 4 * b + q + integer[phase];
                        float re = 0, im = 0;
                        if (phase == 0 || phase == 5) {
                            re = iq[2 * at];
                            im = iq[2 * at + 1];
                        } else
                            for (int t = 0; t < 8; t++) {
                                float w = weights[phase * 8 + t];
                                re += iq[2 * (at + t - 3)] * w;
                                im += iq[2 * (at + t - 3) + 1] * w;
                            }
                        // np.abs(complex64)**2 rounds the magnitude before squaring.
                        float magnitude = hypotf(re, im);
                        power[q] = magnitude * magnitude;
                    }
                    int bit = (power[0] + power[1]) > (power[2] + power[3]);
                    row[b / 8] |= bit << (7 - b % 8);
                    if (b == 4 && (row[0] >> 3) != 17)
                        break;
                }
                if (rescue_row_ok(row)) {
                    ids[count] = k;
                    methods[count] = phase;
                    for (int j = 0; j < 14; j++)
                        rows[14 * count + j] = row[j];
                    count++;
                }
            }
        if (matched) {
            double best = -1, frequency = 0;
            for (int f = -100000; f <= 100000; f += 5000) {
                double complex sum = 0;
                for (int q = 0; q < 8; q++) {
                    int at = pulses[q];
                    double angle = -tau * f * at / 4000000.;
                    sum += (iq[2 * (s + at)] + I * iq[2 * (s + at) + 1]) *
                           (cos(angle) + I * sin(angle));
                }
                double power = creal(sum) * creal(sum) + cimag(sum) * cimag(sum);
                if (power > best) {
                    best = power;
                    frequency = f;
                }
            }
            double angle = -tau * frequency / 4000000.;
            double complex step = cos(angle) + I * sin(angle);
            double complex z[488], rotation = cos(-4 * angle) + I * sin(-4 * angle);
            for (int q = 0; q < 488; q++) {
                z[q] = (iq[2 * (s + q - 4)] + I * iq[2 * (s + q - 4) + 1]) * rotation;
                rotation *= step;
            }
            double complex shape[4] = {0};
            int p[4] = {0, 4, 14, 18};
            for (int q = 0; q < 4; q++)
                for (int j = 0; j < 4; j++)
                    shape[q] += z[4 + p[j] + q - 1] / 4.;
            uint8_t row[14] = {0};
            for (int b = 0; b < 112; b++) {
                double complex left = 0, right = 0;
                int at = 36 + 4 * b;
                for (int q = 0; q < 4; q++) {
                    left += z[at + q - 1] * conj(shape[q]);
                    right += z[at + q + 1] * conj(shape[q]);
                }
                double a = creal(left) * creal(left) + cimag(left) * cimag(left),
                       v = creal(right) * creal(right) + cimag(right) * cimag(right);
                row[b / 8] |= (a > v) << (7 - b % 8);
                if (b == 4 && (row[0] >> 3) != 17)
                    break;
            }
            if (rescue_row_ok(row)) {
                ids[count] = k;
                methods[count] = 6;
                for (int j = 0; j < 14; j++)
                    rows[14 * count + j] = row[j];
                count++;
            }
        }
    }
    return count;
}

/* Clipped-energy slicer. The caller supplies header-screened windows. */
void rescue_baseline(const float *iq, int64_t size, const int64_t *starts, int64_t n, uint8_t *rows,
                     uint8_t *valid) {
    const int on[8] = {0, 1, 4, 5, 14, 15, 18, 19};
    for (int64_t k = 0; k < n; k++) {
        int64_t s = starts[k];
        uint8_t *row = rows + 14 * k;
        for (int j = 0; j < 14; j++)
            row[j] = 0;
        valid[k] = 0;
        if (s < 0 || size < 480 || s > size - 480)
            continue;
        float p[480];
        for (int q = 0; q < 480; q++) {
            float v = hypotf(iq[2 * (s + q)], iq[2 * (s + q) + 1]);
            p[q] = v * v;
        }
        float a[8];
        for (int q = 0; q < 8; q++)
            a[q] = p[on[q]];
        // NumPy pairwise eight-element reduction.
        float scale =
            fmaxf(((a[0] + a[1]) + (a[2] + a[3]) + (a[4] + a[5]) + (a[6] + a[7])) / 8.f, 1.e-5f);
        for (int b = 0; b < 112; b++) {
            int q = 32 + 4 * b;
            float left = fminf(p[q] / scale, 12.f) + fminf(p[q + 1] / scale, 12.f),
                  right = fminf(p[q + 2] / scale, 12.f) + fminf(p[q + 3] / scale, 12.f);
            row[b / 8] |= (left > right) << (7 - b % 8);
        }
        valid[k] = rescue_row_ok(row);
    }
}

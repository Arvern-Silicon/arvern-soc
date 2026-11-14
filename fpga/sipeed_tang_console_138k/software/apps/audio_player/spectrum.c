//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      spectrum.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The input is real: its 512 windowed samples are taken as 256 complex ones
// (even samples real, odd ones imaginary) through a 256-point radix-2 FFT in
// Q15, each stage halving its results (rounded) so nothing overflows, then
// the 512-point spectrum's bins are unpacked from it (X[k] = E[k] + W^k O[k],
// E and O the even and odd samples' transforms): half the
// work of a complex 512-point FFT, the same scale (divided by 512). The
// input is halved and windowed: a full-scale sine reaches a bin magnitude of
// 4096, a power of 2^24, the 0 dB reference. On the RISC-V target the
// butterflies are spectrum_fft.S. Twiddles from a rotation in Q30 at
// start-up (no floating point). A band's level is its loudest bin, or for a
// band narrower than a bin, the power interpolated at its lower edge.
//----------------------------------------------------------------------------
#include "spectrum.h"

#define N        SPEC_N
#define LOG2_N   9u

static int16_t  cos_t[N / 2u], sin_t[N / 2u];   // cos, sin (2 pi k / N), Q15
static int16_t  hann[N];                        // Q15
#define M        (N / 2u)                      // the complex FFT's length
static int32_t  z[2u * M];                      // its samples: real, imaginary, interleaved
static int16_t  tw[2u * M];                     // W_N^k: cos, -sin, interleaved
#if defined(__riscv)
void fft256(int32_t *z, const int16_t *tw);     // spectrum_fft.S: the butterflies, the same results
#endif
static uint32_t pw[N / 2u];
static uint16_t edge[SPEC_BANDS + 1u];          // band edges in bins, Q8

void spectrum_init(uint32_t fs)
{
    // A rotation by 2 pi / N per step: cos and sin of it in Q30
    int64_t c = 1 << 30, s = 0;
    for (unsigned k = 0; k < N / 2u; k++) {
        int32_t cq = (int32_t) ((c + (1 << 14)) >> 15), sq = (int32_t) ((s + (1 << 14)) >> 15);
        cos_t[k] = (int16_t) ((cq > 32767) ? 32767 : cq);
        sin_t[k] = (int16_t) ((sq > 32767) ? 32767 : sq);
        const int64_t cn = (c * 1073660973 - s * 13176464) >> 30;
        s = (s * 1073660973 + c * 13176464) >> 30;
        c = cn;
    }
    for (unsigned k = 0; k < M; k++) {
        tw[2u * k]      = cos_t[k];
        tw[2u * k + 1u] = (int16_t) -sin_t[k];
    }
    for (unsigned i = 0; i < N; i++) {
        const int32_t cs = (i < N / 2u) ? cos_t[i] : -cos_t[i - N / 2u];
        hann[i] = (int16_t) ((32767 - cs) >> 1);
    }
    // 40 Hz x r^b, r = (16000 / 40)^(1/32) = 1.20591 (Q16 79030)
    uint64_t f = 40ull << 16;
    for (unsigned b = 0; b <= SPEC_BANDS; b++) {
        edge[b] = (uint16_t) (((f * N) << 8) / ((uint64_t) fs << 16));
        f = (f * 79030u) >> 16;
    }
}

static void fft(void)
{
    for (unsigned i = 1, j = 0; i < M; i++) {      // bit-reversed order
        unsigned b = M >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j ^= b;
        if (i < j) {
            int32_t t = z[2u * i];
            z[2u * i] = z[2u * j];
            z[2u * j] = t;
            t = z[2u * i + 1u];
            z[2u * i + 1u] = z[2u * j + 1u];
            z[2u * j + 1u] = t;
        }
    }
#if defined(__riscv)
    fft256(z, tw);
#else
    // a stage of butterflies 2 x half wide, twiddle k the table's k N / (2 half)
    for (unsigned half = 1, step = N / 2u; half < M; half <<= 1, step >>= 1) {
        for (unsigned g = 0; g < M; g += 2u * half) {
            for (unsigned k = 0; k < half; k++) {
                const int32_t wr = tw[2u * k * step], wi = tw[2u * k * step + 1u];
                int32_t *const u = &z[2u * (g + k)], *const v = u + 2u * half;
                const int32_t tr = (v[0] * wr - v[1] * wi + (1 << 14)) >> 15;
                const int32_t ti = (v[0] * wi + v[1] * wr + (1 << 14)) >> 15;
                v[0] = (u[0] - tr + 1) >> 1;
                v[1] = (u[1] - ti + 1) >> 1;
                u[0] = (u[0] + tr + 1) >> 1;
                u[1] = (u[1] + ti + 1) >> 1;
            }
        }
    }
#endif
}

// Power to a level: 66 dB over 0..255, from log2 in Q8 (the mantissa's top
// bits taken as the fraction: 0.26 dB at worst)
static uint8_t level_of(uint32_t p)
{
    if (p == 0u)
        return 0u;
    const unsigned e = 31u - (unsigned) __builtin_clz(p);
    const uint32_t frac = ((p << (31u - e)) >> 23) & 0xFFu;
    const int32_t  l = 255 + ((((int32_t) (e * 256u + frac)) - 24 * 256) * 2978 >> 16);   // 66 dB = 24 bits x 3.0103 / 66 x 255 per bit
    return (uint8_t) ((l < 0) ? 0 : (l > 255) ? 255 : l);
}

void spectrum_run(const int16_t *x, uint8_t *level)
{
    for (unsigned m = 0; m < N; m++)
        z[m] = ((int32_t) x[m] * hann[m]) >> 16;     // even samples real, odd ones imaginary
    fft();
    // X[k] = (2E + W^k 2O) / 2, with 2E = Z[k] + conj Z[M - k] and
    // 2O = (Z[k] - conj Z[M - k]) / j, W = e^(-j 2 pi k / N); halved once
    // more: Z came divided by M, X by N
    for (unsigned k = 0; k < M; k++) {
        const unsigned n = (M - k) & (M - 1u);
        const int32_t zrk = z[2u * k], zik = z[2u * k + 1u], zrn = z[2u * n], zin = z[2u * n + 1u];
        const int32_t er = zrk + zrn, ei = zik - zin;
        const int32_t orr = zik + zin, oi = zrn - zrk;
        const int32_t c = cos_t[k], sn = sin_t[k];
        const int32_t wr = (c * orr + sn * oi + (1 << 14)) >> 15;
        const int32_t wi = (c * oi - sn * orr + (1 << 14)) >> 15;
        const int32_t xr = (er + wr + 2) >> 2, xi = (ei + wi + 2) >> 2;
        pw[k] = (uint32_t) (xr * xr) + (uint32_t) (xi * xi);
    }
    for (unsigned b = 0; b < SPEC_BANDS; b++) {
        const unsigned k0 = edge[b] >> 8, k1 = (edge[b + 1u] + 255u) >> 8;
        uint32_t p = 0u;
        if ((unsigned) (edge[b + 1u] - edge[b]) < 256u) {
            const uint32_t f = edge[b] & 0xFFu;
            const unsigned k = (k0 < N / 2u - 1u) ? k0 : N / 2u - 2u;
            p = (uint32_t) (((uint64_t) pw[k] * (256u - f) + (uint64_t) pw[k + 1u] * f) >> 8);
        } else {
            for (unsigned k = k0; k < k1 && k < N / 2u; k++)
                if (pw[k] > p)
                    p = pw[k];
        }
        level[b] = level_of(p);
    }
}

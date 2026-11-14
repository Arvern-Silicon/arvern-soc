//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      codec.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// IMA ADPCM: the IMA step and index tables; a nibble n gives the difference
// step/8 + (n & 1) step/4 + (n & 2) step/2 + (n & 4) step, negated by n & 8.
// In a block, mono nibbles follow in byte order, low nibble first; stereo
// alternates 4 bytes (8 samples) of the left with 4 of the right.
//
// QOA (format by Dominic Szablewski): all values big-endian. A frame: a
// header (channels, rate, samples, size), each channel's LMS history and
// weights (4 + 4 int16), then for each 20 samples one 64-bit slice a
// channel: a 4-bit scale factor and 20 3-bit residuals. A sample is the
// prediction (the history weighted, >> 13) plus the dequantized residual,
// clamped; the weights then move by residual >> 4 with the history's signs.
//----------------------------------------------------------------------------
#include "codec.h"

codec_t codec;

static inline void put(sink_t *s, int32_t l, int32_t r)
{
    const uint32_t w = *s->w;
    s->ring[w & s->mask] = ((uint32_t) (uint16_t) r << 16) | (uint16_t) l;
    *s->w = w + 1u;
}

static inline int32_t clamp16(int32_t v)
{
    return (v < -32768) ? -32768 : (v > 32767) ? 32767 : v;
}

//==========================================================
// PCM
//==========================================================

static unsigned pcm(const uint8_t *p, unsigned n, sink_t *s)
{
    const unsigned fb = (codec.bits / 8u) * codec.channels;
    n -= n % fb;
    for (unsigned i = 0; i < n; i += fb) {
        int32_t l, r;
        if (codec.bits == 16u) {
            l = (int16_t) (p[i] | (p[i + 1u] << 8));
            r = (codec.channels == 2u) ? (int16_t) (p[i + 2u] | (p[i + 3u] << 8)) : l;
        } else {
            l = (p[i] - 128) << 8;
            r = (codec.channels == 2u) ? (p[i + 1u] - 128) << 8 : l;
        }
        put(s, l, r);
    }
    return n;
}

//==========================================================
// IMA ADPCM
//==========================================================

static const uint16_t ima_step[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int32_t pred; int32_t index; } ima_t;

static inline int32_t ima_nibble(ima_t *c, unsigned n)
{
    const int32_t step = ima_step[c->index];
    int32_t d = step >> 3;
    if (n & 1u) d += step >> 2;
    if (n & 2u) d += step >> 1;
    if (n & 4u) d += step;
    c->pred = clamp16((n & 8u) ? c->pred - d : c->pred + d);
    c->index += ima_index[n & 7u];
    c->index = (c->index < 0) ? 0 : (c->index > 88) ? 88 : c->index;
    return c->pred;
}

unsigned codec_adpcm_frames(unsigned n)
{
    const unsigned ch = codec.channels;
    return (n < 4u * ch) ? 0u : 1u + ((n - 4u * ch) * 2u) / ch;
}

// One block of n bytes (a whole block, or the short last one)
static void adpcm_block(const uint8_t *p, unsigned n, sink_t *s)
{
    const unsigned ch = codec.channels;
    ima_t c[2] = { { 0, 0 }, { 0, 0 } };
    for (unsigned k = 0; k < ch; k++) {
        c[k].pred  = (int16_t) (p[4u * k] | (p[4u * k + 1u] << 8));
        c[k].index = (p[4u * k + 2u] > 88u) ? 88 : p[4u * k + 2u];
    }
    put(s, c[0].pred, c[ch - 1u].pred);
    p += 4u * ch;
    n -= 4u * ch;
    if (ch == 1u) {
        for (unsigned i = 0; i < n; i++) {
            const int32_t a = ima_nibble(&c[0], p[i] & 0xFu);
            put(s, a, a);
            const int32_t b = ima_nibble(&c[0], p[i] >> 4);
            put(s, b, b);
        }
    } else {
        int16_t l[8];
        for (unsigned i = 0; i + 8u <= n; i += 8u) {
            for (unsigned j = 0; j < 4u; j++) {
                l[2u * j]      = (int16_t) ima_nibble(&c[0], p[i + j] & 0xFu);
                l[2u * j + 1u] = (int16_t) ima_nibble(&c[0], p[i + j] >> 4);
            }
            for (unsigned j = 0; j < 4u; j++) {
                put(s, l[2u * j],      ima_nibble(&c[1], p[i + 4u + j] & 0xFu));
                put(s, l[2u * j + 1u], ima_nibble(&c[1], p[i + 4u + j] >> 4));
            }
        }
    }
}

static unsigned adpcm(const uint8_t *p, unsigned n, bool last, sink_t *s)
{
    const unsigned b = codec.block;
    unsigned used = 0;
    for (; n - used >= b; used += b)
        adpcm_block(p + used, b, s);
    if (last && (n - used >= 4u * codec.channels)) {
        adpcm_block(p + used, n - used, s);
        used = n;
    }
    return used;
}

//==========================================================
// QOA
//==========================================================

// round_ties_away_from_zero(round(pow(s + 1, 2.75)) x {0.75, -0.75, 2.5,
// -2.5, 4.5, -4.5, 7, -7}[q]), the format's table
static const int16_t qoa_dequant[16][8] = {
    {    1,    -1,    3,    -3,    5,    -5,     7,     -7 },
    {    5,    -5,   18,   -18,   32,   -32,    49,    -49 },
    {   16,   -16,   53,   -53,   95,   -95,   147,   -147 },
    {   34,   -34,  113,  -113,  203,  -203,   315,   -315 },
    {   63,   -63,  210,  -210,  378,  -378,   588,   -588 },
    {  104,  -104,  345,  -345,  621,  -621,   966,   -966 },
    {  158,  -158,  528,  -528,  950,  -950,  1477,  -1477 },
    {  228,  -228,  760,  -760, 1368, -1368,  2128,  -2128 },
    {  316,  -316, 1053, -1053, 1895, -1895,  2947,  -2947 },
    {  422,  -422, 1405, -1405, 2529, -2529,  3934,  -3934 },
    {  548,  -548, 1828, -1828, 3290, -3290,  5117,  -5117 },
    {  696,  -696, 2320, -2320, 4176, -4176,  6496,  -6496 },
    {  868,  -868, 2893, -2893, 5207, -5207,  8099,  -8099 },
    { 1064, -1064, 3548, -3548, 6386, -6386,  9933,  -9933 },
    { 1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005 },
    { 1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336 },
};

typedef struct { int32_t h[4], w[4]; } lms_t;

#if defined(__riscv)
void qoa_slice20(lms_t *m, int16_t *s);         // qoa_slice.S: a whole slice in the ring
void qoa_slice20_mono(lms_t *m, int16_t *s);
#endif

static inline uint32_t be16(const uint8_t *p) { return ((uint32_t) p[0] << 8) | p[1]; }
static inline uint32_t be32(const uint8_t *p) { return (be16(p) << 16) | be16(p + 2); }

// A slice's 20 residuals, dequantized, into s[0], s[k], s[2k], ..:
// residuals 0-9 are bits 59-30 of the slice, 10-19 bits 29-0, each group
// taken from the top of a 32-bit word
static void qoa_unpack(const uint8_t *p, int16_t *s, unsigned k)
{
    const uint32_t hi = be32(p), lo = be32(p + 4);
    const int16_t *dq = qoa_dequant[hi >> 28];
    uint32_t a = (hi << 4) | (lo >> 28), b = lo << 2;
    int16_t *s2 = s + 10u * k;
    for (unsigned i = 0; i < 10u; i++, a <<= 3, b <<= 3, s += k, s2 += k) {
        *s  = dq[a >> 29];
        *s2 = dq[b >> 29];
    }
}

// w += h < 0 ? -d : d, without a branch
#define SGN_ADD(w, h, d)  ((w) += ((d) ^ ((h) >> 31)) - ((h) >> 31))

// One sample: the history h0 (oldest) .. h3, the residual s[i] in, the
// sample out; the oldest entry takes the sample, so the next step names
// the history h1, h2, h3, h0
#define QOA_STEP(h0, h1, h2, h3, i)                                         \
    do {                                                                    \
        const int32_t r = s[i];                                             \
        int32_t v = ((w0 * h0 + w1 * h1 + w2 * h2 + w3 * h3) >> 13) + r;    \
        v = (v < -32768) ? -32768 : (v > 32767) ? 32767 : v;               \
        const int32_t d = r >> 4;                                           \
        SGN_ADD(w0, h0, d);                                                 \
        SGN_ADD(w1, h1, d);                                                 \
        SGN_ADD(w2, h2, d);                                                 \
        SGN_ADD(w3, h3, d);                                                 \
        h0 = v;                                                             \
        s[i] = (int16_t) v;                                                 \
    } while (0)

// One slice of one channel, in place: s[0..nr) holds the dequantized
// residuals and gets the samples. A whole slice runs in blocks of four
// steps, the history's names turning with each step (20 steps: back in
// order). On the RISC-V target, whole slices go straight into the ring
// through qoa_slice.S instead.
static void qoa_slice(lms_t *m, int16_t *s, unsigned nr)
{
    int32_t h0 = m->h[0], h1 = m->h[1], h2 = m->h[2], h3 = m->h[3];
    int32_t w0 = m->w[0], w1 = m->w[1], w2 = m->w[2], w3 = m->w[3];
    if (nr == 20u) {
        for (int16_t *const end = s + 20; s < end; s += 4) {
            QOA_STEP(h0, h1, h2, h3, 0);
            QOA_STEP(h1, h2, h3, h0, 1);
            QOA_STEP(h2, h3, h0, h1, 2);
            QOA_STEP(h3, h0, h1, h2, 3);
        }
    } else {                                    // a file's last slice: one step, then the history turned back
        for (unsigned i = 0; i < nr; i++, s++) {
            QOA_STEP(h0, h1, h2, h3, 0);
            const int32_t t = h0;
            h0 = h1;
            h1 = h2;
            h2 = h3;
            h3 = t;
        }
    }
    m->h[0] = h0; m->h[1] = h1; m->h[2] = h2; m->h[3] = h3;
    m->w[0] = w0; m->w[1] = w1; m->w[2] = w2; m->w[3] = w3;
}

static unsigned qoa(const uint8_t *p, unsigned n, sink_t *s)
{
    const unsigned ch = codec.channels;
    unsigned used = 0;
    while (n - used >= 8u) {
        const uint8_t *f = p + used;
        const unsigned fch = f[0], fs = be16(f + 4), size = be16(f + 6);
        const unsigned hdr = 8u + 16u * fch, slices = (fs + 19u) / 20u;
        if ((fch != ch) || !fs || (size < hdr + 8u * slices * ch) || (slices > 256u))
            return n;                            // not a frame: the rest is skipped
        if (n - used < size)
            break;
        lms_t m[2];
        for (unsigned c = 0; c < ch; c++)
            for (unsigned i = 0; i < 4u; i++) {
                m[c].h[i] = (int16_t) be16(f + 8u + 16u * c + 2u * i);
                m[c].w[i] = (int16_t) be16(f + 16u + 16u * c + 2u * i);
            }
        const uint8_t *q = f + hdr;
        int16_t sl[2][20];
        for (unsigned k = 0; k < slices; k++) {
            const unsigned nr = (fs - 20u * k < 20u) ? fs - 20u * k : 20u;
#if defined(__riscv)
            // a whole slice not crossing the ring's end: decoded in place,
            // each channel in its half of the frames (not yet published)
            const uint32_t w0 = *s->w;
            if ((nr == 20u) && ((w0 & s->mask) + 20u <= s->mask + 1u)) {
                int16_t *const d = (int16_t *) (uintptr_t) &s->ring[w0 & s->mask];
                for (unsigned c = 0; c < ch; c++, q += 8) {
                    qoa_unpack(q, d + c, 2u);
                    if (ch == 1u)
                        qoa_slice20_mono(&m[0], d);
                    else
                        qoa_slice20(&m[c], d + c);
                }
                *s->w = w0 + 20u;
                continue;
            }
#endif
            for (unsigned c = 0; c < ch; c++, q += 8) {
                qoa_unpack(q, sl[c], 1u);
                qoa_slice(&m[c], sl[c], nr);
            }
            // the slice's frames, published at once
            const int16_t *const r = sl[ch - 1u];
            uint32_t w = *s->w;
            for (unsigned i = 0; i < nr; i++, w++)
                s->ring[w & s->mask] = ((uint32_t) (uint16_t) r[i] << 16) | (uint16_t) sl[0][i];
            *s->w = w;
        }
        used += size;
    }
    return used;
}

//==========================================================

unsigned codec_frames_max(unsigned n)
{
    const unsigned ch = codec.channels;
    switch (codec.fmt) {
    case CODEC_ADPCM: return (n * 2u) / ch + 1u;
    case CODEC_QOA:   return (n * 5u) / (2u * ch);           // 20 frames a channel's 8 bytes
    default:          return n / ((codec.bits / 8u) * ch);
    }
}

unsigned codec_run(const uint8_t *p, unsigned n, bool last, sink_t *s)
{
    switch (codec.fmt) {
    case CODEC_ADPCM: return adpcm(p, n, last, s);
    case CODEC_QOA:   return qoa(p, n, s);
    default:          return pcm(p, n, s);
    }
}

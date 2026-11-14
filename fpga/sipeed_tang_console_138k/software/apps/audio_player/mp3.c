//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      mp3.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// MPEG-1 Layer III (ISO/IEC 11172-3). A frame: a 4-byte header, a CRC
// (optional), the side information (each granule and channel's coding
// parameters), then main data that may start in earlier frames (the bit
// reservoir, main_data_begin bytes back). Each of the two granules of 576
// lines a channel: scale factors, the Huffman-coded spectrum (big values in
// up to three regions of pairs, then count1 quadruples of -1 / 0 / 1),
// requantised as sign x |x|^(4/3) x 2^(gain / 4); joint stereo (mid/side,
// intensity); short blocks reordered; alias reduction between subbands;
// an inverse MDCT a subband (36 points, or three of 12 for short blocks),
// windowed and overlapped with the previous granule; then the polyphase
// synthesis filterbank: 32 subbands into 32 samples, 18 times a granule.
//
// Fixed point: the spectrum and the subband samples are Q24 (1.0 = 2^24),
// the transform factors Q31 or Q30 through high-word products (mulh), the
// synthesis window Q30.
//----------------------------------------------------------------------------
#include <string.h>
#include "mp3.h"
#include "mp3_tab.h"

#define GR_N        576u

typedef struct {
    uint16_t part23, big_values;
    uint8_t  global_gain, scf_compress, win_switch, block_type, mixed;
    uint8_t  table[3], sb_gain[3], region0, region1;
    uint8_t  preflag, scf_scale, count1_table;
} gr_t;

typedef struct {
    uint8_t  in[2048];                      // the stream's bytes
    uint8_t  res[2048];                     // the bit reservoir and the frame's main data
    int32_t  xr[2][GR_N];                   // a granule's spectrum, then its subband samples
    int32_t  ovl[2][GR_N];                  // the IMDCT's overlap
    int32_t  v[2][2048];                    // the synthesis FIFO, twice: no tap wraps
    int16_t  pcm[2 * GR_N];                 // a granule, interleaved
    uint8_t  scf_l[2][22];                  // long-block scale factors
    uint8_t  scf_s[2][13][3];               // short-block scale factors
    unsigned voff[2];
} mp3_mem_t;

static mp3_mem_t   *m;
static mp3_read_t   rd_fn;
static unsigned     in_pos, in_len, in_eof, res_len;
static unsigned     sr_idx, nch, mode, mode_ext, started;
static gr_t         gr[2][2];
static unsigned     scfsi[2];
static unsigned     nz[2];                  // the lines past the last non-zero one, a channel

static const uint16_t kbps[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
static const uint32_t rates[3] = { 44100u, 48000u, 32000u };
static const uint8_t  slen[16][2] = {
    {0,0},{0,1},{0,2},{0,3},{3,0},{1,1},{1,2},{1,3},{2,1},{2,2},{2,3},{3,1},{3,2},{3,3},{4,2},{4,3}
};
#define INV_SQRT2   1518500250              // 1 / sqrt(2), Q31

static inline int32_t mulh(int32_t a, int32_t b) { return (int32_t) (((int64_t) a * b) >> 32); }

//==========================================================
// THE STREAM
//==========================================================

// At least n bytes in in[] from in_pos (fewer at the stream's end)
static unsigned fill(unsigned n)
{
    if (in_len - in_pos < n) {
        memmove(m->in, m->in + in_pos, in_len - in_pos);
        in_len -= in_pos;
        in_pos = 0;
        while (!in_eof && in_len < n) {
            const unsigned got = rd_fn(m->in + in_len, sizeof(m->in) - in_len);
            if (!got) in_eof = 1;
            in_len += got;
        }
    }
    return in_len - in_pos;
}

// A frame header: its length in bytes (0: not a supported one)
static unsigned header(const uint8_t *h)
{
    if (h[0] != 0xFFu || (h[1] & 0xFEu) != 0xFAu)           // sync, MPEG-1, Layer III
        return 0;
    const unsigned bi = h[2] >> 4, si = (h[2] >> 2) & 3u;
    if (!bi || bi == 15u || si == 3u)
        return 0;
    return 144u * 1000u * kbps[bi] / rates[si] + ((h[2] >> 1) & 1u);
}

//==========================================================
// BITS
//==========================================================

typedef struct {
    const uint8_t *p;
    uint32_t       pos;                     // in bits
} bits_t;

static uint32_t getbits(bits_t *b, unsigned n)
{
    if (!n)
        return 0;
    const uint8_t *q = b->p + (b->pos >> 3);
    uint32_t w = ((uint32_t) q[0] << 24) | ((uint32_t) q[1] << 16) | ((uint32_t) q[2] << 8) | q[3];
    w = (w << (b->pos & 7u)) >> (32u - n);
    b->pos += n;
    return w;
}

//==========================================================
// SIDE INFORMATION, SCALE FACTORS
//==========================================================

static unsigned side_info(bits_t *b)
{
    const unsigned main_begin = getbits(b, 9);
    getbits(b, (nch == 1u) ? 5u : 3u);
    for (unsigned c = 0; c < nch; c++)
        scfsi[c] = getbits(b, 4);
    for (unsigned g = 0; g < 2u; g++)
        for (unsigned c = 0; c < nch; c++) {
            gr_t *r = &gr[g][c];
            r->part23 = (uint16_t) getbits(b, 12);
            r->big_values = (uint16_t) getbits(b, 9);
            if (r->big_values > 288u) r->big_values = 288u;
            r->global_gain = (uint8_t) getbits(b, 8);
            r->scf_compress = (uint8_t) getbits(b, 4);
            r->win_switch = (uint8_t) getbits(b, 1);
            if (r->win_switch) {
                r->block_type = (uint8_t) getbits(b, 2);
                r->mixed = (uint8_t) getbits(b, 1);
                r->table[0] = (uint8_t) getbits(b, 5);
                r->table[1] = (uint8_t) getbits(b, 5);
                r->table[2] = 0;
                for (unsigned w = 0; w < 3u; w++)
                    r->sb_gain[w] = (uint8_t) getbits(b, 3);
                r->region0 = (r->block_type == 2u && !r->mixed) ? 8u : 7u;
                r->region1 = 36u;
            } else {
                r->block_type = r->mixed = 0;
                for (unsigned t = 0; t < 3u; t++)
                    r->table[t] = (uint8_t) getbits(b, 5);
                r->sb_gain[0] = r->sb_gain[1] = r->sb_gain[2] = 0;
                r->region0 = (uint8_t) getbits(b, 4);
                r->region1 = (uint8_t) getbits(b, 3);
            }
            r->preflag = (uint8_t) getbits(b, 1);
            r->scf_scale = (uint8_t) getbits(b, 1);
            r->count1_table = (uint8_t) getbits(b, 1);
        }
    return main_begin;
}

static void scale_factors(bits_t *b, unsigned g, unsigned c)
{
    const gr_t *r = &gr[g][c];
    const unsigned s1 = slen[r->scf_compress][0], s2 = slen[r->scf_compress][1];
    uint8_t *l = m->scf_l[c];
    if (r->win_switch && r->block_type == 2u) {
        unsigned sfb = 0;
        if (r->mixed) {
            for (; sfb < 8u; sfb++)
                l[sfb] = (uint8_t) getbits(b, s1);
            sfb = 3;
        }
        for (; sfb < 12u; sfb++)
            for (unsigned w = 0; w < 3u; w++)
                m->scf_s[c][sfb][w] = (uint8_t) getbits(b, (sfb < 6u) ? s1 : s2);
        m->scf_s[c][12][0] = m->scf_s[c][12][1] = m->scf_s[c][12][2] = 0;
    } else {
        static const uint8_t grp[5] = { 0, 6, 11, 16, 21 };
        for (unsigned k = 0; k < 4u; k++) {
            if (g == 1u && (scfsi[c] & (8u >> k)))
                continue;                                   // the first granule's
            for (unsigned sfb = grp[k]; sfb < grp[k + 1]; sfb++)
                l[sfb] = (uint8_t) getbits(b, (k < 2u) ? s1 : s2);
        }
        l[21] = 0;
    }
}

//==========================================================
// HUFFMAN, REQUANTISATION
//==========================================================

// |x|^(4/3) x 2^(a / 4), Q24
static int32_t requant(uint32_t x, int32_t a)
{
    unsigned k = 0;
    uint32_t t;
    if (x <= 256u) {
        t = pow43[x];
    } else {                                                // x = (n + f) 8^k, n < 256
        while ((x >> (3u * k)) >= 256u)
            k++;
        const uint32_t n = x >> (3u * k), f = x & ((1u << (3u * k)) - 1u);
        t = pow43[n] + (((pow43[n + 1u] - pow43[n]) * f) >> (3u * k));
    }
    const int32_t p = mulh((int32_t) t, frac4[a & 3]);    // Q17
    const int32_t sh = 7 + (a >> 2) + 4 * (int32_t) k;
    if (sh >= 0)
        return (sh > 12 || p > (0x7FFFFFFF >> sh)) ? 0x7FFFFFFF : p << sh;
    return (sh <= -31) ? 0 : p >> -sh;
}

// The granule's spectrum of channel c: is[] counts (in xr[]), then the
// requantised lines. Returns the lines past the last non-zero one.
static unsigned spectrum(bits_t *b, unsigned g, unsigned c, uint32_t end)
{
    const gr_t *r = &gr[g][c];
    const uint8_t *wl = sfb_long[sr_idx], *ws = sfb_short[sr_idx];
    int32_t *xr = m->xr[c];
    unsigned r1, r2;
    if (r->win_switch) {
        r1 = 36u;
        r2 = GR_N;
    } else {
        unsigned a = 0, i = 0;
        for (; i <= r->region0 && i < 22u; i++) a += wl[i];
        r1 = a;
        for (; i <= (unsigned) r->region0 + r->region1 + 1u && i < 22u; i++) a += wl[i];
        r2 = a;
    }
    const unsigned bv = 2u * r->big_values;
    if (r1 > bv) r1 = bv;
    if (r2 > bv) r2 = bv;
    // a 32-bit cache, its next bits at the top: at least 25 valid after a refill
    const uint8_t *np = b->p + (b->pos >> 3);
    uint32_t cache = ((((uint32_t) np[0] << 24) | ((uint32_t) np[1] << 16) | ((uint32_t) np[2] << 8) | np[3]) << (b->pos & 7u));
    int sh = (int) (b->pos & 7u) - 8;
    np += 4;
#define REFILL      while (sh >= 0) { cache |= (uint32_t) *np++ << sh; sh -= 8; }
#define FLUSH(n)    do { cache <<= (n); sh += (int) (n); } while (0)
#define BITPOS      ((uint32_t) (np - b->p) * 8u - 24u + (uint32_t) sh)
    unsigned i = 0;
    // big values: pairs
    for (unsigned reg = 0; reg < 3u; reg++) {
        const unsigned lim = (reg == 0u) ? r1 : (reg == 1u) ? r2 : bv;
        const unsigned t = r->table[reg];
        const int16_t *cb = huff_tab + huff_index[t];
        const unsigned lb = huff_linbits[t];
        for (; i < lim; i += 2u) {
            unsigned w = 5;
            int32_t leaf = cb[cache >> 27];
            while (leaf < 0) {
                FLUSH(w);
                w = (unsigned) leaf & 7u;
                leaf = cb[(cache >> (32u - w)) - (leaf >> 3)];
            }
            FLUSH((unsigned) leaf >> 8);
            if (lb) {
                for (unsigned j = 0; j < 2u; j++, leaf >>= 4) {
                    REFILL;
                    int32_t v = leaf & 15;
                    if (v == 15) {
                        v += (int32_t) (cache >> (32u - lb));
                        FLUSH(lb);
                    }
                    if (v) {
                        if ((int32_t) cache < 0) v = -v;
                        FLUSH(1);
                    }
                    xr[i + j] = v;
                }
            } else {
                for (unsigned j = 0; j < 2u; j++, leaf >>= 4) {
                    int32_t v = leaf & 15;
                    if (v) {
                        if ((int32_t) cache < 0) v = -v;
                        FLUSH(1);
                    }
                    xr[i + j] = v;
                }
            }
            REFILL;
        }
    }
    // count1: quadruples
    const uint8_t *c1 = r->count1_table ? huff_tab33 : huff_tab32;
    while (i + 4u <= GR_N) {
        unsigned leaf = c1[cache >> 28];
        if (!(leaf & 8u))
            leaf = c1[(leaf >> 3) + ((cache << 4) >> (32u - (leaf & 3u)))];
        FLUSH(leaf & 7u);
        int32_t v[4];
        for (unsigned j = 0; j < 4u; j++) {
            v[j] = 0;
            if (leaf & (128u >> j)) {
                v[j] = ((int32_t) cache < 0) ? -1 : 1;
                FLUSH(1);
            }
        }
        if (BITPOS > end)                                   // the quadruple overran the part
            break;
        xr[i] = v[0]; xr[i + 1] = v[1]; xr[i + 2] = v[2]; xr[i + 3] = v[3];
        i += 4u;
        REFILL;
    }
#undef REFILL
#undef FLUSH
#undef BITPOS
    unsigned n = i;
    for (; i < GR_N; i++)
        xr[i] = 0;
    b->pos = end;
    while (n && !xr[n - 1u])
        n--;

    // requantise
    const int32_t gg = (int32_t) r->global_gain - 210;
    const int32_t sm = r->scf_scale ? 4 : 2;
    unsigned line = 0, sfb = 0;
    if (!(r->win_switch && r->block_type == 2u) || r->mixed) {
        const unsigned last = (r->win_switch && r->block_type == 2u) ? 8u : 22u;   // mixed: 36 lines long
        for (; sfb < last && line < n; sfb++) {
            const int32_t a = gg - sm * (int32_t) (m->scf_l[c][sfb] + (r->preflag ? pretab[sfb] : 0u));
            for (unsigned k = 0; k < wl[sfb] && line < GR_N; k++, line++) {
                const int32_t x = xr[line];
                if (x) xr[line] = (x < 0) ? -requant((uint32_t) -x, a) : requant((uint32_t) x, a);
            }
        }
        sfb = 3;
    }
    if (r->win_switch && r->block_type == 2u) {
        if (!r->mixed) sfb = 0;
        for (; sfb < 13u && line < n; sfb++)
            for (unsigned w = 0; w < 3u; w++) {
                const int32_t a = gg - 8 * (int32_t) r->sb_gain[w] - sm * (int32_t) m->scf_s[c][sfb][w];
                for (unsigned k = 0; k < ws[sfb] && line < GR_N; k++, line++) {
                    const int32_t x = xr[line];
                    if (x) xr[line] = (x < 0) ? -requant((uint32_t) -x, a) : requant((uint32_t) x, a);
                }
            }
    }
    return n;
}

//==========================================================
// STEREO
//==========================================================

// Intensity stereo from line 'from' of the long bands from sfb0 (the right
// channel's scale factors give the positions)
static void is_long(unsigned from_sfb, unsigned to_sfb)
{
    const uint8_t *wl = sfb_long[sr_idx];
    unsigned line = 0;
    for (unsigned s = 0; s < from_sfb; s++) line += wl[s];
    for (unsigned s = from_sfb; s < to_sfb; s++) {
        const unsigned p = m->scf_l[1][(s < 21u) ? s : 20u];
        for (unsigned k = 0; k < wl[s]; k++, line++)
            if (p < 7u) {
                const int32_t x = m->xr[0][line];
                m->xr[0][line] = mulh(x, is_left[p]) << 1;
                m->xr[1][line] = mulh(x, is_right[p]) << 1;
            }
    }
}

static void stereo(const gr_t *r)
{
    int32_t *l = m->xr[0], *rr = m->xr[1];
    unsigned ms_end = (nz[0] > nz[1]) ? nz[0] : nz[1];
    if (mode_ext & 1u) {                                    // intensity: above the right channel's last line
        const uint8_t *wl = sfb_long[sr_idx], *ws = sfb_short[sr_idx];
        if (r->win_switch && r->block_type == 2u) {
            // short blocks: the right channel's last non-zero band, a window
            unsigned start = 0, sfb0 = 0;
            if (r->mixed) { start = 36; sfb0 = 3; }
            unsigned last_w[3] = { sfb0, sfb0, sfb0 };
            unsigned line = start;
            for (unsigned s = sfb0; s < 13u; s++)
                for (unsigned w = 0; w < 3u; w++)
                    for (unsigned k = 0; k < ws[s]; k++, line++)
                        if (rr[line]) last_w[w] = s + 1u;
            ms_end = 0;
            line = start;
            for (unsigned s = sfb0; s < 13u; s++)
                for (unsigned w = 0; w < 3u; w++) {
                    const unsigned p = m->scf_s[1][(s < 12u) ? s : 11u][w];
                    for (unsigned k = 0; k < ws[s]; k++, line++) {
                        if (s < last_w[w]) {
                            if ((mode_ext & 2u)) {
                                const int32_t a = l[line], b = rr[line];
                                l[line] = mulh(a + b, INV_SQRT2) << 1;
                                rr[line] = mulh(a - b, INV_SQRT2) << 1;
                            }
                        } else if (p < 7u) {
                            const int32_t x = l[line];
                            l[line] = mulh(x, is_left[p]) << 1;
                            rr[line] = mulh(x, is_right[p]) << 1;
                        }
                    }
                }
            ms_end = start;                                 // the mixed long part below
            if (r->mixed && (mode_ext & 2u))
                for (unsigned i = 0; i < 36u; i++) {
                    const int32_t a = l[i], b = rr[i];
                    l[i] = mulh(a + b, INV_SQRT2) << 1;
                    rr[i] = mulh(a - b, INV_SQRT2) << 1;
                }
            nz[0] = nz[1] = GR_N;
            return;
        }
        // long blocks: from the band after the right channel's last line
        unsigned s = 0, line = 0;
        if (nz[1]) {                                        // past the band holding the last line
            while (s < 22u && line + wl[s] < nz[1]) {
                line += wl[s];
                s++;
            }
            line += wl[s];
            s++;
        }
        ms_end = line;
        is_long(s, 22u);
        nz[0] = nz[1] = GR_N;
    }
    if (mode_ext & 2u)
        for (unsigned i = 0; i < ms_end; i++) {
            const int32_t a = l[i], b = rr[i];
            l[i] = mulh(a + b, INV_SQRT2) << 1;
            rr[i] = mulh(a - b, INV_SQRT2) << 1;
        }
    if (nz[0] < ms_end) nz[0] = ms_end;
    if (nz[1] < ms_end) nz[1] = ms_end;
}

//==========================================================
// REORDER, ALIAS REDUCTION, IMDCT
//==========================================================

static void reorder(int32_t *xr, unsigned sfb0)
{
    const uint8_t *ws = sfb_short[sr_idx];
    int32_t tmp[3 * 66];
    unsigned start = (sfb0 == 3u) ? 36u : 0u;
    for (unsigned s = sfb0; s < 13u; s++) {
        const unsigned w = ws[s];
        for (unsigned win = 0; win < 3u; win++)
            for (unsigned j = 0; j < w; j++)
                tmp[3u * j + win] = xr[start + win * w + j];
        memcpy(xr + start, tmp, 3u * w * sizeof(int32_t));
        start += 3u * w;
    }
}

static void alias(int32_t *xr, unsigned sbs)
{
    for (unsigned sb = 1; sb < sbs; sb++)
        for (unsigned i = 0; i < 8u; i++) {
            const int32_t u = xr[18u * sb - 1u - i], d = xr[18u * sb + i];
            xr[18u * sb - 1u - i] = (mulh(u, aa_cs[i]) - mulh(d, aa_ca[i])) << 1;
            xr[18u * sb + i] = (mulh(d, aa_cs[i]) + mulh(u, aa_ca[i])) << 1;
        }
}

static inline int32_t mul31(int32_t a, int32_t c) { return mulh(a, c) << 1; }

// The 9-point DCT-III of the 18-point DCT-IV's halves, in place
static void dct3_9(int32_t *y)
{
    const int32_t *k = dct9_c;
    int32_t s0 = y[0], s2 = y[2], s4 = y[4], s6 = y[6], s8 = y[8], t0, t2, t4;
    t0 = s0 + (s6 >> 1);
    s0 -= s6;
    t4 = mul31(s4 + s2, k[0]);
    t2 = mul31(s8 + s2, k[1]);
    s6 = mul31(s4 - s8, k[2]);
    s4 += s8 - s2;
    s2 = s0 - (s4 >> 1);
    const int32_t y4 = s4 + s0;
    s8 = t0 - t2 + s6;
    s0 = t0 - t4 + t2;
    s4 = t0 + t4 - s6;
    int32_t s1 = y[1], s3 = y[3], s5 = y[5], s7 = y[7];
    s3 = mul31(s3, k[3]);
    t0 = mul31(s5 + s1, k[4]);
    t4 = mul31(s5 - s7, k[5]);
    t2 = mul31(s1 + s7, k[6]);
    s1 = mul31(s1 - s5 - s7, k[3]);
    s5 = t0 - s3 - t2;
    s7 = t4 - s3 - t0;
    s3 = t4 + s3 - t2;
    y[0] = s4 - s7; y[1] = s2 + s1; y[2] = s0 - s3; y[3] = s8 + s5; y[4] = y4;
    y[5] = s8 - s5; y[6] = s0 + s3; y[7] = s2 - s1; y[8] = s4 + s7;
}

// The 18-point DCT-IV y[n] = sum x[k] cos(pi / 72 (2n + 1)(2k + 1)), by two
// 9-point DCT-IIIs and twiddles
static void dct4_18(const int32_t *x, int32_t *y)
{
    int32_t co[9], si[9];
    co[0] = -x[0];
    si[0] = x[17];
    for (unsigned i = 0; i < 4u; i++) {
        si[8u - 2u * i] = x[4u * i + 1u] - x[4u * i + 2u];
        co[1u + 2u * i] = x[4u * i + 1u] + x[4u * i + 2u];
        si[7u - 2u * i] = x[4u * i + 4u] - x[4u * i + 3u];
        co[2u + 2u * i] = -(x[4u * i + 3u] + x[4u * i + 4u]);
    }
    dct3_9(co);
    dct3_9(si);
    for (unsigned i = 0; i < 9u; i++) {
        const int32_t sv = (i & 1u) ? -si[i] : si[i];
        y[9u + i] = -(mul31(co[i], twid9[9u + i]) + mul31(sv, twid9[i]));
        y[8u - i] = -(mul31(co[i], twid9[i]) - mul31(sv, twid9[9u + i]));
    }
}

// One subband: its 18 lines in x[] become its 18 samples, the overlap kept.
// The IMDCT z[i] = sum x[k] cos(pi / 72 (2i + 19)(2k + 1)) is the DCT-IV's
// y[i + 9] (i < 9), -y[26 - i] (i < 27), -y[i - 27]; the short one likewise
// from a 6-point DCT-IV.
static void imdct(int32_t *x, int32_t *ov, unsigned bt, unsigned odd, unsigned silent)
{
    if (silent) {
        for (unsigned i = 0; i < 18u; i++) {
            x[i] = ov[i];
            ov[i] = 0;
        }
    } else {
        if (bt != 2u) {                                     // mapping, window and overlap in one pass
            const int32_t *wn = win_long[(bt == 3u) ? 2u : bt];
            int32_t y[18];
            dct4_18(x, y);
            for (unsigned j = 0; j < 9u; j++) {
                x[j] = ov[j] + mul31(y[j + 9u], wn[j]);
                x[9u + j] = ov[9u + j] - mul31(y[17u - j], wn[9u + j]);
                ov[j] = -mul31(y[8u - j], wn[18u + j]);
                ov[9u + j] = -mul31(y[j], wn[27u + j]);
            }
        } else {
            int32_t z[36];
            memset(z, 0, sizeof(z));
            for (unsigned w = 0; w < 3u; w++) {
                int32_t y[6];
                for (unsigned n = 0; n < 6u; n++) {
                    int32_t a = 0;
                    for (unsigned k = 0; k < 6u; k++)
                        a += mulh(x[w + 3u * k], dct4_6[n][k]);
                    y[n] = a << 1;
                }
                int32_t *zw = z + 6u + 6u * w;
                for (unsigned i = 0; i < 3u; i++)  zw[i] += mul31(y[i + 3u], win_short[i]);
                for (unsigned i = 3; i < 9u; i++)  zw[i] -= mul31(y[8u - i], win_short[i]);
                for (unsigned i = 9; i < 12u; i++) zw[i] -= mul31(y[i - 9u], win_short[i]);
            }
            for (unsigned i = 0; i < 18u; i++) {
                x[i] = z[i] + ov[i];
                ov[i] = z[i + 18u];
            }
        }
    }
    if (odd)
        for (unsigned i = 1; i < 18u; i += 2u)
            x[i] = -x[i];
}

//==========================================================
// SYNTHESIS
//==========================================================

#include "mp3_dct32.h"

// A time slot of channel c's subband samples (src[18 sb]) into its V
// buffer (returned: the newest 64 first). V[64] from the DCT's X:
// X[i + 16] (i < 16), 0, -X[48 - i] (i <= 48), -X[i - 48].
static int32_t *vput(unsigned c, const int32_t *src)
{
    int32_t s[32];
    dct32(src, s);
    const unsigned o = m->voff[c] = (m->voff[c] - 64u) & 1023u;
    int32_t *p = m->v[c] + o, *q = p + 1024;                // o: a multiple of 64, no wrap
    for (unsigned k = 0; k < 16u; k++) {
        p[k] = q[k] = s[16u + k];
        p[32u - k] = q[32u - k] = -s[16u + k];
        p[33u + k] = q[33u + k] = -s[15u - k];
    }
    p[16] = q[16] = 0;
    for (unsigned k = 1; k < 16u; k++)
        p[48u + k] = q[48u + k] = -s[k];
    return p;
}

// A mono time slot -> 32 PCM samples (stride 2): out[j] = sum over i < 8
// of V[128 i + j] D[64 i + j] + V[128 i + 96 + j] D[64 i + 32 + j], every
// tap at a constant offset of two pointers
static void synth(const int32_t *src, int16_t *pcm)
{
    const int32_t *v0 = vput(0u, src), *d = synth_d;
    for (unsigned j = 0; j < 32u; j++, v0++, d++) {
        const int32_t *v1 = v0 + 512;
        int32_t a = mulh(v0[0], d[0]) + mulh(v0[96], d[32]) + mulh(v0[128], d[64]) + mulh(v0[224], d[96])
                  + mulh(v0[256], d[128]) + mulh(v0[352], d[160]) + mulh(v0[384], d[192]) + mulh(v0[480], d[224])
                  + mulh(v1[0], d[256]) + mulh(v1[96], d[288]) + mulh(v1[128], d[320]) + mulh(v1[224], d[352])
                  + mulh(v1[256], d[384]) + mulh(v1[352], d[416]) + mulh(v1[384], d[448]) + mulh(v1[480], d[480]);
        a = (a + 64) >> 7;
        pcm[2u * j] = (int16_t) ((a > 32767) ? 32767 : (a < -32768) ? -32768 : a);
    }
}

// A stereo time slot: the same window for both channels (mp3_win.S)
void mp3_win2(const int32_t *vl, const int32_t *vr, const int32_t *d, int16_t *pcm);

static void synth2(const int32_t *srcl, const int32_t *srcr, int16_t *pcm)
{
    const int32_t *vl = vput(0u, srcl);
    mp3_win2(vl, vput(1u, srcr), synth_d, pcm);
}

//==========================================================
// FRAMES
//==========================================================

static void granule(bits_t *b, unsigned g, const uint32_t *start)
{
    for (unsigned c = 0; c < nch; c++) {
        b->pos = start[c];
        scale_factors(b, g, c);
        nz[c] = spectrum(b, g, c, start[c] + gr[g][c].part23);
    }
    if (nch == 2u && mode == 1u && mode_ext)
        stereo(&gr[g][0]);
    for (unsigned c = 0; c < nch; c++) {
        const gr_t *r = &gr[g][c];
        int32_t *xr = m->xr[c];
        const unsigned short_blk = r->win_switch && r->block_type == 2u;
        unsigned last = nz[c];
        if (short_blk) {
            reorder(xr, r->mixed ? 3u : 0u);
            // the reorder spreads a band's windows over the band: to its end
            const uint8_t *ws = sfb_short[sr_idx];
            unsigned end = r->mixed ? 36u : 0u;
            for (unsigned sfb = r->mixed ? 3u : 0u; sfb < 13u && end < last; sfb++)
                end += 3u * ws[sfb];
            if (last > (r->mixed ? 36u : 0u))
                last = (end > GR_N) ? GR_N : end;
        }
        unsigned sbs = (last + 17u) / 18u;
        if (sbs > 32u) sbs = 32u;
        if (!short_blk)
            alias(xr, (sbs < 32u) ? sbs + 1u : 32u);
        else if (r->mixed)
            alias(xr, 2u);
        const unsigned live = (short_blk || sbs >= 32u) ? ((sbs > 32u) ? 32u : sbs) : sbs + 1u;   // alias spreads one up
        for (unsigned sb = 0; sb < 32u; sb++) {
            const unsigned bt = short_blk ? ((r->mixed && sb < 2u) ? 0u : 2u) : r->block_type;
            imdct(xr + 18u * sb, m->ovl[c] + 18u * sb, bt, sb & 1u, sb >= live);
        }
    }
    for (unsigned ts = 0; ts < 18u; ts++) {                 // the synthesis, time slot by time slot
        if (nch == 2u)
            synth2(m->xr[0] + ts, m->xr[1] + ts, m->pcm + 64u * ts);
        else
            synth(m->xr[0] + ts, m->pcm + 64u * ts);
    }
    if (nch == 1u)
        for (unsigned i = 0; i < GR_N; i++)
            m->pcm[2u * i + 1u] = m->pcm[2u * i];
}

static int frame(sink_t *sk, unsigned *done)
{
    *done = 0;
    for (;;) {
        if (fill(4) < 4u)
            return 0;
        const uint8_t *h = m->in + in_pos;
        if (h[0] == 'T' && h[1] == 'A' && h[2] == 'G')
            return 0;                                       // ID3v1: the end
        const unsigned len = header(h);
        if (!len) {                                         // resync
            in_pos++;
            continue;
        }
        if (fill(len) < len)
            return 0;
        h = m->in + in_pos;
        sr_idx = (h[2] >> 2) & 3u;
        mode = h[3] >> 6;
        mode_ext = (h[3] >> 4) & 3u;
        nch = (mode == 3u) ? 1u : 2u;
        const unsigned crc = !(h[1] & 1u);
        const unsigned side = (nch == 1u) ? 17u : 32u;
        bits_t b = { h + 4u + 2u * crc, 0 };
        const unsigned main_begin = side_info(&b);
        // Xing / Info: no audio
        const uint8_t *x = h + 4u + 2u * crc + side;
        if (!started && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4))) {
            in_pos += len;
            started = 1;
            continue;
        }
        started = 1;
        // the main data onto the reservoir
        const unsigned md_len = len - 4u - 2u * crc - side;
        if (res_len > 511u) {
            memmove(m->res, m->res + res_len - 511u, 511u);
            res_len = 511u;
        }
        const unsigned have = res_len;
        memcpy(m->res + res_len, h + 4u + 2u * crc + side, md_len);
        res_len += md_len;
        memset(m->res + res_len, 0, 4);                     // the bit reader's look-ahead
        in_pos += len;
        if (main_begin > have)
            continue;                                       // its data is in frames we did not see
        bits_t md = { m->res + have - main_begin, 0 };
        uint32_t pos = 0;
        for (unsigned g = 0; g < 2u; g++) {
            uint32_t start[2];
            for (unsigned c = 0; c < nch; c++) {
                start[c] = pos;
                pos += gr[g][c].part23;
            }
            if ((pos + 7u) / 8u > main_begin + md_len)
                return -1;
            granule(&md, g, start);
            uint32_t w = *sk->w;
            for (unsigned i = 0; i < GR_N; i++, w++)
                sk->ring[w & sk->mask] = ((uint32_t) (uint16_t) m->pcm[2u * i + 1u] << 16) | (uint16_t) m->pcm[2u * i];
            *sk->w = w;
            *done += GR_N;
        }
        return 1;
    }
}

int mp3_frame(sink_t *s)
{
    unsigned n;
    const int e = frame(s, &n);
    return (e < 0) ? e : (int) n;
}

//==========================================================
// OPENING: ID3v2, THE FIRST FRAME
//==========================================================

// A text frame's content into dst (ISO-8859-1, UTF-8 or UTF-16 kept to ASCII)
static void id3_text(const uint8_t *p, unsigned n, char *dst, unsigned max)
{
    unsigned k = 0;
    if (!n || !max)
        return;
    const unsigned enc = p[0];
    p++; n--;
    if (enc == 1u || enc == 2u) {                           // UTF-16
        unsigned be = (enc == 2u);
        if (n >= 2u && ((p[0] == 0xFEu && p[1] == 0xFFu) || (p[0] == 0xFFu && p[1] == 0xFEu))) {
            be = (p[0] == 0xFEu);
            p += 2; n -= 2;
        }
        for (unsigned i = 0; i + 1u < n && k + 1u < max; i += 2u) {
            const unsigned ch = be ? ((unsigned) p[i] << 8 | p[i + 1]) : ((unsigned) p[i + 1] << 8 | p[i]);
            if (!ch) break;
            dst[k++] = (ch < 128u) ? (char) ch : '?';
        }
    } else {
        for (unsigned i = 0; i < n && p[i] && k + 1u < max; i++)
            if (p[i] < 128u) dst[k++] = (char) p[i];
            else if (enc == 0u) dst[k++] = '?';
            else if ((p[i] & 0xC0u) == 0xC0u) dst[k++] = '?';  // a UTF-8 sequence: one mark
    }
    dst[k] = 0;
}

int mp3_open(mp3_read_t rd, mp3_skip_t skip, uint32_t size, void *mem, mp3_info_t *info,
             char *title, char *artist, unsigned text_max)
{
    m = (mp3_mem_t *) mem;
    memset(m, 0, sizeof(*m));
    rd_fn = rd;
    in_pos = in_len = in_eof = res_len = started = 0;
    uint32_t offset = 0;
    if (fill(10) >= 10u && !memcmp(m->in, "ID3", 3)) {
        const uint8_t *t = m->in;
        const unsigned ver = t[3];
        uint32_t tag = ((uint32_t) (t[6] & 127u) << 21) | ((uint32_t) (t[7] & 127u) << 14) | ((uint32_t) (t[8] & 127u) << 7) | (t[9] & 127u);
        const uint32_t total = tag + 10u + ((t[5] & 0x10u) ? 10u : 0u);
        in_pos += 10;
        uint32_t left = tag;
        const unsigned hl = (ver == 2u) ? 6u : 10u;
        while (left >= hl && fill(hl) >= hl) {
            const uint8_t *f = m->in + in_pos;
            if (!f[0])
                break;                                      // the padding
            uint32_t n;
            if (ver == 2u)      n = ((uint32_t) f[3] << 16) | ((uint32_t) f[4] << 8) | f[5];
            else if (ver == 4u) n = ((uint32_t) (f[4] & 127u) << 21) | ((uint32_t) (f[5] & 127u) << 14) | ((uint32_t) (f[6] & 127u) << 7) | (f[7] & 127u);
            else                n = ((uint32_t) f[4] << 24) | ((uint32_t) f[5] << 16) | ((uint32_t) f[6] << 8) | f[7];
            const unsigned is_title = (ver == 2u) ? !memcmp(f, "TT2", 3) : !memcmp(f, "TIT2", 4);
            const unsigned is_artist = (ver == 2u) ? !memcmp(f, "TP1", 3) : !memcmp(f, "TPE1", 4);
            in_pos += hl;
            left -= hl;
            if (n > left) n = left;
            if ((is_title || is_artist) && n < 256u && fill(n) >= n) {
                id3_text(m->in + in_pos, n, is_title ? title : artist, text_max);
                in_pos += n;
            } else {                                        // skipped: what the buffer holds, then a seek
                const uint32_t inbuf = (in_len - in_pos < n) ? in_len - in_pos : n;
                in_pos += inbuf;
                if (n > inbuf && skip(n - inbuf))
                    return -1;
            }
            left -= n;
        }
        // the rest of the tag
        if (left) {
            const uint32_t inbuf = (in_len - in_pos < left) ? in_len - in_pos : left;
            in_pos += inbuf;
            if (left > inbuf && skip(left - inbuf))
                return -1;
        }
        offset = total;
    }
    // the first frame: a header followed by another one
    for (unsigned tries = 0; tries < 65536u; tries++) {
        if (fill(4) < 4u)
            return -1;
        const unsigned len = header(m->in + in_pos);
        if (len && fill(len + 4u) >= len + 4u && header(m->in + in_pos + len)) {
            const uint8_t *h = m->in + in_pos;
            info->rate = rates[(h[2] >> 2) & 3u];
            info->channels = ((h[3] >> 6) == 3u) ? 1u : 2u;
            info->bitrate = kbps[h[2] >> 4] * 1000u;
            info->frames = 0;
            const unsigned crc = !(h[1] & 1u), side = (info->channels == 1u) ? 17u : 32u;
            const uint8_t *x = h + 4u + 2u * crc + side;
            if ((!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) && (x[7] & 1u))
                info->frames = (((uint32_t) x[8] << 24) | ((uint32_t) x[9] << 16) | ((uint32_t) x[10] << 8) | x[11]) * MP3_FRAME;
            else if (size > offset + tries)
                info->frames = (uint32_t) (((uint64_t) (size - offset - tries) * 8u * info->rate) / info->bitrate);
            return 0;
        }
        in_pos++;
    }
    return -1;
}

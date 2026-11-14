//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      jpeg.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// JPEG (ITU-T T.81), Huffman-coded, 8 bits: baseline and extended
// sequential (SOF0, SOF1) and progressive (SOF2); one component (grey) or
// three (YCbCr, or RGB by an Adobe marker), any sampling factors up to 4.
// Arithmetic coding, 12-bit samples, CMYK and lossless are not decoded.
//
// A picture larger than the screen decodes at 1/2, 1/4 or 1/8 of its size
// by a smaller IDCT (the 4 x 4, 2 x 2 or 1 x 1 lowest frequencies of each
// block): the largest reduction that keeps it at least as large as the
// screen's fit. A sequential file is decoded a row of MCUs at a time:
// each block's coefficients (Huffman, zig-zag, dequantised), the IDCT into
// the components' rows, then the colour conversion (chroma replicated, the
// JFIF YCbCr equations in libjpeg's fixed point) into RGB rows. A
// progressive one keeps the coefficients the IDCT needs (and which of the
// others are non-zero, for the refinement scans) for every block until
// its last scan. When they do not fit, the chroma goes to luma's reduction,
// then both to a further one; a component kept at 1 x 1 stores its DC term
// alone, its AC scans skipped. When even that does not fit, the picture is
// drawn at 1/8 from its first scan (every component's DC, interleaved),
// the later scans ignored: the DC's refinement bits are lost. A picture
// reduced below its fit this way grows back to it smoothly.
//
// The 8 x 8 IDCT is the accurate integer one (the LLM factorisation,
// 13-bit constants, two passes), the same results as libjpeg's "islow".
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"
#include "decoders.h"

#define CMP_MAX     3u

typedef struct {
    uint8_t  look[512];                     // 9-bit look-ahead: the value
    uint8_t  look_len[512];                 // its length (0: longer)
    int32_t  fast[512];                     // AC: the code and its value bits within 9: value << 8 | run << 4 | length (0: no)
    int32_t  maxcode[18];                   // the largest code of each length (-1: none)
    int32_t  valptr[17];                    // the index of its first value
    int32_t  mincode[17];
    uint8_t  val[256];
} huff_t;

typedef struct {
    uint8_t  id, h, v, tq;                  // sampling factors, quantisation table
    uint8_t  td, ta;                        // the scan's DC / AC tables
    int32_t  dc_pred;
    unsigned bw, bh;                        // blocks across / down (the MCUs' padded size)
    unsigned kc;                            // its IDCT's size: chroma at a lower reduction than luma
    unsigned sx, sy;                        // the output pixels a sample covers, across and down
    unsigned shx, shy;                      // their log2 (powers of two)
    uint8_t *plane;                         // the MCU row's samples
    unsigned pw;                            // its width in samples
    int16_t *coef;                          // progressive: k x k a block
    uint8_t *nzmask;                        // progressive, k > 1: 8 bytes a block
} comp_t;

typedef struct {
    uint16_t q[4][64];                      // natural order
    huff_t   dc[4], ac[4];
    comp_t   c[CMP_MAX];
    unsigned nc, hmax, vmax, mcux, mcuy;
    uint32_t width, height;
    unsigned progressive, adobe_rgb, restart;
    unsigned scale, k;                      // the reduction (1, 2, 4, 8), the IDCT's size (8 / scale)
    unsigned flat;                          // chroma at luma's reduction
    unsigned first_only;                    // progressive: drawn from the first scan
    // the bit reader
    uint32_t bits;
    int      nbits;
    int      marker;                        // a marker hit inside the scan (0: none)
    // progressive state
    unsigned eobrun;
} jpeg_t;

static jpeg_t *J;

static const uint8_t zz[64] = {             // zig-zag -> natural
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

//==========================================================
// MARKERS, TABLES
//==========================================================

static int u16(void)
{
    const int a = img_byte(), b = img_byte();
    return (b < 0) ? -1 : (a << 8) | b;
}

static int build_huff(huff_t *t, const uint8_t *counts, const uint8_t *vals, unsigned nv)
{
    memset(t->look_len, 0, sizeof(t->look_len));
    memcpy(t->val, vals, nv);
    int32_t code = 0;
    unsigned k = 0;
    for (unsigned len = 1; len <= 16u; len++) {
        t->valptr[len] = (int32_t) k;
        t->mincode[len] = code;
        const unsigned n = counts[len - 1u];
        if (!n) {
            t->maxcode[len] = -1;
        } else {
            for (unsigned i = 0; i < n; i++, k++, code++) {
                if (len <= 9u) {                            // the look-ahead entries
                    const unsigned sh = 9u - len;
                    for (unsigned f = 0; f < (1u << sh); f++) {
                        t->look[((unsigned) code << sh) | f] = vals[k];
                        t->look_len[((unsigned) code << sh) | f] = (uint8_t) len;
                    }
                }
            }
            t->maxcode[len] = code - 1;
            if (code - 1 >= (1 << len))
                return -1;
        }
        code <<= 1;
    }
    t->maxcode[17] = 0x7FFFFFFF;
    // the combined AC entries: a code and its value bits within the 9
    for (unsigned p = 0; p < 512u; p++) {
        t->fast[p] = 0;
        const unsigned len = t->look_len[p], rs = t->look[p], r = rs >> 4, sz = rs & 15u;
        if (len && sz && len + sz <= 9u) {
            const unsigned v = (p >> (9u - len - sz)) & ((1u << sz) - 1u);
            const int val = (v < (1u << (sz - 1u))) ? (int) v - (1 << sz) + 1 : (int) v;
            t->fast[p] = (int32_t) ((uint32_t) val << 8) | (int32_t) ((r << 4) | (len + sz));
        }
    }
    return 0;
}

static int read_dht(void)
{
    int len = u16() - 2;
    while (len > 0) {
        const int tc = img_byte();
        uint8_t counts[16], vals[256];
        if (tc < 0 || img_read(counts, 16))
            return IMG_ERR_READ;
        unsigned nv = 0;
        for (unsigned i = 0; i < 16u; i++) nv += counts[i];
        if (nv > 256u || img_read(vals, nv))
            return IMG_ERR_FORMAT;
        huff_t *t = ((tc >> 4) ? J->ac : J->dc) + (tc & 3);
        if ((tc & 15) > 3 || build_huff(t, counts, vals, nv))
            return IMG_ERR_FORMAT;
        len -= 17 + (int) nv;
    }
    return IMG_OK;
}

static int read_dqt(void)
{
    int len = u16() - 2;
    while (len > 0) {
        const int pq = img_byte();
        if (pq < 0) return IMG_ERR_READ;
        uint16_t *q = J->q[pq & 3];
        for (unsigned i = 0; i < 64u; i++)
            q[zz[i]] = (uint16_t) ((pq >> 4) ? u16() : img_byte());
        len -= 65 + ((pq >> 4) ? 64 : 0);
    }
    return IMG_OK;
}

//==========================================================
// BITS, HUFFMAN
//==========================================================

static void bits_reset(void)
{
    J->bits = 0;
    J->nbits = 0;
    J->marker = 0;
}

// At least n (<= 25) bits in the cache; past a marker, zeros
static inline void need(int n)
{
    while (J->nbits < n) {
        int c = 0;
        if (!J->marker) {
            c = img_byte();
            if (c == 0xFF) {
                int d = img_byte();
                while (d == 0xFF) d = img_byte();
                if (d != 0) {                               // a marker: no more data
                    J->marker = (d < 0) ? 0xD9 : d;
                    c = 0;
                }
            } else if (c < 0) {
                J->marker = 0xD9;
                c = 0;
            }
        }
        J->bits |= (uint32_t) c << (24 - J->nbits);
        J->nbits += 8;
    }
}

static inline unsigned getbits(int n)
{
    if (!n) return 0;
    need(n);
    const unsigned v = J->bits >> (32 - n);
    J->bits <<= n;
    J->nbits -= n;
    return v;
}

static inline int extend(unsigned v, int n)
{
    return (n && v < (1u << (n - 1))) ? (int) v - (1 << n) + 1 : (int) v;
}

static int huff_decode(const huff_t *t)
{
    need(16);
    const unsigned pk = J->bits >> 23;
    unsigned len = t->look_len[pk];
    if (len) {
        J->bits <<= len;
        J->nbits -= (int) len;
        return t->look[pk];
    }
    int32_t code = (int32_t) (J->bits >> 22);               // 10 bits on
    len = 10;
    while (len <= 16u && code > t->maxcode[len]) {
        code = (int32_t) (J->bits >> (31u - len));
        len++;
    }
    if (len > 16u)
        return -1;
    J->bits <<= len;
    J->nbits -= (int) len;
    return t->val[t->valptr[len] + code - t->mincode[len]];
}

//==========================================================
// IDCT
//==========================================================

#define CB      13
#define P1      2
#define F0298   2446
#define F0390   3196
#define F0541   4433
#define F0765   6270
#define F0899   7373
#define F1175   9633
#define F1501   12299
#define F1847   15137
#define F1961   16069
#define F2053   16819
#define F2562   20995
#define F3072   25172
#define DESC(x, n)  (((x) + (1 << ((n) - 1))) >> (n))

static inline uint8_t clamp8(int v) { return (uint8_t) ((v < 0) ? 0 : (v > 255) ? 255 : v); }

// The 8 x 8 inverse DCT of in[] (natural order, dequantised) into out
static void idct8(const int32_t *in, uint8_t *out, unsigned stride)
{
    int32_t ws[64];
    for (unsigned c = 0; c < 8u; c++) {                     // columns
        const int32_t *s = in + c;
        int32_t *w = ws + c;
        if (!(s[8] | s[16] | s[24] | s[32] | s[40] | s[48] | s[56])) {
            const int32_t dc = s[0] << P1;
            for (unsigned r = 0; r < 8u; r++) w[8u * r] = dc;
            continue;
        }
        int32_t z2 = s[16], z3 = s[48];
        int32_t z1 = (z2 + z3) * F0541;
        int32_t t2 = z1 + z3 * -F1847, t3 = z1 + z2 * F0765;
        z2 = s[0]; z3 = s[32];
        int32_t t0 = (z2 + z3) << CB, t1 = (z2 - z3) << CB;
        const int32_t a10 = t0 + t3, a13 = t0 - t3, a11 = t1 + t2, a12 = t1 - t2;
        t0 = s[56]; t1 = s[40]; t2 = s[24]; t3 = s[8];
        z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; int32_t z4 = t1 + t3;
        const int32_t z5 = (z3 + z4) * F1175;
        t0 *= F0298; t1 *= F2053; t2 *= F3072; t3 *= F1501;
        z1 *= -F0899; z2 *= -F2562; z3 *= -F1961; z4 *= -F0390;
        z3 += z5; z4 += z5;
        t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;
        w[0] = DESC(a10 + t3, CB - P1); w[56] = DESC(a10 - t3, CB - P1);
        w[8] = DESC(a11 + t2, CB - P1); w[48] = DESC(a11 - t2, CB - P1);
        w[16] = DESC(a12 + t1, CB - P1); w[40] = DESC(a12 - t1, CB - P1);
        w[24] = DESC(a13 + t0, CB - P1); w[32] = DESC(a13 - t0, CB - P1);
    }
    for (unsigned r = 0; r < 8u; r++, out += stride) {     // rows
        const int32_t *w = ws + 8u * r;
        if (!(w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7])) {
            const uint8_t v = clamp8(DESC(w[0], P1 + 3) + 128);
            for (unsigned x = 0; x < 8u; x++) out[x] = v;
            continue;
        }
        int32_t z2 = w[2], z3 = w[6];
        int32_t z1 = (z2 + z3) * F0541;
        int32_t t2 = z1 + z3 * -F1847, t3 = z1 + z2 * F0765;
        int32_t t0 = (w[0] + w[4]) << CB, t1 = (w[0] - w[4]) << CB;
        const int32_t a10 = t0 + t3, a13 = t0 - t3, a11 = t1 + t2, a12 = t1 - t2;
        t0 = w[7]; t1 = w[5]; t2 = w[3]; t3 = w[1];
        z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; int32_t z4 = t1 + t3;
        const int32_t z5 = (z3 + z4) * F1175;
        t0 *= F0298; t1 *= F2053; t2 *= F3072; t3 *= F1501;
        z1 *= -F0899; z2 *= -F2562; z3 *= -F1961; z4 *= -F0390;
        z3 += z5; z4 += z5;
        t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;
        const int n = CB + P1 + 3;
        out[0] = clamp8(DESC(a10 + t3, n) + 128); out[7] = clamp8(DESC(a10 - t3, n) + 128);
        out[1] = clamp8(DESC(a11 + t2, n) + 128); out[6] = clamp8(DESC(a11 - t2, n) + 128);
        out[2] = clamp8(DESC(a12 + t1, n) + 128); out[5] = clamp8(DESC(a12 - t1, n) + 128);
        out[3] = clamp8(DESC(a13 + t0, n) + 128); out[4] = clamp8(DESC(a13 - t0, n) + 128);
    }
}

// The k x k (4 or 2) reduced IDCT: the 8 x 8 one averaged over each of its
// k x k squares (s = 8 / k pixels a side), from the k x k lowest
// frequencies: g(x) = sum C(u) / 2 F(u) cos((2x + 1) u pi / 2k) a(u) per
// dimension, a(u) = the mean of cos((2j + 1 - s) u pi / 16), j < s, the
// average's factor; C(0) = 1 / sqrt(2). The factors in Q14, [x][u].
static const int16_t red4[4][4] = {
    { 5793, 7423, 5352, 2607 }, { 5793, 3075, -5352, -6293 }, { 5793, -3075, -5352, 6293 }, { 5793, -7423, 5352, -2607 }
};
static const int16_t red2[2][2] = { { 5793, 5249 }, { 5793, -5249 } };

static void idct_red(const int32_t *in, uint8_t *out, unsigned stride, unsigned k)
{
    int32_t ws[16];
    const int16_t *m = (k == 4u) ? &red4[0][0] : &red2[0][0];
    for (unsigned u = 0; u < k; u++)                        // columns: y
        for (unsigned y = 0; y < k; y++) {
            int32_t s = 0;
            for (unsigned v = 0; v < k; v++)
                s += in[8u * v + u] * m[y * k + v];
            ws[y * k + u] = s;                              // Q14
        }
    for (unsigned y = 0; y < k; y++, out += stride)         // rows: x
        for (unsigned x = 0; x < k; x++) {
            int64_t s = 0;
            for (unsigned u = 0; u < k; u++)
                s += (int64_t) ws[y * k + u] * m[x * k + u];
            out[x] = clamp8((int) ((s + (1LL << 27)) >> 28) + 128);
        }
}

static void idct(const int32_t *in, uint8_t *out, unsigned stride, unsigned k)
{
    if (k == 8u)
        idct8(in, out, stride);
    else if (k == 1u)
        out[0] = clamp8(DESC(in[0], 3) + 128);
    else
        idct_red(in, out, stride, k);
}

//==========================================================
// COLOUR
//==========================================================

// libjpeg's YCbCr -> RGB, 16-bit fixed point
#define FIX16(x)    ((int32_t) ((x) * 65536.0 + 0.5))
static int32_t cr_r[256], cb_b[256], cr_g[256], cb_g[256];
static uint8_t range_t[768];                // v + 256 -> v clamped to 0 .. 255
#define RANGE(v)    range_t[(v) + 256]

static void colour_init(void)
{
    for (int i = 0; i < 768; i++)
        range_t[i] = (uint8_t) ((i < 256) ? 0 : (i > 511) ? 255 : i - 256);
    for (int i = 0; i < 256; i++) {
        const int32_t x = i - 128;
        cr_r[i] = (91881 * x + 32768) >> 16;                // 1.40200
        cb_b[i] = (116130 * x + 32768) >> 16;               // 1.77200
        cr_g[i] = -46802 * x;                               // 0.71414
        cb_g[i] = -22554 * x + 32768;                       // 0.34414
    }
}

// The MCU row's rows (k x vmax of them) into RGB, then out
static void emit_rows(unsigned mcu_row, uint8_t *rgb)
{
    const unsigned k = J->k, rows = k * J->vmax;
    const unsigned out_w = (unsigned) ((J->width + J->scale - 1u) / J->scale);
    const unsigned out_h = (unsigned) ((J->height + J->scale - 1u) / J->scale);
    for (unsigned r = 0; r < rows; r++) {
        const unsigned y = mcu_row * rows + r;
        if (y >= out_h)
            return;
        if (J->nc == 1u) {
            const uint8_t *p = J->c[0].plane + r * J->c[0].pw;
            for (unsigned x = 0; x < out_w; x++)
                rgb[3u * x] = rgb[3u * x + 1u] = rgb[3u * x + 2u] = p[x];
        } else {
            const comp_t *c0 = &J->c[0], *c1 = &J->c[1], *c2 = &J->c[2];
            const uint8_t *p0 = c0->plane + (r >> c0->shy) * c0->pw;
            const uint8_t *p1 = c1->plane + (r >> c1->shy) * c1->pw;
            const uint8_t *p2 = c2->plane + (r >> c2->shy) * c2->pw;
            const unsigned s0 = c0->shx, s1 = c1->shx, s2 = c2->shx;
            uint8_t *o = rgb;
            if (J->adobe_rgb) {
                for (unsigned x = 0; x < out_w; x++, o += 3) {
                    o[0] = p0[x >> s0]; o[1] = p1[x >> s1]; o[2] = p2[x >> s2];
                }
            } else if (s0 == 0u && s1 == s2) {               // luma 1:1, chroma alike (every usual file)
                for (unsigned x = 0; x < out_w; x++, o += 3) {
                    const int Y = p0[x], Cb = p1[x >> s1], Cr = p2[x >> s1];
                    o[0] = RANGE(Y + cr_r[Cr]);
                    o[1] = RANGE(Y + ((cb_g[Cb] + cr_g[Cr]) >> 16));
                    o[2] = RANGE(Y + cb_b[Cb]);
                }
            } else {
                for (unsigned x = 0; x < out_w; x++, o += 3) {
                    const int Y = p0[x >> s0], Cb = p1[x >> s1], Cr = p2[x >> s2];
                    o[0] = RANGE(Y + cr_r[Cr]);
                    o[1] = RANGE(Y + ((cb_g[Cb] + cr_g[Cr]) >> 16));
                    o[2] = RANGE(Y + cb_b[Cb]);
                }
            }
        }
        out_row(y, rgb);
    }
}

//==========================================================
// SEQUENTIAL
//==========================================================

// The bit cache in locals (registers): at least 25 bits after REFILL; past
// a marker (mk, J->marker's copy), zeros
#define REFILL()                                                            \
    while (nb <= 24) {                                                      \
        int c_ = 0;                                                         \
        if (!mk) {                                                          \
            c_ = img_byte();                                                \
            if (c_ == 0xFF || c_ < 0) {                                     \
                if (c_ == 0xFF) {                                           \
                    int d_ = img_byte();                                    \
                    while (d_ == 0xFF) d_ = img_byte();                     \
                    if (d_ != 0) { mk = (d_ < 0) ? 0xD9 : d_; c_ = 0; }     \
                } else {                                                    \
                    mk = 0xD9;                                              \
                    c_ = 0;                                                 \
                }                                                           \
            }                                                               \
        }                                                                   \
        bits |= (uint32_t) c_ << (24 - nb);                                 \
        nb += 8;                                                            \
    }
#define DROP(n)     do { bits <<= (n); nb -= (int) (n); } while (0)

// A Huffman symbol from the cache (refilled): -1 on a bad code
static inline int huff_local(const huff_t *t, uint32_t *pb, int *pn)
{
    uint32_t bits = *pb;
    const unsigned pk = bits >> 23;
    unsigned len = t->look_len[pk];
    int v;
    if (len) {
        v = t->look[pk];
    } else {
        int32_t code = (int32_t) (bits >> 22);
        len = 10;
        while (len <= 16u && code > t->maxcode[len]) {
            code = (int32_t) (bits >> (31u - len));
            len++;
        }
        if (len > 16u)
            return -1;
        v = t->val[t->valptr[len] + code - t->mincode[len]];
    }
    *pb = bits << len;
    *pn -= (int) len;
    return v;
}

// A block's coefficients into blk (all zero on entry); pos[] the natural
// positions set beyond the DC, *np their count. An AC coefficient whose
// code and value bits fit in 9 comes from one lookup (fast[]).
static int decode_block(comp_t *c, int32_t *blk, unsigned k, uint8_t *pos, unsigned *np)
{
    uint32_t bits = J->bits;
    int nb = J->nbits, mk = J->marker;
    unsigned n_set = 0;
    REFILL();
    const int s = huff_local(&J->dc[c->td], &bits, &nb);
    if (s < 0 || s > 11)
        return IMG_ERR_FORMAT;
    if (s) {
        REFILL();
        const unsigned v = bits >> (32 - s);
        DROP(s);
        c->dc_pred += extend(v, s);
    }
    const uint16_t *q = J->q[c->tq];
    blk[0] = c->dc_pred * q[0];
    const huff_t *ac = &J->ac[c->ta];
    const int32_t *fast = ac->fast;
    for (unsigned i = 1; i < 64u; ) {
        REFILL();
        const int32_t f = fast[bits >> 23];
        int val;
        unsigned r;
        if (f) {
            r = ((unsigned) f >> 4) & 15u;
            val = f >> 8;
            DROP(f & 15);
        } else {
            const int rs = huff_local(ac, &bits, &nb);
            if (rs < 0)
                return IMG_ERR_FORMAT;
            r = (unsigned) rs >> 4;
            const unsigned sz = (unsigned) rs & 15u;
            if (!sz) {
                if (r != 15u) break;                        // EOB
                i += 16u;
                continue;
            }
            REFILL();
            const unsigned v = bits >> (32u - sz);
            DROP(sz);
            val = extend(v, (int) sz);
        }
        i += r;
        if (i > 63u)
            return IMG_ERR_FORMAT;
        const unsigned n = zz[i];
        if (((n & 7u) < k) && ((n >> 3) < k)) {
            blk[n] = val * q[n];
            pos[n_set++] = (uint8_t) n;
        }
        i++;
    }
    J->bits = bits;
    J->nbits = nb;
    J->marker = mk;
    *np = n_set;
    return IMG_OK;
}

static int restart(void)
{
    // the RSTn marker: read by the bit reader, or still to come
    if (!J->marker) {
        int c;
        do c = img_byte(); while (c >= 0 && c != 0xFF);
        do c = img_byte(); while (c == 0xFF);
        if (c < 0xD0 || c > 0xD7)
            return IMG_ERR_FORMAT;
    } else if (J->marker < 0xD0 || J->marker > 0xD7) {
        return IMG_ERR_FORMAT;
    }
    bits_reset();
    for (unsigned i = 0; i < J->nc; i++) J->c[i].dc_pred = 0;
    J->eobrun = 0;
    return IMG_OK;
}

static int sequential(uint8_t *rgb)
{
    int32_t blk[64];
    uint8_t pos[64];
    unsigned np;
    memset(blk, 0, sizeof(blk));
    unsigned todo = J->restart;
    for (unsigned my = 0; my < J->mcuy; my++) {
        for (unsigned mx = 0; mx < J->mcux; mx++) {
            if (J->restart && !todo) {
                if (restart()) return IMG_ERR_FORMAT;
                todo = J->restart;
            }
            for (unsigned ci = 0; ci < J->nc; ci++) {
                comp_t *c = &J->c[ci];
                const unsigned k = c->kc;
                for (unsigned by = 0; by < c->v; by++)
                    for (unsigned bx = 0; bx < c->h; bx++) {
                        if (decode_block(c, blk, k, pos, &np))
                            return IMG_ERR_FORMAT;
                        uint8_t *dst = c->plane + by * k * c->pw + (mx * c->h + bx) * k;
                        if (!np) {                          // DC only: a flat block, the IDCT's exact value
                            const uint8_t v = clamp8(DESC(blk[0], 3) + 128);
                            for (unsigned yy = 0; yy < k; yy++, dst += c->pw)
                                for (unsigned xx = 0; xx < k; xx++) dst[xx] = v;
                        } else {
                            idct(blk, dst, c->pw, k);
                            for (unsigned j = 0; j < np; j++) blk[pos[j]] = 0;
                        }
                        blk[0] = 0;
                    }
            }
            if (J->restart) todo--;
        }
        emit_rows(my, rgb);
    }
    return IMG_OK;
}

//==========================================================
// PROGRESSIVE
//==========================================================

// A block's stored coefficients: k x k (natural order within), and the
// non-zero mask of all 64 (zig-zag order, bit i)
static inline int16_t *pcoef(comp_t *c, unsigned bx, unsigned by)
{
    return c->coef + ((size_t) by * c->bw + bx) * c->kc * c->kc;
}
static inline uint8_t *pmask(comp_t *c, unsigned bx, unsigned by)
{
    return c->nzmask + ((size_t) by * c->bw + bx) * 8u;
}
static inline int kept(unsigned zi, unsigned k)             // the zig-zag index's natural position, -1: not kept
{
    const unsigned n = zz[zi];
    return ((n & 7u) < k && (n >> 3) < k) ? (int) ((n >> 3) * k + (n & 7u)) : -1;
}

static int prog_block(comp_t *c, unsigned bx, unsigned by, unsigned ss, unsigned se, unsigned ah, unsigned al)
{
    int16_t *co = pcoef(c, bx, by);
    if (ss == 0u) {                                         // DC
        if (!ah) {
            const int s = huff_decode(&J->dc[c->td]);
            if (s < 0 || s > 11) return IMG_ERR_FORMAT;
            c->dc_pred += extend(getbits(s), s);
            co[0] = (int16_t) (c->dc_pred * (1 << al));
        } else if (getbits(1)) {
            co[0] = (int16_t) (co[0] | (1 << al));
        }
        return IMG_OK;
    }
    uint8_t *mk = pmask(c, bx, by);
    const huff_t *ac = &J->ac[c->ta];
    if (!ah) {                                              // AC first
        if (J->eobrun) { J->eobrun--; return IMG_OK; }
        for (unsigned i = ss; i <= se; ) {
            const int rs = huff_decode(ac);
            if (rs < 0) return IMG_ERR_FORMAT;
            const unsigned r = (unsigned) rs >> 4, sz = (unsigned) rs & 15u;
            if (!sz) {
                if (r < 15u) {                              // EOBn
                    J->eobrun = (1u << r) - 1u + getbits((int) r);
                    break;
                }
                i += 16u;
                continue;
            }
            i += r;
            if (i > 63u) return IMG_ERR_FORMAT;
            const int v = extend(getbits((int) sz), (int) sz) * (1 << al);
            const int p = kept(i, c->kc);
            if (p >= 0) co[p] = (int16_t) v;
            mk[i >> 3] |= (uint8_t) (1u << (i & 7u));
            i++;
        }
        return IMG_OK;
    }
    // AC refinement: a correction bit for each coefficient already
    // non-zero, new ones of +-1 << al placed among the zero ones
    const int p1 = 1 << al, m1 = -(1 << al);
    unsigned i = ss;
    if (!J->eobrun) {
        for (; i <= se; ) {
            const int rs = huff_decode(ac);
            if (rs < 0) return IMG_ERR_FORMAT;
            int r = rs >> 4;
            const unsigned sz = (unsigned) rs & 15u;
            int v = 0;
            if (sz) {
                if (sz != 1u) return IMG_ERR_FORMAT;
                v = getbits(1) ? p1 : m1;
            } else if (r != 15) {
                J->eobrun = (1u << r) + getbits(r);
                break;
            }
            for (; i <= se; i++) {
                if (mk[i >> 3] & (1u << (i & 7u))) {        // non-zero: its correction
                    if (getbits(1)) {
                        const int p = kept(i, c->kc);
                        if (p >= 0 && (co[p] & p1) == 0)
                            co[p] = (int16_t) (co[p] + ((co[p] >= 0) ? p1 : m1));
                    }
                } else {
                    if (r == 0) {
                        if (v) {
                            const int p = kept(i, c->kc);
                            if (p >= 0) co[p] = (int16_t) v;
                            mk[i >> 3] |= (uint8_t) (1u << (i & 7u));
                        }
                        i++;
                        break;
                    }
                    r--;
                }
            }
        }
    }
    if (J->eobrun) {                                        // the rest: corrections only
        for (; i <= se; i++)
            if (mk[i >> 3] & (1u << (i & 7u)))
                if (getbits(1)) {
                    const int p = kept(i, c->kc);
                    if (p >= 0 && (co[p] & p1) == 0)
                        co[p] = (int16_t) (co[p] + ((co[p] >= 0) ? p1 : m1));
                }
        J->eobrun--;
    }
    return IMG_OK;
}

static int prog_scan(comp_t **sc, unsigned ns, unsigned ss, unsigned se, unsigned ah, unsigned al)
{
    unsigned todo = J->restart;
    J->eobrun = 0;
    if (ns == 1u) {                                         // non-interleaved: the component's own blocks
        comp_t *c = sc[0];
        const unsigned bw = (unsigned) ((((J->width * c->h + J->hmax - 1u) / J->hmax) + 7u) / 8u);
        const unsigned bh = (unsigned) ((((J->height * c->v + J->vmax - 1u) / J->vmax) + 7u) / 8u);
        for (unsigned by = 0; by < bh; by++)
            for (unsigned bx = 0; bx < bw; bx++) {
                if (J->restart && !todo) {
                    if (restart()) return IMG_ERR_FORMAT;
                    todo = J->restart;
                }
                if (prog_block(c, bx, by, ss, se, ah, al)) return IMG_ERR_FORMAT;
                if (J->restart) todo--;
            }
        return IMG_OK;
    }
    for (unsigned my = 0; my < J->mcuy; my++)
        for (unsigned mx = 0; mx < J->mcux; mx++) {
            if (J->restart && !todo) {
                if (restart()) return IMG_ERR_FORMAT;
                todo = J->restart;
            }
            for (unsigned i = 0; i < ns; i++) {
                comp_t *c = sc[i];
                for (unsigned by = 0; by < c->v; by++)
                    for (unsigned bx = 0; bx < c->h; bx++)
                        if (prog_block(c, mx * c->h + bx, my * c->v + by, ss, se, ah, al))
                            return IMG_ERR_FORMAT;
            }
            if (J->restart) todo--;
        }
    return IMG_OK;
}

// A scan's entropy-coded data skipped: up to the next marker other than
// RSTn, which the file's loop then reads
static int skip_scan(void)
{
    for (;;) {
        int c = img_byte();
        if (c < 0) return IMG_ERR_READ;
        if (c != 0xFF) continue;
        do c = img_byte(); while (c == 0xFF);
        if (c < 0) return IMG_ERR_READ;
        if (c == 0 || (c >= 0xD0 && c <= 0xD7)) continue;
        return img_seek(img_pos() - 2u) ? IMG_ERR_READ : IMG_OK;
    }
}

// The picture at 1/8 from the first scan (the DC's first bits of every
// component, interleaved), a row of MCUs at a time
static int first_scan(comp_t **sc, unsigned ns, uint8_t *rgb, unsigned al)
{
    unsigned todo = J->restart;
    for (unsigned my = 0; my < J->mcuy; my++) {
        for (unsigned mx = 0; mx < J->mcux; mx++) {
            if (J->restart && !todo) {
                if (restart()) return IMG_ERR_FORMAT;
                todo = J->restart;
            }
            for (unsigned ci = 0; ci < ns; ci++) {
                comp_t *c = sc[ci];
                const int q = (int) J->q[c->tq][0];
                for (unsigned by = 0; by < c->v; by++)
                    for (unsigned bx = 0; bx < c->h; bx++) {
                        const int s = huff_decode(&J->dc[c->td]);
                        if (s < 0 || s > 11) return IMG_ERR_FORMAT;
                        c->dc_pred += extend(getbits(s), s);
                        c->plane[by * c->pw + mx * c->h + bx] = clamp8(DESC(c->dc_pred * (1 << al) * q, 3) + 128);
                    }
            }
            if (J->restart) todo--;
        }
        emit_rows(my, rgb);
    }
    return IMG_OK;
}

// After the last scan: the IDCT of every block, a row of MCUs at a time
static void prog_output(uint8_t *rgb)
{
    int32_t blk[64];
    for (unsigned my = 0; my < J->mcuy; my++) {
        for (unsigned ci = 0; ci < J->nc; ci++) {
            comp_t *c = &J->c[ci];
            const unsigned k = c->kc;
            const uint16_t *q = J->q[c->tq];
            for (unsigned by = 0; by < c->v; by++)
                for (unsigned bx = 0; bx < c->bw; bx++) {
                    const int16_t *co = pcoef(c, bx, my * c->v + by);
                    memset(blk, 0, sizeof(blk));
                    for (unsigned y = 0; y < k; y++)
                        for (unsigned x = 0; x < k; x++)
                            blk[8u * y + x] = co[y * k + x] * q[8u * y + x];
                    idct(blk, c->plane + by * k * c->pw + bx * k, c->pw, k);
                }
        }
        emit_rows(my, rgb);
    }
}

//==========================================================
// THE FILE
//==========================================================

static int read_sof(unsigned type)
{
    u16();
    const int p = img_byte();
    J->height = (uint32_t) u16();
    J->width = (uint32_t) u16();
    J->nc = (unsigned) img_byte();
    if (p != 8)
        return IMG_ERR_UNSUPP;
    if (!J->width || !J->height || (J->nc != 1u && J->nc != 3u))
        return (J->nc == 4u) ? IMG_ERR_UNSUPP : IMG_ERR_FORMAT;
    J->progressive = (type == 0xC2);
    J->hmax = J->vmax = 1;
    for (unsigned i = 0; i < J->nc; i++) {
        comp_t *c = &J->c[i];
        c->id = (uint8_t) img_byte();
        const int hv = img_byte();
        c->tq = (uint8_t) (img_byte() & 3);
        c->h = (uint8_t) (hv >> 4);
        c->v = (uint8_t) (hv & 15);
        if (!c->h || !c->v || c->h > 4u || c->v > 4u)
            return IMG_ERR_FORMAT;
        if (c->h > J->hmax) J->hmax = c->h;
        if (c->v > J->vmax) J->vmax = c->v;
    }
    for (unsigned i = 0; i < J->nc; i++)
        if (J->hmax % J->c[i].h || J->vmax % J->c[i].v)
            return IMG_ERR_UNSUPP;
    J->mcux = (unsigned) ((J->width + 8u * J->hmax - 1u) / (8u * J->hmax));
    J->mcuy = (unsigned) ((J->height + 8u * J->vmax - 1u) / (8u * J->vmax));
    for (unsigned i = 0; i < J->nc; i++) {
        J->c[i].bw = J->mcux * J->c[i].h;
        J->c[i].bh = J->mcuy * J->c[i].v;
    }
    return IMG_OK;
}

// The reduction: the largest that keeps the picture at least as large as
// its fit on the screen (fit_w x fit_h for the full size)
static unsigned pick_scale(unsigned fit_w, unsigned fit_h)
{
    unsigned s = 8;
    while (s > 1u && ((J->width + s - 1u) / s < fit_w || (J->height + s - 1u) / s < fit_h))
        s /= 2u;
    return s;
}

// Each component's IDCT size: a subsampled component at a lower reduction
// (its samples then cover as many output pixels as luma's, or fewer),
// unless flat
static void comp_sizes(void)
{
    for (unsigned i = 0; i < J->nc; i++) {
        comp_t *c = &J->c[i];
        unsigned f = J->hmax / c->h;
        if (J->vmax / c->v < f) f = J->vmax / c->v;
        if (J->flat) f = 1;
        c->kc = J->k * f;
        if (c->kc > 8u) c->kc = 8u;
        c->sx = (J->hmax * J->k) / (c->h * c->kc);
        c->sy = (J->vmax * J->k) / (c->v * c->kc);
        if (!c->sx) c->sx = 1;
        if (!c->sy) c->sy = 1;
        c->shx = (unsigned) __builtin_ctz(c->sx);
        c->shy = (unsigned) __builtin_ctz(c->sy);
    }
}

static int alloc_planes(void)
{
    for (unsigned i = 0; i < J->nc; i++) {
        comp_t *c = &J->c[i];
        c->pw = c->bw * c->kc;
        c->plane = img_take(c->pw * c->v * c->kc);
        if (!c->plane) return IMG_ERR_MEMORY;
    }
    return IMG_OK;
}

// Whether a progressive file's memory, in the order it is taken (each
// component's coefficients and non-zero masks, its MCU-row plane, the RGB
// row), fits at the current sizes
static int prog_fits(void)
{
    uint32_t sz[3u * CMP_MAX + 1u];
    unsigned n = 0;
    for (unsigned i = 0; i < J->nc; i++) {
        const comp_t *c = &J->c[i];
        sz[n++] = c->bw * c->bh * c->kc * c->kc * 2u;
        sz[n++] = (c->kc > 1u) ? c->bw * c->bh * 8u : 0u;
    }
    for (unsigned i = 0; i < J->nc; i++)
        sz[n++] = J->c[i].bw * J->c[i].kc * J->c[i].v * J->c[i].kc;
    sz[n++] = 3u * ((J->width + J->scale - 1u) / J->scale + 8u);
    return img_fits(sz, n);
}

int jpeg_decode(img_info_t *info)
{
    J = img_take(sizeof(jpeg_t));
    if (!J)
        return IMG_ERR_MEMORY;
    memset(J, 0, sizeof(*J));
    if (img_byte() != 0xFF || img_byte() != 0xD8)
        return IMG_ERR_FORMAT;
    colour_init();
    uint8_t *rgb = 0;
    int got_sof = 0;
    for (;;) {
        int c = img_byte();
        if (c < 0) return IMG_ERR_READ;
        if (c != 0xFF) continue;
        do c = img_byte(); while (c == 0xFF);
        if (c < 0) return IMG_ERR_READ;
        int e = IMG_OK;
        switch (c) {
        case 0xC0: case 0xC1: case 0xC2:
            e = read_sof((unsigned) c);
            if (e) return e;
            got_sof = 1;
            unsigned fit_scale;
            {
                unsigned fw, fh;
                out_fit(J->width, J->height, &fw, &fh);
                J->scale = fit_scale = pick_scale(fw, fh);
            }
            if (J->progressive) {                           // coefficients: reduce until they fit
                for (;;) {
                    J->k = 8u / J->scale;
                    J->flat = 0;
                    comp_sizes();
                    if (prog_fits()) break;
                    J->flat = 1;
                    comp_sizes();
                    if (prog_fits()) break;
                    if (J->scale == 8u) { J->first_only = 1; break; }
                    J->scale *= 2u;
                }
                for (unsigned i = 0; i < J->nc && !J->first_only; i++) {
                    comp_t *cc = &J->c[i];
                    const uint32_t nb = cc->bw * cc->bh;
                    cc->coef = img_take(nb * cc->kc * cc->kc * 2u);
                    if (!cc->coef) return IMG_ERR_MEMORY;
                    memset(cc->coef, 0, nb * cc->kc * cc->kc * 2u);
                    if (cc->kc > 1u) {
                        cc->nzmask = img_take(nb * 8u);
                        if (!cc->nzmask) return IMG_ERR_MEMORY;
                        memset(cc->nzmask, 0, nb * 8u);
                    }
                }
            }
            J->k = 8u / J->scale;
            comp_sizes();
            if (alloc_planes()) return IMG_ERR_MEMORY;
            info->width = J->width;
            info->height = J->height;
            info->out_w = (unsigned) ((J->width + J->scale - 1u) / J->scale);
            info->out_h = (unsigned) ((J->height + J->scale - 1u) / J->scale);
            strcpy(info->format, J->progressive ? "JPEG prog" : "JPEG");
            rgb = img_take(3u * (info->out_w + 8u));
            if (!rgb) return IMG_ERR_MEMORY;
            if ((J->scale > fit_scale) ? out_begin_smooth(info->out_w, info->out_h)
                                       : out_begin(info->out_w, info->out_h))
                return IMG_ERR_FORMAT;
            break;
        case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB:
        case 0xCD: case 0xCE: case 0xCF:
            return IMG_ERR_UNSUPP;                          // lossless, hierarchical, arithmetic
        case 0xC4: e = read_dht(); break;
        case 0xDB: e = read_dqt(); break;
        case 0xDD: u16(); J->restart = (unsigned) u16(); break;
        case 0xEE: {                                        // Adobe: its transform flag
            int len = u16() - 2;
            uint8_t a[12];
            if (len >= 12 && !img_read(a, 12)) {
                if (!memcmp(a, "Adobe", 5)) J->adobe_rgb = (a[11] == 0);
                len -= 12;
            }
            if (img_skip((uint32_t) len)) return IMG_ERR_READ;
            break;
        }
        case 0xDA: {                                        // a scan
            if (!got_sof) return IMG_ERR_FORMAT;
            u16();
            const unsigned ns = (unsigned) img_byte();
            comp_t *sc[CMP_MAX];
            if (!ns || ns > J->nc) return IMG_ERR_FORMAT;
            for (unsigned i = 0; i < ns; i++) {
                const int id = img_byte(), t = img_byte();
                sc[i] = 0;
                for (unsigned j = 0; j < J->nc; j++)
                    if (J->c[j].id == id) sc[i] = &J->c[j];
                if (!sc[i]) return IMG_ERR_FORMAT;
                sc[i]->td = (uint8_t) ((t >> 4) & 3);
                sc[i]->ta = (uint8_t) (t & 3);
            }
            const unsigned ss = (unsigned) img_byte(), se = (unsigned) img_byte();
            const int a = img_byte();
            for (unsigned i = 0; i < J->nc; i++) J->c[i].dc_pred = 0;
            bits_reset();
            if (!J->progressive) {
                if (ns != J->nc) return IMG_ERR_UNSUPP;     // a non-interleaved sequential file
                e = sequential(rgb);
                return e;
            }
            if (se > 63u || ss > se) return IMG_ERR_FORMAT;
            if (J->first_only) {                            // the first scan alone
                if (ss || (a >> 4) || ns != J->nc) return IMG_ERR_MEMORY;
                return first_scan(sc, ns, rgb, (unsigned) a & 15u);
            }
            if (ss && sc[0]->kc == 1u) {                    // an AC scan of a DC-only component
                e = skip_scan();
                if (e) return e;
                break;
            }
            e = prog_scan(sc, ns, ss, se, (unsigned) a >> 4, (unsigned) a & 15u);
            if (e) return e;
            // the next marker: the one the bit reader stopped at is read
            // again by the loop (a seek back over it), else it follows
            if (J->marker) {
                const int at_end = (J->marker == 0xD9);
                J->marker = 0;
                if (at_end && img_pos() >= img_size()) { prog_output(rgb); return IMG_OK; }
                if (img_seek(img_pos() - 2u)) return IMG_ERR_READ;
            }
            break;
        }
        case 0xD9:                                          // the end
            if (J->progressive && got_sof) { prog_output(rgb); return IMG_OK; }
            return IMG_ERR_FORMAT;
        case 0xD8: case 0x01:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
            break;
        default: {                                          // APPn, COM, ...: skipped
            const int len = u16();
            if (len < 2 || img_skip((uint32_t) len - 2u)) return IMG_ERR_READ;
            break;
        }
        }
        if (e) return e;
    }
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      png.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// PNG: the signature, then chunks (length, type, data, CRC): IHDR (size,
// bit depth, colour type, interlace), PLTE, tRNS, then the IDAT chunks, one
// zlib stream (inflate.c) across them: each row a filter byte (none, sub,
// up, average, Paeth) and its bytes. Every colour type and bit depth
// (grey 1-16, RGB 8 and 16, palette 1-8, grey + alpha, RGBA; 16 bits by
// their upper byte); transparency (alpha, or tRNS) blends over the
// screen's black. An Adam7-interlaced file is put together in memory and
// drawn once when it fits there; else it is drawn pass by pass, each pass's
// pixels as blocks the later passes refine (nearest-pixel scaling).
// The CRCs are not checked; the other chunks are skipped.
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"
#include "inflate.h"
#include "decoders.h"

static uint32_t chunk_left;                 // the IDAT bytes left in this chunk
static unsigned idat_end;

static uint32_t be32(const uint8_t *p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

// The zlib stream's next byte, across IDAT chunks
static int idat_byte(void)
{
    while (!chunk_left) {
        if (idat_end) return -1;
        uint8_t h[12];
        if (img_read(h, 12)) return -1;                     // the CRC, the next chunk's length and type
        if (memcmp(h + 8, "IDAT", 4)) { idat_end = 1; return -1; }
        chunk_left = be32(h + 4);
    }
    chunk_left--;
    return img_byte();
}

static inline uint8_t paeth(int a, int b, int c)
{
    const int p = a + b - c;
    int pa = p - a, pb = p - b, pc = p - c;
    if (pa < 0) pa = -pa;
    if (pb < 0) pb = -pb;
    if (pc < 0) pc = -pc;
    return (uint8_t) ((pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c);
}

static int unfilter(uint8_t *row, const uint8_t *prev, uint32_t n, unsigned bpp, unsigned f)
{
    switch (f) {
    case 0: break;
    case 1: for (uint32_t i = bpp; i < n; i++) row[i] = (uint8_t) (row[i] + row[i - bpp]); break;
    case 2: for (uint32_t i = 0; i < n; i++) row[i] = (uint8_t) (row[i] + prev[i]); break;
    case 3:
        for (uint32_t i = 0; i < n; i++)
            row[i] = (uint8_t) (row[i] + (((i >= bpp) ? row[i - bpp] : 0) + prev[i]) / 2);
        break;
    case 4:
        for (uint32_t i = 0; i < n; i++)
            row[i] = (uint8_t) (row[i] + paeth((i >= bpp) ? row[i - bpp] : 0, prev[i], (i >= bpp) ? prev[i - bpp] : 0));
        break;
    default: return -1;
    }
    return 0;
}

typedef struct {
    unsigned depth, type, ch;               // bit depth, colour type, channels
    uint8_t  pal[256][3], pal_a[256];
    unsigned trns;                          // a tRNS colour key (grey or RGB)
    uint16_t key[3];
} fmt_t;

// A row's n pixels (unpacked from raw) into RGB, alpha blended over black
static void to_rgb(const fmt_t *f, const uint8_t *raw, uint32_t n, uint8_t *o)
{
    const unsigned d = f->depth;
    for (uint32_t x = 0; x < n; x++, o += 3) {
        unsigned r, g, b, a = 255;
        if (f->type == 3u || (f->type == 0u && d < 8u)) {   // packed samples
            const uint32_t bit = x * d;
            const unsigned v = (raw[bit >> 3] >> (8u - d - (bit & 7u))) & ((1u << d) - 1u);
            if (f->type == 3u) {
                r = f->pal[v][0]; g = f->pal[v][1]; b = f->pal[v][2]; a = f->pal_a[v];
            } else {
                r = g = b = v * ((d == 1u) ? 255u : (d == 2u) ? 85u : 17u);
                if (f->trns && v == f->key[0]) a = 0;
            }
        } else {
            const unsigned sb = d / 8u;                     // 1 or 2 bytes a sample
            const uint8_t *p = raw + x * f->ch * sb;
            const unsigned s0 = p[0], s1 = (f->ch > 1u) ? p[sb] : 0u, s2 = (f->ch > 2u) ? p[2u * sb] : 0u;
            if (f->type == 0u || f->type == 4u) {
                r = g = b = s0;
                if (f->type == 4u) a = s1;
                else if (f->trns && ((sb == 2u) ? ((unsigned) p[0] << 8 | p[1]) : p[0]) == f->key[0]) a = 0;
            } else {
                r = s0; g = s1; b = s2;
                if (f->type == 6u) a = p[3u * sb];
                else if (f->trns) {
                    const unsigned kr = (sb == 2u) ? ((unsigned) p[0] << 8 | p[1]) : p[0];
                    const unsigned kg = (sb == 2u) ? ((unsigned) p[2] << 8 | p[3]) : p[1];
                    const unsigned kb = (sb == 2u) ? ((unsigned) p[4] << 8 | p[5]) : p[2];
                    if (kr == f->key[0] && kg == f->key[1] && kb == f->key[2]) a = 0;
                }
            }
        }
        if (a != 255u) {                                    // x a / 255, rounded, without a division
            r = r * a + 128u; r = (r + (r >> 8)) >> 8;
            g = g * a + 128u; g = (g + (g >> 8)) >> 8;
            b = b * a + 128u; b = (b + (b >> 8)) >> 8;
        }
        o[0] = (uint8_t) r; o[1] = (uint8_t) g; o[2] = (uint8_t) b;
    }
}

int png_decode(img_info_t *info)
{
    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    uint8_t h[25];
    if (img_read(h, 8) || memcmp(h, sig, 8))
        return IMG_ERR_FORMAT;
    if (img_read(h, 25) || memcmp(h + 4, "IHDR", 4))
        return IMG_ERR_FORMAT;
    const uint32_t w = be32(h + 8), ht = be32(h + 12);
    fmt_t *f = img_take(sizeof(fmt_t));
    if (!f) return IMG_ERR_MEMORY;
    memset(f, 0, sizeof(*f));
    memset(f->pal_a, 255, sizeof(f->pal_a));
    f->depth = h[16];
    f->type = h[17];
    const unsigned interlace = h[20];
    static const uint8_t chans[7] = { 1, 0, 3, 1, 2, 0, 4 };
    if (!w || !ht || w > 32768u || ht > 32768u || f->type > 6u || !chans[f->type] || h[18] || h[19] || interlace > 1u)
        return IMG_ERR_FORMAT;
    f->ch = chans[f->type];
    const unsigned d = f->depth;
    if (!(d == 1u || d == 2u || d == 4u || d == 8u || d == 16u) || (f->type != 0u && f->type != 3u && d < 8u) ||
        (f->type == 3u && d > 8u))
        return IMG_ERR_FORMAT;
    // the chunks up to the first IDAT
    for (;;) {
        uint8_t c[8];
        if (img_read(c, 8)) return IMG_ERR_READ;
        const uint32_t len = be32(c);
        if (!memcmp(c + 4, "IDAT", 4)) { chunk_left = len; break; }
        if (!memcmp(c + 4, "PLTE", 4) && len <= 768u) {
            for (unsigned i = 0; i < len / 3u; i++)
                if (img_read(f->pal[i], 3)) return IMG_ERR_READ;
            if (img_skip(len % 3u + 4u)) return IMG_ERR_READ;
        } else if (!memcmp(c + 4, "tRNS", 4) && len <= 256u) {
            uint8_t t[256];
            if (img_read(t, len) || img_skip(4)) return IMG_ERR_READ;
            if (f->type == 3u) memcpy(f->pal_a, t, len);
            else if (f->type == 0u && len >= 2u) { f->trns = 1; f->key[0] = (uint16_t) (t[0] << 8 | t[1]); }
            else if (f->type == 2u && len >= 6u) {
                f->trns = 1;
                for (unsigned i = 0; i < 3u; i++) f->key[i] = (uint16_t) (t[2u * i] << 8 | t[2u * i + 1u]);
            }
        } else if (!memcmp(c + 4, "IEND", 4)) {
            return IMG_ERR_FORMAT;
        } else if (img_skip(len + 4u)) {
            return IMG_ERR_READ;
        }
    }
    idat_end = 0;
    const unsigned bpp = (f->ch * d + 7u) / 8u;             // the filter's byte distance
    const uint32_t stride = (w * f->ch * d + 7u) / 8u;
    void *zmem = img_take(inflate_mem());
    uint8_t *row = img_take(stride + 1u), *prev = img_take(stride + 1u), *rgb = img_take(3u * w);
    uint8_t *mask = interlace ? img_take(w) : 0;
    if (!zmem || !row || !prev || !rgb || (interlace && !mask))
        return IMG_ERR_MEMORY;
    if (!inflate_begin(idat_byte, zmem))
        return IMG_ERR_FORMAT;
    info->width = w;
    info->height = ht;
    info->out_w = w;
    info->out_h = ht;
    strcpy(info->format, interlace ? "PNG Adam7" : "PNG");
    if (out_begin(w, ht))
        return IMG_ERR_FORMAT;
    if (!interlace) {
        memset(prev, 0, stride + 1u);
        for (uint32_t y = 0; y < ht; y++) {
            if (inflate_read(row, stride + 1u) != (int) (stride + 1u))
                return IMG_ERR_READ;
            if (unfilter(row + 1, prev + 1, stride, bpp, row[0]))
                return IMG_ERR_FORMAT;
            to_rgb(f, row + 1, w, rgb);
            out_row(y, rgb);
            uint8_t *t = row; row = prev; prev = t;
        }
        return IMG_OK;
    }
    // Adam7: each pass's pixel drawn as the block the later passes refine
    static const uint8_t x0[7] = { 0, 4, 0, 2, 0, 1, 0 }, y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const uint8_t dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    static const uint8_t bw[7] = { 8, 4, 4, 2, 2, 1, 1 }, bh[7] = { 8, 8, 4, 4, 2, 2, 1 };
    uint8_t *px = img_take(3u * ((w + 7u) / 8u * 8u));
    if (!px) return IMG_ERR_MEMORY;
    // A picture that fits in the memory left is put together first and drawn
    // once (no coarse stages); a larger one is drawn as its passes come
    uint8_t *whole = img_take(3u * w * ht);
    for (unsigned p = 0; p < 7u; p++) {
        if (x0[p] >= w || y0[p] >= ht) continue;
        const uint32_t pw = (w - x0[p] + dx[p] - 1u) / dx[p], ph = (ht - y0[p] + dy[p] - 1u) / dy[p];
        const uint32_t ps = (pw * f->ch * d + 7u) / 8u;
        memset(prev, 0, ps + 1u);
        for (uint32_t j = 0; j < ph; j++) {
            if (inflate_read(row, ps + 1u) != (int) (ps + 1u))
                return IMG_ERR_READ;
            if (unfilter(row + 1, prev + 1, ps, bpp, row[0]))
                return IMG_ERR_FORMAT;
            to_rgb(f, row + 1, pw, px);
            if (whole) {
                uint8_t *dst = whole + 3u * ((y0[p] + j * dy[p]) * w + x0[p]);
                for (uint32_t i = 0; i < pw; i++, dst += 3u * dx[p])
                    memcpy(dst, px + 3u * i, 3);
                uint8_t *t = row; row = prev; prev = t;
                continue;
            }
            memset(mask, 0, w);
            for (uint32_t i = 0; i < pw; i++) {             // the blocks across
                const uint32_t x = x0[p] + i * dx[p];
                for (unsigned k = 0; k < bw[p] && x + k < w; k++) {
                    memcpy(rgb + 3u * (x + k), px + 3u * i, 3);
                    mask[x + k] = 1;
                }
            }
            const uint32_t y = y0[p] + j * dy[p];
            for (unsigned k = 0; k < bh[p] && y + k < ht; k++)
                out_rect(y + k, 0, w, rgb, mask);
            uint8_t *t = row; row = prev; prev = t;
        }
    }
    if (whole)
        for (uint32_t y = 0; y < ht; y++)
            out_row(y, whole + 3u * w * y);
    return IMG_OK;
}

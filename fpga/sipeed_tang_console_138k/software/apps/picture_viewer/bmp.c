//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      bmp.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// BMP (Windows bitmap): a 14-byte file header (its pixels' offset), an
// information header (the OS/2 12-byte one, or 40 to 124 bytes: the size,
// the bits a pixel, the compression), a palette below 16 bits a pixel, then
// the rows, padded to 4 bytes, bottom-up unless the height is negative.
// Uncompressed 1, 4, 8, 16, 24 and 32 bits a pixel, and BI_BITFIELDS
// masks at 16 and 32; RLE is not decoded. The rows are read up to 32 KB at
// a time, a bottom-up file's from its end.
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"
#include "decoders.h"

#define ROWS_N  32768u                      // the rows read at a time, at most (bytes)

static uint32_t le32(const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint32_t le16(const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); }

// A mask's shift and width
static void mask_shape(uint32_t m, unsigned *sh, unsigned *bits)
{
    unsigned s = 0, b = 0;
    if (m) {
        while (!(m & 1u)) { m >>= 1; s++; }
        while (m & 1u) { m >>= 1; b++; }
    }
    *sh = s;
    *bits = b;
}

static inline unsigned field(uint32_t px, unsigned sh, unsigned bits)
{
    if (!bits)
        return 0;
    const uint32_t v = (px >> sh) & ((1u << bits) - 1u);
    return (bits >= 8u) ? v >> (bits - 8u) : (v * 255u + ((1u << bits) - 1u) / 2u) / ((1u << bits) - 1u);
}

int bmp_decode(img_info_t *info)
{
    uint8_t h[124 + 14];
    if (img_read(h, 18) || h[0] != 'B' || h[1] != 'M')
        return IMG_ERR_FORMAT;
    const uint32_t data = le32(h + 10), hs = le32(h + 14);
    if (hs != 12u && (hs < 40u || hs > 124u))
        return IMG_ERR_FORMAT;
    if (img_read(h + 18, hs - 4u))
        return IMG_ERR_READ;
    int32_t w, ht;
    unsigned bpp, comp = 0, ncol = 0, pal_entry = 4;
    if (hs == 12u) {
        w = (int32_t) le16(h + 18);
        ht = (int32_t) le16(h + 20);
        bpp = le16(h + 24);
        pal_entry = 3;
    } else {
        w = (int32_t) le32(h + 18);
        ht = (int32_t) le32(h + 22);
        bpp = le16(h + 28);
        comp = le32(h + 30);
        ncol = le32(h + 46);
    }
    const int top_down = (ht < 0);
    if (top_down) ht = -ht;
    if (w <= 0 || ht <= 0 || w > 32768 || ht > 32768)
        return IMG_ERR_FORMAT;
    uint32_t mr = 0x00FF0000u, mg = 0x0000FF00u, mb = 0x000000FFu;
    if (bpp == 16u) { mr = 0x7C00u; mg = 0x03E0u; mb = 0x001Fu; }
    if (comp == 3u || comp == 6u) {                         // BI_BITFIELDS: after a 40-byte header, or in it
        uint8_t m[12];
        if (hs >= 52u) memcpy(m, h + 54, 12);
        else if (img_read(m, 12)) return IMG_ERR_READ;
        mr = le32(m); mg = le32(m + 4); mb = le32(m + 8);
    } else if (comp != 0u) {
        return IMG_ERR_UNSUPP;
    }
    if (bpp != 1u && bpp != 4u && bpp != 8u && bpp != 16u && bpp != 24u && bpp != 32u)
        return IMG_ERR_UNSUPP;
    if ((bpp == 16u || bpp == 32u) == 0 && (comp == 3u || comp == 6u))
        return IMG_ERR_FORMAT;
    // the palette
    uint8_t *pal = 0;
    if (bpp <= 8u) {
        const unsigned n = (ncol && ncol <= (1u << bpp)) ? ncol : (1u << bpp);
        pal = img_take(3u * 256u);
        if (!pal)
            return IMG_ERR_MEMORY;
        memset(pal, 0, 3u * 256u);
        if (img_seek(14u + hs + ((hs == 40u && (comp == 3u || comp == 6u)) ? 12u : 0u)))
            return IMG_ERR_READ;
        for (unsigned i = 0; i < n; i++) {
            uint8_t e[4];
            if (img_read(e, pal_entry))
                return IMG_ERR_READ;
            pal[3u * i] = e[2]; pal[3u * i + 1u] = e[1]; pal[3u * i + 2u] = e[0];
        }
    }
    unsigned rs, rb, gs, gb, bs, bb;
    mask_shape(mr, &rs, &rb);
    mask_shape(mg, &gs, &gb);
    mask_shape(mb, &bs, &bb);

    const uint32_t stride = (((uint32_t) w * bpp + 31u) / 32u) * 4u;
    uint8_t *rgb = img_take(3u * (uint32_t) w);
    unsigned k = (stride <= ROWS_N) ? (unsigned) (ROWS_N / stride) : 1u;   // rows a read
    if (k > (unsigned) ht) k = (unsigned) ht;
    uint8_t *rows = img_take(k * stride);
    while (!rows && k > 1u)
        rows = img_take((k /= 2u) * stride);
    if (!rows || !rgb)
        return IMG_ERR_MEMORY;
    info->width = (uint32_t) w;
    info->height = (uint32_t) ht;
    info->out_w = (unsigned) w;
    info->out_h = (unsigned) ht;
    strcpy(info->format, "BMP");
    if (out_begin((unsigned) w, (unsigned) ht))
        return IMG_ERR_FORMAT;
    unsigned lo = 1, hi = 0;                                // the file rows in rows[]
    for (unsigned y = 0; y < (unsigned) ht; y++) {
        const unsigned fy = top_down ? y : (unsigned) ht - 1u - y;
        if (fy < lo || fy > hi) {
            if (top_down) {
                lo = fy;
                hi = (fy + k <= (unsigned) ht) ? fy + k - 1u : (unsigned) ht - 1u;
            } else {
                hi = fy;
                lo = (fy + 1u >= k) ? fy + 1u - k : 0u;
            }
            if (img_seek(data + lo * stride) || img_read(rows, (hi - lo + 1u) * stride))
                return IMG_ERR_READ;
        }
        const uint8_t *row = rows + (fy - lo) * stride;
        uint8_t *o = rgb;
        for (unsigned x = 0; x < (unsigned) w; x++, o += 3) {
            if (bpp <= 8u) {
                const unsigned bit = x * bpp;
                const unsigned v = (row[bit >> 3] >> (8u - bpp - (bit & 7u))) & ((1u << bpp) - 1u);
                memcpy(o, pal + 3u * v, 3);
            } else if (bpp == 24u) {
                o[0] = row[3u * x + 2u]; o[1] = row[3u * x + 1u]; o[2] = row[3u * x];
            } else {
                const uint32_t px = (bpp == 16u) ? le16(row + 2u * x) : le32(row + 4u * x);
                o[0] = (uint8_t) field(px, rs, rb);
                o[1] = (uint8_t) field(px, gs, gb);
                o[2] = (uint8_t) field(px, bs, bb);
            }
        }
        out_row(y, rgb);
    }
    return IMG_OK;
}

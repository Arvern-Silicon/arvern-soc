//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      qoi.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// QOI ("Quite OK Image"): a 14-byte header ("qoif", width and height big-
// endian, channels, colour space), then the pixels as chunks: a full RGB
// or RGBA value, an index into the 64 last colours (hashed), a small or a
// luma-based difference from the previous pixel, or a run of it. Alpha is
// blended over the screen's black.
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"
#include "decoders.h"

static uint32_t be32(const uint8_t *p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

int qoi_decode(img_info_t *info)
{
    uint8_t h[14];
    if (img_read(h, 14) || memcmp(h, "qoif", 4))
        return IMG_ERR_FORMAT;
    const uint32_t w = be32(h + 4), ht = be32(h + 8);
    if (!w || !ht || w > 32768u || ht > 32768u)
        return IMG_ERR_FORMAT;
    uint8_t *rgb = img_take(3u * w);
    if (!rgb)
        return IMG_ERR_MEMORY;
    info->width = w;
    info->height = ht;
    info->out_w = w;
    info->out_h = ht;
    strcpy(info->format, "QOI");
    if (out_begin(w, ht))
        return IMG_ERR_FORMAT;
    uint8_t idx[64][4];
    memset(idx, 0, sizeof(idx));
    uint8_t r = 0, g = 0, b = 0, a = 255;
    unsigned run = 0;
    for (uint32_t y = 0; y < ht; y++) {
        uint8_t *o = rgb;
        for (uint32_t x = 0; x < w; x++, o += 3) {
            if (run) {
                run--;
            } else {
                const int c = img_byte();
                if (c < 0)
                    return IMG_ERR_READ;
                if (c == 0xFE) {
                    const int c1 = img_byte(), c2 = img_byte(), c3 = img_byte();
                    if (c3 < 0) return IMG_ERR_READ;
                    r = (uint8_t) c1; g = (uint8_t) c2; b = (uint8_t) c3;
                } else if (c == 0xFF) {
                    const int c1 = img_byte(), c2 = img_byte(), c3 = img_byte(), c4 = img_byte();
                    if (c4 < 0) return IMG_ERR_READ;
                    r = (uint8_t) c1; g = (uint8_t) c2; b = (uint8_t) c3; a = (uint8_t) c4;
                } else if ((c >> 6) == 0) {                 // QOI_OP_INDEX
                    r = idx[c][0]; g = idx[c][1]; b = idx[c][2]; a = idx[c][3];
                } else if ((c >> 6) == 1) {                 // QOI_OP_DIFF
                    r = (uint8_t) (r + ((c >> 4) & 3) - 2);
                    g = (uint8_t) (g + ((c >> 2) & 3) - 2);
                    b = (uint8_t) (b + (c & 3) - 2);
                } else if ((c >> 6) == 2) {                 // QOI_OP_LUMA
                    const int c1 = img_byte();
                    if (c1 < 0) return IMG_ERR_READ;
                    const int dg = (c & 63) - 32;
                    r = (uint8_t) (r + dg - 8 + ((c1 >> 4) & 15));
                    g = (uint8_t) (g + dg);
                    b = (uint8_t) (b + dg - 8 + (c1 & 15));
                } else {                                    // QOI_OP_RUN
                    run = (unsigned) (c & 63);
                }
                const unsigned hsh = (r * 3u + g * 5u + b * 7u + a * 11u) & 63u;
                idx[hsh][0] = r; idx[hsh][1] = g; idx[hsh][2] = b; idx[hsh][3] = a;
            }
            if (a == 255u) {
                o[0] = r; o[1] = g; o[2] = b;
            } else {
                unsigned v;                                 // x a / 255, rounded, without a division
                v = r * a + 128u; o[0] = (uint8_t) ((v + (v >> 8)) >> 8);
                v = g * a + 128u; o[1] = (uint8_t) ((v + (v >> 8)) >> 8);
                v = b * a + 128u; o[2] = (uint8_t) ((v + (v >> 8)) >> 8);
            }
        }
        out_row(y, rgb);
    }
    return IMG_OK;
}

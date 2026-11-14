//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gif.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// GIF (87a, 89a): the logical screen (its size, a global palette), then
// blocks: extensions (the graphic control one: the frame's delay, its
// disposal, a transparent colour), images (a rectangle of the screen, a
// local palette, interlaced or not, LZW-coded in sub-blocks), the trailer.
// Each frame is drawn onto the screen as it decodes, its transparent pixels
// left as they were (nearest-pixel scaling); a frame disposed "to the
// background" is cleared to black before the next one, "to the previous"
// is kept as drawn. After the last frame the animation starts again, on a
// screen cleared to black unless the first frame covers it all (no
// transparent colour): a frame takes long to draw enlarged, a clear would
// show as a black flash.
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"
#include "decoders.h"

#define CODES   4096u

typedef struct {
    uint32_t sw, sh;                        // the logical screen
    uint8_t  gpal[256][3], lpal[256][3];
    unsigned gpal_n;
    uint32_t first;                         // the first block after the global palette (the loop)
    unsigned dispose, dx, dy, dw, dh;       // the last frame's disposal and rectangle
    unsigned frames, single;
    unsigned first_covers;                  // the first frame covers the screen, no transparent colour
    // LZW
    uint16_t prefix[CODES];
    uint8_t  suffix[CODES], first_ch[CODES], stack[CODES];
    unsigned sub_left;                      // the data sub-block's bytes left
    uint32_t bits;
    unsigned nbits;
    uint8_t *idx, *rgb, *mask;              // a row: indices, colours, which are drawn
} gif_t;

static gif_t *G;


static int skip_subblocks(void)
{
    for (;;) {
        const int n = img_byte();
        if (n < 0) return -1;
        if (!n) return 0;
        if (img_skip((uint32_t) n)) return -1;
    }
}

// The image data's next byte, across its sub-blocks (-1 at the end)
static int data_byte(void)
{
    while (!G->sub_left) {
        const int n = img_byte();
        if (n <= 0) return -1;
        G->sub_left = (unsigned) n;
    }
    G->sub_left--;
    return img_byte();
}

// A frame: its rectangle (fx, fy, fw x fh), interlaced or not, palette pal,
// transparent index tr (-1: none)
static int frame_data(unsigned fx, unsigned fy, unsigned fw, unsigned fh, unsigned interlace,
                      uint8_t (*pal)[3], int tr)
{
    const int min = img_byte();
    if (min < 2 || min > 11)
        return IMG_ERR_FORMAT;
    const unsigned clear = 1u << min, eoi = clear + 1u;
    unsigned size = (unsigned) min + 1u, next = clear + 2u;
    int prev = -1;
    G->sub_left = 0;
    G->bits = 0;
    G->nbits = 0;
    for (unsigned i = 0; i < clear; i++) {
        G->suffix[i] = (uint8_t) i;
        G->first_ch[i] = (uint8_t) i;
        G->prefix[i] = 0xFFFF;
    }
    unsigned x = 0, row = 0, pass = 0, y = 0;
    static const uint8_t istart[4] = { 0, 4, 2, 1 }, istep[4] = { 8, 8, 4, 2 };
    const unsigned w = (fw > G->sw) ? (unsigned) G->sw : fw;
    for (;;) {
        while (G->nbits < size) {
            const int c = data_byte();
            if (c < 0) goto done;                           // a truncated frame: what came
            G->bits |= (uint32_t) c << G->nbits;
            G->nbits += 8;
        }
        const unsigned code = G->bits & ((1u << size) - 1u);
        G->bits >>= size;
        G->nbits -= size;
        if (code == clear) {
            size = (unsigned) min + 1u;
            next = clear + 2u;
            prev = -1;
            continue;
        }
        if (code == eoi)
            break;
        unsigned sp = 0, cur;
        if (prev < 0) {
            if (code >= clear) return IMG_ERR_FORMAT;
            G->stack[sp++] = (uint8_t) code;
        } else {
            if (code < next) {
                cur = code;
            } else if (code == next) {                      // the string being defined
                G->stack[sp++] = G->first_ch[prev];
                cur = (unsigned) prev;
            } else {
                return IMG_ERR_FORMAT;
            }
            while (cur >= clear) {
                G->stack[sp++] = G->suffix[cur];
                cur = G->prefix[cur];
            }
            G->stack[sp++] = (uint8_t) cur;
            if (next < CODES) {
                G->prefix[next] = (uint16_t) prev;
                G->suffix[next] = (code < next) ? G->first_ch[code] : G->first_ch[prev];
                G->first_ch[next] = G->first_ch[prev];
                next++;
                if (next == (1u << size) && size < 12u)
                    size++;
            }
        }
        prev = (int) code;
        while (sp) {                                        // the string, onto the rows
            const uint8_t v = G->stack[--sp];
            if (x < w) G->idx[x] = v;
            if (++x == fw) {
                if (y < fh) {
                    for (unsigned i = 0; i < w; i++) {
                        const unsigned k = G->idx[i];
                        G->rgb[3u * i] = pal[k][0]; G->rgb[3u * i + 1u] = pal[k][1]; G->rgb[3u * i + 2u] = pal[k][2];
                        G->mask[i] = (tr < 0 || k != (unsigned) tr);
                    }
                    out_rect(fy + y, fx, w, G->rgb, G->mask);
                }
                x = 0;
                row++;
                if (!interlace) {
                    y = row;
                } else {
                    y += istep[pass];
                    while (y >= fh && pass < 3u) {
                        pass++;
                        y = istart[pass];
                    }
                }
                if (row >= fh) goto done;
            }
        }
    }
done:
    // the rest of the data
    while (G->sub_left) { G->sub_left--; if (img_byte() < 0) break; }
    return skip_subblocks() ? IMG_ERR_READ : IMG_OK;
}

// The blocks up to the next image, drawn; at the trailer, the first one
// again (once one was drawn)
static int next_frame(img_info_t *info, unsigned *delay_ms)
{
    int tr = -1;
    unsigned disposal = 0, delay = 0;
    for (unsigned loops = 0; ; ) {
        const int b = img_byte();
        if (b == 0x21) {                                    // an extension
            const int label = img_byte();
            if (label == 0xF9) {
                uint8_t g[6];
                if (img_read(g, 6)) return IMG_ERR_READ;
                disposal = (g[1] >> 2) & 7u;
                delay = (unsigned) (g[2] | (g[3] << 8));
                tr = (g[1] & 1u) ? g[4] : -1;
                if (g[5] && skip_subblocks()) return IMG_ERR_READ;
            } else if (label < 0 || skip_subblocks()) {
                return IMG_ERR_READ;
            }
        } else if (b == 0x2C) {                             // an image
            uint8_t d[9];
            if (img_read(d, 9)) return IMG_ERR_READ;
            const unsigned fx = (unsigned) (d[0] | d[1] << 8), fy = (unsigned) (d[2] | d[3] << 8);
            const unsigned fw = (unsigned) (d[4] | d[5] << 8), fh = (unsigned) (d[6] | d[7] << 8);
            uint8_t (*pal)[3] = G->gpal;
            if (d[8] & 0x80u) {
                const unsigned n = 2u << (d[8] & 7u);
                if (img_read(&G->lpal[0][0], 3u * n)) return IMG_ERR_READ;
                pal = G->lpal;
            }
            // the previous frame's disposal, then this one
            if (G->frames && G->dispose == 2u)
                out_fill(G->dx, G->dy, G->dw, G->dh, 0);
            const int e = frame_data(fx, fy, fw, fh, (d[8] >> 6) & 1u, pal, tr);
            if (e) return e;
            if (!G->frames)
                G->first_covers = !fx && !fy && fw == G->sw && fh == G->sh && tr < 0;
            G->dispose = disposal;
            G->dx = fx; G->dy = fy; G->dw = fw; G->dh = fh;
            G->frames++;
            info->frames = G->frames;
            *delay_ms = (delay < 2u) ? 100u : delay * 10u;
            return IMG_OK;
        } else if (b == 0x3B || b < 0) {                    // the trailer: again from the start
            if (!G->frames || ++loops > 1u) return IMG_ERR_FORMAT;
            if (G->single) { *delay_ms = 0; return IMG_OK; }
            if (img_seek(G->first)) return IMG_ERR_READ;
            if (!G->first_covers)                       // else the first frame redraws it all
                out_fill(0, 0, G->sw, G->sh, 0);
            G->dispose = 0;
        } else {
            return IMG_ERR_FORMAT;
        }
    }
}

int gif_open(img_info_t *info, unsigned *delay_ms)
{
    uint8_t h[13];
    if (img_read(h, 13) || memcmp(h, "GIF8", 4) || (h[4] != '7' && h[4] != '9') || h[5] != 'a')
        return IMG_ERR_FORMAT;
    G = img_take(sizeof(gif_t));
    if (!G) return IMG_ERR_MEMORY;
    memset(G, 0, sizeof(*G));
    G->sw = (uint32_t) (h[6] | h[7] << 8);
    G->sh = (uint32_t) (h[8] | h[9] << 8);
    if (!G->sw || !G->sh)
        return IMG_ERR_FORMAT;
    if (h[10] & 0x80u) {
        G->gpal_n = 2u << (h[10] & 7u);
        if (img_read(&G->gpal[0][0], 3u * G->gpal_n)) return IMG_ERR_READ;
    }
    G->first = img_pos();
    G->idx = img_take(G->sw);
    G->rgb = img_take(3u * G->sw);
    G->mask = img_take(G->sw);
    if (!G->idx || !G->rgb || !G->mask)
        return IMG_ERR_MEMORY;
    info->width = G->sw;
    info->height = G->sh;
    info->out_w = G->sw;
    info->out_h = G->sh;
    strcpy(info->format, "GIF");
    if (out_begin(G->sw, G->sh))
        return IMG_ERR_FORMAT;
    out_fill(0, 0, G->sw, G->sh, 0);
    const int e = next_frame(info, delay_ms);
    if (e) return e;
    // a still: one image before the trailer
    const uint32_t at = img_pos();
    int b;
    do {                                                    // look for another image
        b = img_byte();
        if (b == 0x21) { if (img_byte() < 0 || skip_subblocks()) break; }
    } while (b == 0x21);
    G->single = (b != 0x2C);
    if (img_seek(at)) return IMG_ERR_READ;
    if (G->single) *delay_ms = 0;
    if (!G->single) strcpy(info->format, "GIF anim");
    return IMG_OK;
}

int gif_next(img_info_t *info, unsigned *delay_ms)
{
    if (G->single) {
        *delay_ms = 0;
        return IMG_OK;
    }
    return next_frame(info, delay_ms);
}

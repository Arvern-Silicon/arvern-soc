/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      vl_tang.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Omnispeak's video back-end (VL_Backend) on Belenos, at 4 bits per pixel,
 * the 16 EGA colours through the colour table.
 *
 *   surfaces   in the VRAM heap, 4 bpp, lines a multiple of 8 pixels: the
 *              screen (336 x 224, the view and its scroll margin), the tile
 *              buffer, and the game's others while they exist
 *   GPU        copies between surfaces, fills, the cached tiles and sprites
 *              (gfxcache.c): a masked one is its 1-bit mask expanded in
 *              colour 0 (the drawn pixels cleared), then its pixels ORed in,
 *              what the EGA latches did; the white flash of a hit sprite is
 *              the mask expanded in white
 *   staging    the 1-bit pictures (fonts, the intro's bitmaps) and the
 *              planar ones (the 8 x 8 tiles, the pictures), plain or masked:
 *              converted by the CPU into the staging ring, a large one in
 *              bands, drawn from there by the GPU (an expansion, a copy; a
 *              masked one its opaque pixels cleared, then ORed in) without
 *              waiting for it
 *   pictures   a planar picture larger than the ring: converted once into
 *              the picture cache (gfxcache.c) when it has room, drawn from
 *              there
 *   CPU        the plane-masked planar and 1-bit draws, after waiting for
 *              the GPU; a plane-masked fill is two GPU fills (AND, then
 *              OR), up to 4 pixels the CPU's
 *   present    the 320 x 200 window at the scroll position into the
 *              displayed frame (gfx_present)
 *----------------------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include "id_ca.h"
#include "id_vl.h"
#include "id_vl_private.h"
#include "id_vh.h"
#include "id_sd.h"
#include "id_in.h"
#include "id_us.h"
#include "tang.h"
#include "belenos.h"
#include "tang_video.h"

extern blns_t arv_blns;                         /* board.c: blns_vram_ba for the 1-bpp masks */

typedef struct {
    uint8_t  *mem;                              /* CPU view                     */
    uint32_t  pa;                               /* pixel address                */
    uint16_t  w, h, pitch;                      /* pitch in pixels              */
} surf_t;

static inline unsigned px_get(const surf_t *s, int x, int y)
{
    const uint32_t i = (uint32_t) y * s->pitch + (uint32_t) x;
    return (s->mem[i >> 1] >> ((i & 1u) * 4u)) & 15u;
}

static inline void px_put(surf_t *s, int x, int y, unsigned v)
{
    const uint32_t i = (uint32_t) y * s->pitch + (uint32_t) x;
    uint8_t *b = &s->mem[i >> 1];
    *b = (i & 1u) ? (uint8_t) ((*b & 0x0Fu) | (v << 4)) : (uint8_t) ((*b & 0xF0u) | v);
}

/* The part of a w x h rectangle at (x, y) inside the surface: the skipped
 * columns and lines of the source in *sx, *sy; false when nothing is left */
static bool clip(const surf_t *s, int *x, int *y, int *w, int *h, int *sx, int *sy)
{
    *sx = *sy = 0;
    if (*x < 0) { *sx = -*x; *w += *x; *x = 0; }
    if (*y < 0) { *sy = -*y; *h += *y; *y = 0; }
    if (*x + *w > s->w) *w = s->w - *x;
    if (*y + *h > s->h) *h = s->h - *y;
    return *w > 0 && *h > 0;
}

/*------------------------------- surfaces -----------------------------------*/

static void TANG_SetVideoMode(int mode) { (void) mode; }

static void *TANG_CreateSurface(int w, int h, VL_SurfaceUsage usage)
{
    surf_t *s = (surf_t *) malloc(sizeof(surf_t));
    (void) usage;
    if (!s)
        Quit("VL_CreateSurface: out of SRAM");
    s->w = (uint16_t) w;
    s->h = (uint16_t) h;
    s->pitch = (uint16_t) ((w + 7) & ~7);
    s->mem = (uint8_t *) vheap_alloc((uint32_t) s->pitch * (uint32_t) h / 2u);
    if (!s->mem) {
        arv_mem_report("surface");
        Quit("VL_CreateSurface: out of VRAM");
    }
    s->pa = arv_pa4(s->mem);
    return s;
}

static void TANG_DestroySurface(void *surface)
{
    surf_t *s = (surf_t *) surface;
    if (!s)
        return;
    vheap_free(s->mem);
    free(s);
}

static long TANG_GetSurfaceMemUse(void *surface)
{
    const surf_t *s = (const surf_t *) surface;
    return (long) s->pitch * s->h / 2;
}

static void TANG_GetSurfaceDimensions(void *surface, int *w, int *h)
{
    const surf_t *s = (const surf_t *) surface;
    if (w) *w = s->w;
    if (h) *h = s->h;
}

static uint32_t rgb_of(unsigned ega)
{
    const uint8_t *c = VL_EGARGBColorTable[ega & 15u];
    return (uint32_t) c[0] << 16 | (uint32_t) c[1] << 8 | c[2];
}

static void TANG_RefreshPaletteAndBorderColor(void *screen)
{
    uint32_t rgb[16];
    (void) screen;
    for (int i = 0; i < 16; i++)
        rgb[i] = rgb_of(vl_emuegavgaadapter.palette[i]);
    gfx_palette(rgb, rgb_of(vl_emuegavgaadapter.bordercolor));
}

static int TANG_SurfacePGet(void *surface, int x, int y)
{
    gfx_wait();
    return (int) px_get((const surf_t *) surface, x, y);
}

static void TANG_SurfaceRect(void *dst, int x, int y, int w, int h, int colour)
{
    surf_t *d = (surf_t *) dst;
    int sx, sy;
    if (clip(d, &x, &y, &w, &h, &sx, &sy))
        gfx_fill(d->pa + (uint32_t) y * d->pitch + (uint32_t) x, d->pitch, (unsigned) w, (unsigned) h, (unsigned) colour);
}

static void TANG_SurfaceRect_PM(void *surface, int x, int y, int w, int h, int colour, int mapmask)
{
    surf_t *d = (surf_t *) surface;
    uint32_t dst;
    int sx, sy;
    mapmask &= 15;
    if (mapmask == 15) {
        TANG_SurfaceRect(surface, x, y, w, h, colour);
        return;
    }
    if (!mapmask || !clip(d, &x, &y, &w, &h, &sx, &sy))
        return;
    /* A few pixels (the Star Wars text, a pixel at a time after a
     * VL_SurfacePGet that has waited for the GPU already): the CPU, about
     * half the time of two GPU fills (measured, up to 4 pixels) */
    if (w * h <= 4) {
        gfx_wait();
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                px_put(d, x + i, y + j, (px_get(d, x + i, y + j) & ~(unsigned) mapmask) | ((unsigned) colour & (unsigned) mapmask));
        return;
    }
    /* the planes outside mapmask kept: AND their bits, then OR the colour's
     * bits in the others */
    dst = d->pa + (uint32_t) y * d->pitch + (uint32_t) x;
    gfx_fill_rop(dst, d->pitch, (unsigned) w, (unsigned) h, ~(unsigned) mapmask & 15u, GFX_ROP_AND);
    if ((unsigned) colour & (unsigned) mapmask)
        gfx_fill_rop(dst, d->pitch, (unsigned) w, (unsigned) h, (unsigned) colour & (unsigned) mapmask, GFX_ROP_OR);
}

static void TANG_SurfaceToSurface(void *src_surface, void *dst_surface, int x, int y, int sx, int sy, int sw, int sh)
{
    const surf_t *s = (const surf_t *) src_surface;
    surf_t *d = (surf_t *) dst_surface;
    int cx, cy;
    if (!clip(d, &x, &y, &sw, &sh, &cx, &cy))
        return;
    sx += cx;
    sy += cy;
    gfx_copy(s->pa + (uint32_t) sy * s->pitch + (uint32_t) sx, s->pitch,
             d->pa + (uint32_t) y * d->pitch + (uint32_t) x, d->pitch, (unsigned) sw, (unsigned) sh, GFX_ROP_S, 0u);
}

/* Within one surface: walked away from the overlap */
static void TANG_SurfaceToSelf(void *surface, int x, int y, int sx, int sy, int sw, int sh)
{
    surf_t *s = (surf_t *) surface;
    unsigned back = 0u;
    if (y > sy)
        back |= GFX_BACK_Y;
    else if (y == sy && x > sx)
        back |= GFX_BACK_X;
    gfx_copy(s->pa + (uint32_t) sy * s->pitch + (uint32_t) sx, s->pitch,
             s->pa + (uint32_t) y * s->pitch + (uint32_t) x, s->pitch, (unsigned) sw, (unsigned) sh, GFX_ROP_S, back);
}

static void TANG_ScrollSurface(void *surface, int x, int y)
{
    const surf_t *s = (const surf_t *) surface;
    int dx, dy, sx, sy;
    const int w = s->w - (x < 0 ? -x : x), h = s->h - (y < 0 ? -y : y);
    if (x > 0) { dx = 0; sx = x; } else { dx = -x; sx = 0; }
    if (y > 0) { dy = 0; sy = y; } else { dy = -y; sy = 0; }
    TANG_SurfaceToSelf(surface, dx, dy, sx, sy, w, h);
}

/*------------------------------- planar EGA ---------------------------------*/

/* The staging ring (VRAM_STAGE): the CPU writes a 1-bit picture (text) or a
 * planar one (an 8 x 8 tile, a band of a picture) there in the GPU's form,
 * and the GPU draws it from there in order with the rest of its drawing, so
 * the CPU does not wait for the GPU (a present that waits for the end of the
 * displayed frame) before drawing. Each draw takes the next bytes, from a
 * word boundary; at the end of the ring the GPU's work queued is waited for
 * once (gfx_wait: no draw still reads the ring) and the ring starts again.
 * NULL: larger than the ring */
static uint32_t stage_at;

static uint8_t *stage_take(uint32_t n)
{
    uint8_t *p;
    if (n > VRAM_STAGE_SIZE)
        return NULL;
    stage_at = (stage_at + 3u) & ~3u;           /* word-aligned: the planar conversion writes words */
    if (stage_at + n > VRAM_STAGE_SIZE) {
        gfx_wait();
        stage_at = 0u;
    }
    p = arv_vram + VRAM_STAGE + stage_at;
    stage_at += n;
    return p;
}

/* The 8 bits of a row from bit k (the first pixel the most significant
 * bit, as the game stores them), of which n (1..8) are used: the second
 * byte read only when the used bits reach it */
static inline unsigned bits8(const uint8_t *src, uint32_t k, unsigned n)
{
    const uint8_t *q = src + (k >> 3);
    const unsigned sh = k & 7u;
    unsigned v = (unsigned) q[0] << sh;
    if (sh && n > 8u - sh)
        v |= (unsigned) q[1] >> (8u - sh);
    return v & 0xFFu;
}

/* The bit order reversed (the GPU's 1 bpp: the first pixel the least
 * significant bit) */
static inline unsigned rev8(unsigned v)
{
    static const uint8_t r4[16] = { 0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                    0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF };
    return (unsigned) (r4[v & 15u] << 4) | r4[(v >> 4) & 15u];
}

enum { PL_COPY, PL_MASKED, PL_PLANES };

/* The 4 pixels of a nibble of a plane (the first pixel the most significant
 * bit), each at bit 0 of its own 4 bits (the first pixel the lowest) */
static const uint16_t spread4[16] = {
    0x0000, 0x1000, 0x0100, 0x1100, 0x0010, 0x1010, 0x0110, 0x1110,
    0x0001, 0x1001, 0x0101, 0x1101, 0x0011, 0x1011, 0x0111, 0x1111 };

static inline uint32_t spread8(unsigned b)
{
    return (uint32_t) spread4[b >> 4] | (uint32_t) spread4[b & 15u] << 16;
}

/* Lines sy.. of a planar picture (plane bytes p0, bw bytes per line, plane
 * bytes per plane), byte columns c0..c0+nb-1, to 4 bpp at o (a word a byte
 * column, opitch words per line); m (not NULL) the mask plane, to 1 bpp at
 * om (nb bytes per line): its bits inverted (1 where the picture is opaque)
 * and in the GPU's order */
static void convert(const uint8_t *p0, int bw, int plane, int sy, int lines, int c0, int nb,
                    uint32_t *o, unsigned opitch, const uint8_t *m, uint8_t *om)
{
    for (int j = 0; j < lines; j++) {
        const uint8_t *q = p0 + (sy + j) * bw + c0;
        uint32_t *w = o + (unsigned) j * opitch;
        for (int k = 0; k < nb; k++, q++)
            w[k] = spread8(q[0]) | spread8(q[plane]) << 1 | spread8(q[2 * plane]) << 2 | spread8(q[3 * plane]) << 3;
        if (m) {
            const uint8_t *r = m + (sy + j) * bw + c0;
            uint8_t *t = om + (unsigned) j * (unsigned) nb;
            for (int k = 0; k < nb; k++)
                t[k] = (uint8_t) rev8(~(unsigned) r[k] & 0xFFu);
        }
    }
}

/* The GPU's draw of a converted rectangle: pix at 4 bpp and, for
 * PL_MASKED, mask its opaque bits at 1 bpp, both at a pitch of pitch pixels.
 * Masked as the EGA latches: the opaque pixels cleared (the mask expanded in
 * colour 0), then the picture ORed in, so a transparent pixel is the old one
 * ORed with the picture's, an opaque one the picture's */
static void planar_draw(uint32_t pix, uint32_t mask, unsigned pitch, surf_t *d, int x, int y, int cw, int ch, int mode)
{
    const uint32_t dst = d->pa + (uint32_t) y * d->pitch + (uint32_t) x;
    if (mode == PL_MASKED) {
        gfx_expand(mask, pitch, dst, d->pitch, (unsigned) cw, (unsigned) ch, 0u);
        gfx_copy(pix, pitch, dst, d->pitch, (unsigned) cw, (unsigned) ch, GFX_ROP_OR, 0u);
    } else
        gfx_copy(pix, pitch, dst, d->pitch, (unsigned) cw, (unsigned) ch, GFX_ROP_S, 0u);
}

/* The chunk of a picture whose data is src (a bitmap for PL_COPY, a masked
 * one otherwise), -1 when it is none (an 8 x 8 tile, a sprite's data) */
static int pic_chunk_of(const void *src, int mode)
{
    const int c0 = mode == PL_COPY ? ca_gfxInfoE.offBitmaps : ca_gfxInfoE.offMasked;
    const int n  = mode == PL_COPY ? ca_gfxInfoE.numBitmaps : ca_gfxInfoE.numMasked;
    for (int c = c0; c < c0 + n; c++)
        if (ca_graphChunks[c] == src)
            return c;
    return -1;
}

/* A picture larger than the staging ring, converted whole into the picture
 * cache (gfxcache.c) the first time, then drawn from there; false when it
 * is not a picture or does not fit the cache */
static bool planar_cached(const uint8_t *src, int w, int h, int mode, surf_t *d, int x, int y, int cw, int ch, int sx, int sy)
{
    const int bw = w / 8, plane = bw * h;
    const uint32_t npix = (uint32_t) plane * 4u, n = npix + (mode == PL_MASKED || mode == PL_PLANES ? (uint32_t) plane : 0u);
    const int chunk = pic_chunk_of(src, mode);
    bool fresh;
    uint8_t *p;
    if (chunk < 0 || !(p = gc_picture(chunk, n, &fresh)))
        return false;
    if (fresh)
        convert(src + (mode == PL_COPY ? 0 : plane), bw, plane, 0, h, 0, bw, (uint32_t *) p, (unsigned) bw,
                mode == PL_COPY ? NULL : src, p + npix);
    planar_draw(arv_pa4(p) + (uint32_t) (sy * w + sx), blns_vram_ba(&arv_blns, p + npix) + (uint32_t) (sy * w + sx),
                (unsigned) w, d, x, y, cw, ch, mode);
    return true;
}

/* The clipped rectangle converted to 4 bpp (and its mask) in the staging
 * ring, whole bytes of the planes, in bands of lines as many as the ring
 * holds, each drawn by the GPU */
static void planar_staged(const uint8_t *src, int w, int h, int mode, surf_t *d, int x, int y, int cw, int ch, int sx, int sy)
{
    const int bw = w / 8, plane = bw * h;
    const int c0 = sx >> 3, nb = ((sx + cw + 7) >> 3) - c0, ox = sx & 7;
    const uint32_t lpix = 4u * (uint32_t) nb, lmask = mode == PL_MASKED ? (uint32_t) nb : 0u;
    const int band = (int) (VRAM_STAGE_SIZE / (lpix + lmask));
    for (int j = 0; j < ch; j += band) {
        const int lines = ch - j < band ? ch - j : band;
        const uint32_t npix = lpix * (uint32_t) lines;
        uint8_t *t = stage_take(npix + lmask * (uint32_t) lines);
        convert(src + (mode == PL_COPY ? 0 : plane), bw, plane, sy + j, lines, c0, nb, (uint32_t *) t, (unsigned) nb,
                mode == PL_MASKED ? src : NULL, t + npix);
        planar_draw(arv_pa4(t) + (uint32_t) ox, blns_vram_ba(&arv_blns, t + npix) + (uint32_t) ox,
                    8u * (unsigned) nb, d, x, y + j, cw, lines, mode);
    }
}

/* A picture in the game's planar form (4 planes of w/8 bytes x h, after the
 * mask plane for PL_MASKED / PL_PLANES): PL_COPY writes it, PL_MASKED as the
 * EGA latches (the mask bit set: ORed in), PL_PLANES the colour planes
 * without the mask; the planes outside mapmask are kept. With every plane
 * the GPU draws it (above); the plane-masked draws are the CPU's, after
 * waiting for the GPU */
static void planar(const uint8_t *src, surf_t *d, int x, int y, int w, int h, int mode, unsigned mapmask)
{
    const int bw = w / 8, plane = bw * h;
    const uint8_t *m = src, *p0 = src + (mode == PL_COPY ? 0 : plane);
    int sx, sy, cw = w, ch = h;
    if (!clip(d, &x, &y, &cw, &ch, &sx, &sy))
        return;
    if (mapmask == 15u) {
        if ((uint32_t) plane * 4u <= VRAM_STAGE_SIZE || !planar_cached(src, w, h, mode, d, x, y, cw, ch, sx, sy))
            planar_staged(src, w, h, mode, d, x, y, cw, ch, sx, sy);
        return;
    }
    gfx_wait();
    for (int j = 0; j < ch; j++) {
        const int r = (sy + j) * bw;
        for (int i = 0; i < cw; i++) {
            const int c = sx + i, o = r + (c >> 3), b = 7 - (c & 7);
            unsigned v = ((p0[o] >> b) & 1u) | (((p0[o + plane] >> b) & 1u) << 1) |
                         (((p0[o + 2 * plane] >> b) & 1u) << 2) | (((p0[o + 3 * plane] >> b) & 1u) << 3);
            const unsigned old = px_get(d, x + i, y + j);
            if (mode == PL_MASKED && ((m[o] >> b) & 1u))
                v |= old;
            px_put(d, x + i, y + j, (old & ~mapmask) | (v & mapmask));
        }
    }
}

enum { B1_OPAQUE, B1_SET, B1_CLEAR, B1_XOR };

/* With every plane: the clipped rectangle in the GPU's bit order in the
 * staging ring (inverted for B1_CLEAR), then a GPU colour expansion: opaque
 * in colour and 0 (B1_OPAQUE), transparent (B1_SET, B1_CLEAR) or XOR
 * (B1_XOR); false when it does not fit the ring */
static bool bits_gpu(const uint8_t *src, int spitch, surf_t *d, int x, int y, int cw, int ch, int sx, int sy,
                     unsigned colour, int mode)
{
    const unsigned bpr = ((unsigned) cw + 7u) / 8u;         /* staging bytes per row */
    const unsigned inv = (mode == B1_CLEAR) ? 0xFFu : 0u;
    uint8_t *t = stage_take(bpr * (unsigned) ch);
    if (!t)
        return false;
    for (int j = 0; j < ch; j++) {
        const uint32_t k = (uint32_t) ((sy + j) * spitch + sx);
        uint8_t *o = t + (unsigned) j * bpr;
        for (unsigned b = 0; b < bpr; b++) {
            const unsigned n = (unsigned) cw - 8u * b;
            o[b] = (uint8_t) rev8(bits8(src, k + 8u * b, n < 8u ? n : 8u) ^ inv);
        }
    }
    gfx_expand1(blns_vram_ba(&arv_blns, t), bpr * 8u, d->pa + (uint32_t) y * d->pitch + (uint32_t) x, d->pitch,
                (unsigned) cw, (unsigned) ch, colour,
                mode == B1_OPAQUE ? GFX_EXP_OPAQUE : mode == B1_XOR ? GFX_EXP_XOR : GFX_EXP_SET);
    return true;
}

/* A 1-bit picture: B1_OPAQUE the colour where set and 0 where clear (rows
 * packed), B1_SET / B1_CLEAR the colour where set / clear, B1_XOR the colour
 * XORed where set (rows of whole bytes) */
static void bits(const uint8_t *src, surf_t *d, int x, int y, int w, int h, unsigned colour, int mode, unsigned mapmask)
{
    const int spitch = (mode == B1_OPAQUE) ? w : (w + 7) & ~7;
    int sx, sy, cw = w, ch = h;
    if (!clip(d, &x, &y, &cw, &ch, &sx, &sy))
        return;
    if (mapmask == 15u && bits_gpu(src, spitch, d, x, y, cw, ch, sx, sy, colour, mode))
        return;
    gfx_wait();
    for (int j = 0; j < ch; j++)
        for (int i = 0; i < cw; i++) {
            const int k = (sy + j) * spitch + sx + i;
            const bool set = (src[k >> 3] >> (7 - (k & 7))) & 1u;
            const unsigned old = px_get(d, x + i, y + j);
            unsigned v;
            switch (mode) {
            case B1_OPAQUE: v = set ? colour : 0u; break;
            case B1_SET:    if (!set) continue; v = colour; break;
            case B1_CLEAR:  if (set) continue; v = colour; break;
            default:        if (!set) continue; v = old ^ colour; break;
            }
            px_put(d, x + i, y + j, (old & ~mapmask) | (v & mapmask));
        }
}

static void TANG_UnmaskedToSurface(void *src, void *dst, int x, int y, int w, int h)
{ planar((const uint8_t *) src, (surf_t *) dst, x, y, w, h, PL_COPY, 15u); }
static void TANG_UnmaskedToSurface_PM(void *src, void *dst, int x, int y, int w, int h, int mapmask)
{ planar((const uint8_t *) src, (surf_t *) dst, x, y, w, h, PL_COPY, (unsigned) mapmask & 15u); }
static void TANG_MaskedToSurface(void *src, void *dst, int x, int y, int w, int h)
{ planar((const uint8_t *) src, (surf_t *) dst, x, y, w, h, PL_PLANES, 15u); }
static void TANG_MaskedBlitToSurface(void *src, void *dst, int x, int y, int w, int h)
{ planar((const uint8_t *) src, (surf_t *) dst, x, y, w, h, PL_MASKED, 15u); }
static void TANG_BitToSurface(void *src, void *dst, int x, int y, int w, int h, int colour)
{ bits((const uint8_t *) src, (surf_t *) dst, x, y, w, h, (unsigned) colour & 15u, B1_OPAQUE, 15u); }
static void TANG_BitToSurface_PM(void *src, void *dst, int x, int y, int w, int h, int colour, int mapmask)
{ bits((const uint8_t *) src, (surf_t *) dst, x, y, w, h, (unsigned) colour & 15u, B1_OPAQUE, (unsigned) mapmask & 15u); }
static void TANG_BitXorWithSurface(void *src, void *dst, int x, int y, int w, int h, int colour)
{ bits((const uint8_t *) src, (surf_t *) dst, x, y, w, h, (unsigned) colour & 15u, B1_XOR, 15u); }
static void TANG_BitBlitToSurface(void *src, void *dst, int x, int y, int w, int h, int colour)
{ bits((const uint8_t *) src, (surf_t *) dst, x, y, w, h, (unsigned) colour & 15u, B1_SET, 15u); }
static void TANG_BitInvBlitToSurface(void *src, void *dst, int x, int y, int w, int h, int colour)
{ bits((const uint8_t *) src, (surf_t *) dst, x, y, w, h, (unsigned) colour & 15u, B1_CLEAR, 15u); }

/*------------------------------- cached graphics (GPU) ----------------------*/

/* A 16 x 16 tile of the cache at (x, y) of a surface (NULL: the screen) */
void VL_Tang_Tile(void *src, void *surface, int x, int y)
{
    surf_t *d = (surf_t *) (surface ? surface : vl_emuegavgaadapter.screen);
    int w = 16, h = 16, sx, sy;
    if (!src || !clip(d, &x, &y, &w, &h, &sx, &sy))
        return;
    gfx_copy(arv_pa4(src) + (uint32_t) (sy * 16 + sx), 16u, d->pa + (uint32_t) y * d->pitch + (uint32_t) x, d->pitch,
             (unsigned) w, (unsigned) h, GFX_ROP_S, 0u);
}

/* A w x h record (mask, then pixels) at (x, y), clipped: masked, or its mask
 * in a colour */
static void masked_rec(surf_t *d, const uint8_t *rec, int rw, int rh, int x, int y, int colour)
{
    int w = rw, h = rh, sx, sy;
    uint32_t dst, skip;
    if (!clip(d, &x, &y, &w, &h, &sx, &sy))
        return;
    dst = d->pa + (uint32_t) y * d->pitch + (uint32_t) x;
    skip = (uint32_t) (sy * rw + sx);
    if (colour < 0)
        gfx_masked(arv_pa4(rec + rw / 8 * rh) + skip, blns_vram_ba(&arv_blns, rec) + skip, (unsigned) rw,
                   dst, d->pitch, (unsigned) w, (unsigned) h);
    else
        gfx_expand(blns_vram_ba(&arv_blns, rec) + skip, (unsigned) rw, dst, d->pitch, (unsigned) w, (unsigned) h, (unsigned) colour);
}

void VL_Tang_MaskedTile(void *src, void *surface, int x, int y)
{
    if (src)
        masked_rec((surf_t *) (surface ? surface : vl_emuegavgaadapter.screen), (const uint8_t *) src, 16, 16, x, y, -1);
}

void VL_Tang_DrawSprite(int x, int y, int sprite, int colour)
{
    const uint8_t *rec = (const uint8_t *) gc_sprite(sprite);
    VH_SpriteTableEntry e;
    if (!rec)
        return;
    e = VH_GetSpriteTableEntry(sprite);
    masked_rec((surf_t *) vl_emuegavgaadapter.screen, rec, e.width * 8, e.height, x, y, colour);
}

/*------------------------------- frames and time ----------------------------*/

static void TANG_Present(void *surface, int scrollXpx, int scrollYpx, bool singleBuffered)
{
    const surf_t *s = (const surf_t *) surface;
    (void) singleBuffered;
    arv_board_poll();
    gfx_present(s->pa + (uint32_t) scrollYpx * s->pitch + (uint32_t) scrollXpx, s->pitch);
}

static int  TANG_GetActiveBufferId(void *surface) { (void) surface; return 0; }
static int  TANG_GetNumBuffers(void *surface)     { (void) surface; return 1; }
static void TANG_SyncBuffers(void *surface)       { (void) surface; }
static void TANG_UpdateRect(void *surface, int x, int y, int w, int h) { (void) surface; (void) x; (void) y; (void) w; (void) h; }
static void TANG_FlushParams(void) {}

/* vbls 70 Hz refreshes, on the game clock (SD time count); 0: a pause for
 * the controls */
static void TANG_WaitVBLs(int vbls)
{
    const uint32_t t0 = SD_GetTimeCount();
    IN_PumpEvents();
    while (SD_GetTimeCount() - t0 < (uint32_t) vbls) {
        IN_PumpEvents();
        (void) tang_video_poll(false);          /* a palette step waiting for a frame start */
    }
}

static VL_Backend vl_tang_backend = {
    .setVideoMode = TANG_SetVideoMode,
    .createSurface = TANG_CreateSurface,
    .destroySurface = TANG_DestroySurface,
    .getSurfaceMemUse = TANG_GetSurfaceMemUse,
    .getSurfaceDimensions = TANG_GetSurfaceDimensions,
    .refreshPaletteAndBorderColor = TANG_RefreshPaletteAndBorderColor,
    .surfacePGet = TANG_SurfacePGet,
    .surfaceRect = TANG_SurfaceRect,
    .surfaceRect_PM = TANG_SurfaceRect_PM,
    .surfaceToSurface = TANG_SurfaceToSurface,
    .surfaceToSelf = TANG_SurfaceToSelf,
    .unmaskedToSurface = TANG_UnmaskedToSurface,
    .unmaskedToSurface_PM = TANG_UnmaskedToSurface_PM,
    .maskedToSurface = TANG_MaskedToSurface,
    .maskedBlitToSurface = TANG_MaskedBlitToSurface,
    .bitToSurface = TANG_BitToSurface,
    .bitToSurface_PM = TANG_BitToSurface_PM,
    .bitXorWithSurface = TANG_BitXorWithSurface,
    .bitBlitToSurface = TANG_BitBlitToSurface,
    .bitInvBlitToSurface = TANG_BitInvBlitToSurface,
    .scrollSurface = TANG_ScrollSurface,
    .present = TANG_Present,
    .getActiveBufferId = TANG_GetActiveBufferId,
    .getNumBuffers = TANG_GetNumBuffers,
    .syncBuffers = TANG_SyncBuffers,
    .updateRect = TANG_UpdateRect,
    .flushParams = TANG_FlushParams,
    .waitVBLs = TANG_WaitVBLs,
};

VL_Backend *VL_Impl_GetBackend(void)
{
    return &vl_tang_backend;
}

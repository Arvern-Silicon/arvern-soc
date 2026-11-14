/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      gfx.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The drawing primitives of the video layer at 4 bits per pixel: copies
 * (plain or ORed), fills, the expansion of a 1-bit mask into one colour,
 * masked draws (one pass with the mask plane) and the copy of the view to
 * the displayed frame. The Belenos GPU does them
 * (in order, behind the CPU); with ARV_SOFT_GFX the CPU does, the same
 * pixels (the reference the GPU is checked against, and the simulator
 * build). While a present waits for the end of the displayed frame, the
 * GPU's drawing goes into a command list (below).
 *----------------------------------------------------------------------------*/
#include <string.h>
#include "tang.h"

#ifndef ARV_SOFT_GFX

#include "belenos.h"
#include "tang_board.h"
#include "tang_video.h"

extern blns_t arv_blns;
uint32_t arv_vs_now, arv_vs_wait;

void gfx_init(void) {}

/* Behind a present that waits for the end of the displayed frame (the GPU
 * holds on WAIT_REFR_DONE for up to a frame), the 8-word command FIFO is
 * full after about one more draw, and the CPU would wait there for the
 * refresh instead of running the game. While a present waits, the draws,
 * and the next present if it comes first, are recorded into a command list
 * instead: the same calls in the same order (a present still decides at its
 * call whether it waits), one CALL in the FIFO. At each frame end the list
 * is called and the next one taken, so that a present recorded reaches the
 * GPU when it would have from the FIFO; once no present waits, the draws go
 * to the FIFO again. A list nearly full is called and the next one taken;
 * when that one still runs, the draws go to the FIFO as before. Three lists
 * in turn: the one taken at a frame end is not the one called at the frame
 * end before, whose present the GPU may still be copying. Every way out
 * calls the list first, so nothing pushed later overtakes it. With AHEAD
 * presents waiting, the next one waits for the first one's frame end: the
 * picture is at most AHEAD refreshes behind the game (a 16-word FIFO let
 * the CPU run about as far ahead; 2 measured slower, 4 as fast). */
#define LISTS       3u
#define AHEAD       4u                          /* presents waiting at most   */
#define LIST_BYTES  ((VRAM_LISTS_SIZE / LISTS) & ~7u)  /* each list (340 words) */
#define LIST_MARGIN 32u                         /* words: above the longest call */

static blns_gpu_list_t  lists[LISTS];
static blns_gpu_list_t *rec;                    /* the list being recorded    */
static unsigned         next;                   /* the list to record next    */
static unsigned         owed;                   /* frame ends the presents sent wait for */
static uint16_t         seen;                   /* frame ends since the first one  */
static uint16_t         base;                   /* the frame number then      */

static void rec_call(void)
{
    blns_gpu_list_t *l = rec;
    rec = NULL;
    if (blns_gpu_list_end(&arv_blns) == BLNS_ERR_FULL)
        arv_printf("keen4: GPU list overflow\n");
    (void) blns_gpu_list_call(&arv_blns, l, false);
}

/* The draws recorded so far to the GPU, and no more recording */
static void unroute(void)
{
    owed = 0u;
    if (rec)
        rec_call();
}

/* Before each GPU call: into a list while a present waits, else the FIFO */
static void route(void)
{
    blns_gpu_list_t *l;
    uint16_t         n;
    if (!owed)
        return;
    n = (uint16_t) (blns_frame_num(&arv_blns) - base);
    if (n >= owed) {                            /* every present has gone     */
        unroute();
        return;
    }
    if (rec && n == seen && rec->count + LIST_MARGIN <= rec->capacity)
        return;
    seen = n;                                   /* a frame end, or a list full */
    if (rec)
        rec_call();
    l = &lists[next];
    if (!l->capacity) {
        l->addr     = blns_vram_wa(&arv_blns, arv_vram + VRAM_LISTS + next * LIST_BYTES);
        l->capacity = LIST_BYTES / 4u;
    }
    if (blns_gpu_list_busy(&arv_blns, l) || blns_gpu_list_begin(&arv_blns, l, 0u) != BLNS_OK)
        return;                                 /* the FIFO, as before        */
    rec  = l;
    next = (next + 1u) % LISTS;
}

/* AHEAD presents waiting already: the CPU waits for the first one's frame
 * end (the GPU shows a frame per refresh) */
static __attribute__((noinline)) void present_room(void)
{
    while (owed >= AHEAD && (uint16_t) (blns_frame_num(&arv_blns) - base) + AHEAD <= owed)
        if (blns_wait_frame(&arv_blns) != BLNS_OK)
            break;
}

static void surf(blns_surf_t *s, uint32_t pa, unsigned pitch, unsigned w, unsigned h, unsigned back)
{
    *s = blns_surf_rect(&arv_blns, pa, (uint16_t) pitch, (uint16_t) w, (uint16_t) h,
                        (uint8_t) (((back & GFX_BACK_X) ? BLNS_SWAP_X : 0u) | ((back & GFX_BACK_Y) ? BLNS_SWAP_Y : 0u)));
}

void gfx_copy(uint32_t src, unsigned spitch, uint32_t dst, unsigned dpitch,
              unsigned w, unsigned h, int rop, unsigned back)
{
    blns_surf_t s, d;
    if (!w || !h)
        return;
    route();
    surf(&s, src, spitch, w, h, back);
    surf(&d, dst, dpitch, w, h, back);
    blns_gpu_copy(&arv_blns, &s, &d, (uint16_t) w, (uint16_t) h, rop == GFX_ROP_OR ? BLNS_ROP_OR : BLNS_ROP_S);
}

void gfx_fill_rop(uint32_t dst, unsigned dpitch, unsigned w, unsigned h, unsigned colour, int rop)
{
    blns_surf_t d;
    if (!w || !h)
        return;
    route();
    surf(&d, dst, dpitch, w, h, 0u);
    blns_gpu_fill(&arv_blns, &d, (uint16_t) w, (uint16_t) h, colour & 15u,
                  rop == GFX_ROP_AND ? BLNS_ROP_AND : rop == GFX_ROP_OR ? BLNS_ROP_OR : BLNS_ROP_S);
}

void gfx_fill(uint32_t dst, unsigned dpitch, unsigned w, unsigned h, unsigned colour)
{
    gfx_fill_rop(dst, dpitch, w, h, colour, GFX_ROP_S);
}

void gfx_expand(uint32_t src1, unsigned spitch, uint32_t dst, unsigned dpitch,
                unsigned w, unsigned h, unsigned colour)
{
    const blns_expand_t e = { .width = (uint16_t) w, .height = (uint16_t) h, .src_pitch = (uint16_t) spitch,
                              .depth = BLNS_EXP_1BPP, .opaque = false, .mask = false, .swap = BLNS_SWAP_NONE };
    if (!w || !h)
        return;
    route();
    blns_gpu_expand_begin(&arv_blns, &e, dst, (uint16_t) dpitch, colour & 15u, 0u);
    blns_gpu_expand(&arv_blns, src1, dst);
}

void gfx_expand1(uint32_t src1, unsigned spitch, uint32_t dst, unsigned dpitch,
                 unsigned w, unsigned h, unsigned colour, int mode)
{
    const blns_expand_t e = { .width = (uint16_t) w, .height = (uint16_t) h, .src_pitch = (uint16_t) spitch,
                              .depth = BLNS_EXP_1BPP, .opaque = mode == GFX_EXP_OPAQUE, .mask = false,
                              .swap = BLNS_SWAP_NONE, .rop = mode == GFX_EXP_XOR ? BLNS_ROP_XOR : BLNS_ROP_S };
    if (!w || !h)
        return;
    route();
    blns_gpu_expand_begin(&arv_blns, &e, dst, (uint16_t) dpitch, colour & 15u, 0u);
    blns_gpu_expand(&arv_blns, src1, dst);
}

/* One pass with the mask plane: the picture where its mask bit is 1. The
 * same pixels as the two passes (expand the mask in colour 0, OR the
 * picture) because GFX4BPP.CK4 has colour 0 under every clear mask bit
 * (tools/make_gfx.py; the score box digits land inside its mask). Without
 * the mask plane, the two passes. */
void gfx_masked(uint32_t src, uint32_t mask, unsigned spitch, uint32_t dst, unsigned dpitch,
                unsigned w, unsigned h)
{
    static const blns_blit_t masked = { .rop = BLNS_ROP_S, .mplane = true };
    blns_surf_t s, d;
    if (!w || !h)
        return;
    route();
    if (BLNS_HAS(&arv_blns, GPU_MPLANE) && blns_gpu_mask_plane(&arv_blns, mask, false) == BLNS_OK) {
        surf(&s, src, spitch, w, h, 0u);
        surf(&d, dst, dpitch, w, h, 0u);
        blns_gpu_blit(&arv_blns, &s, &d, (uint16_t) w, (uint16_t) h, &masked);
        return;
    }
    gfx_expand(mask, spitch, dst, dpitch, w, h, 0u);
    gfx_copy(src, spitch, dst, dpitch, w, h, GFX_ROP_OR, 0u);
}

void gfx_wait(void)
{
    unroute();
    blns_gpu_wait(&arv_blns);
}

/* Tear-free: into the colour table bank not shown, selected at a frame start
 * (tang_video_palette; a fade step of the same frame goes at the next poll) */
void gfx_palette(const uint32_t rgb[16], uint32_t border)
{
    static uint32_t shown[16];
    memcpy(shown, rgb, sizeof(shown));
    tang_video_palette(shown, 16u);
    blns_dpi_border(&arv_blns, border);
}

/* The view into the displayed frame, at the end of a displayed frame (inside
 * the vertical blanking) unless the scan is past the image or less than a
 * third into it: then at once, a tear at worst, rather than a frame late */
void gfx_present(uint32_t src, unsigned spitch)
{
    const blns_surf_t s = { src, (uint16_t) spitch, BLNS_SWAP_NONE };
    const blns_surf_t d = { VRAM_FRAME * 2u, FRAME_W, BLNS_SWAP_NONE };
    bool              waited;
    present_room();
    route();
    const blns_status_t st = blns_gpu_present_copy(&arv_blns, &s, &d, FRAME_W, FRAME_H,
                                                   BLNS_PRESENT_ADAPTIVE, &waited);
    if (st == BLNS_ERR_GPU || st == BLNS_ERR_STATE) {
        /* once a frame: a stopped GPU would freeze the picture silently */
        blns_gpu_err_t e;
        unroute();
        blns_gpu_error(&arv_blns, &e);
        arv_printf("keen4: GPU error %d (cause %u op %u), recovered\n", (int) st,
                   (unsigned) e.cause, (unsigned) e.opcode);
        blns_gpu_recover(&arv_blns);
        return;
    }
    if (waited) {
        arv_vs_wait++;
        if (!owed++) {                          /* the next calls into a list */
            base = blns_frame_num(&arv_blns);
            seen = 0u;
        }
    } else
        arv_vs_now++;
}

#else /* ARV_SOFT_GFX */

static inline unsigned px_get(uint32_t pa)
{
    return (arv_vram[pa >> 1] >> ((pa & 1u) * 4u)) & 15u;
}

static inline void px_put(uint32_t pa, unsigned v)
{
    uint8_t *b = &arv_vram[pa >> 1];
    *b = (pa & 1u) ? (uint8_t) ((*b & 0x0Fu) | (v << 4)) : (uint8_t) ((*b & 0xF0u) | v);
}

void gfx_init(void) {}

void gfx_copy(uint32_t src, unsigned spitch, uint32_t dst, unsigned dpitch,
              unsigned w, unsigned h, int rop, unsigned back)
{
    for (unsigned j = 0; j < h; j++) {
        const unsigned y = (back & GFX_BACK_Y) ? h - 1u - j : j;
        for (unsigned i = 0; i < w; i++) {
            const unsigned x = (back & GFX_BACK_X) ? w - 1u - i : i;
            unsigned v = px_get(src + y * spitch + x);
            if (rop == GFX_ROP_OR)
                v |= px_get(dst + y * dpitch + x);
            px_put(dst + y * dpitch + x, v);
        }
    }
}

void gfx_fill_rop(uint32_t dst, unsigned dpitch, unsigned w, unsigned h, unsigned colour, int rop)
{
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            const uint32_t d = dst + y * dpitch + x;
            unsigned v = colour & 15u;
            if (rop == GFX_ROP_AND)
                v &= px_get(d);
            else if (rop == GFX_ROP_OR)
                v |= px_get(d);
            px_put(d, v);
        }
}

void gfx_fill(uint32_t dst, unsigned dpitch, unsigned w, unsigned h, unsigned colour)
{
    gfx_fill_rop(dst, dpitch, w, h, colour, GFX_ROP_S);
}

void gfx_expand(uint32_t src1, unsigned spitch, uint32_t dst, unsigned dpitch,
                unsigned w, unsigned h, unsigned colour)
{
    if (!spitch)
        spitch = w;
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            const uint32_t s = src1 + y * spitch + x;
            if ((arv_vram[s >> 3] >> (s & 7u)) & 1u)
                px_put(dst + y * dpitch + x, colour & 15u);
        }
}

void gfx_expand1(uint32_t src1, unsigned spitch, uint32_t dst, unsigned dpitch,
                 unsigned w, unsigned h, unsigned colour, int mode)
{
    if (!spitch)
        spitch = w;
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            const uint32_t s = src1 + y * spitch + x;
            const uint32_t d = dst + y * dpitch + x;
            if ((arv_vram[s >> 3] >> (s & 7u)) & 1u)
                px_put(d, mode == GFX_EXP_XOR ? px_get(d) ^ (colour & 15u) : colour & 15u);
            else if (mode == GFX_EXP_OPAQUE)
                px_put(d, 0u);
        }
}

/* The reference: the EGA way, the mask clears the shape, the picture is ORed in */
void gfx_masked(uint32_t src, uint32_t mask, unsigned spitch, uint32_t dst, unsigned dpitch,
                unsigned w, unsigned h)
{
    gfx_expand(mask, spitch, dst, dpitch, w, h, 0u);
    gfx_copy(src, spitch, dst, dpitch, w, h, GFX_ROP_OR, 0u);
}

void gfx_wait(void) {}

uint32_t arv_palette[16], arv_border;

void gfx_palette(const uint32_t rgb[16], uint32_t border)
{
    for (int i = 0; i < 16; i++)
        arv_palette[i] = rgb[i];
    arv_border = border;
}

void gfx_present(uint32_t src, unsigned spitch)
{
    gfx_copy(src, spitch, VRAM_FRAME * 2u, FRAME_W, FRAME_W, FRAME_H, GFX_ROP_S, 0u);
}

#endif

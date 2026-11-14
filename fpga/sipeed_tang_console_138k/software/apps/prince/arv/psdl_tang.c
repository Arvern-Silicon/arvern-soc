/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      psdl_tang.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * picosdl's backend on the Tang (psdl_internal.h):
 *   video   the game draws into its frame in the VRAM (320 x 200 at 8 bpp);
 *           a present is the GPU's copy of it into the displayed frame not
 *           shown, once the refresh has left it (the previous flip taken),
 *           and the sync waits for that copy and flips the refresh to it
 *           from its next frame: no tear, nothing waits for the scan unless
 *           the game presents faster than the refresh. ARV_PRESENT_COPY: one
 *           displayed frame, the copy (of the frame or of the rectangle) into
 *           it timed against the scan (BLNS_PRESENT_ADAPTIVE). The game never
 *           draws into what is shown; the palette is the Belenos colour
 *           table
 *   drawing the GPU draws picosdl's blits and fills whose pixels are all in
 *           the VRAM (the game's frame, the image cache, the font, picosdl's
 *           surface arena): the 4 bpp images by colour expansion, the 8 bpp
 *           surfaces by copies; the CPU waits for it before it touches VRAM
 *           pixels itself (psdl_backend_cpu_touch) and before the image cache
 *           is overwritten (arv_gpu_sync)
 *   input   USB keyboards (a HID usage is an SDL scancode) and gamepads:
 *             d-pad   the arrows (run, jump, crouch; Up also climbs)
 *             A       Shift (grab, pick up, strike)
 *             B       Up (jump), Y Down (crouch)
 *             X       Space (time left)
 *             Start   Enter (start; continue)
 *             Select  Escape (pause)
 *   time    the machine timer
 *   audio   the I2S interrupt pulls the mixer (psdl_audio_render)
 *----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <string.h>
#include "SDL2/SDL.h"
#include "psdl_internal.h"
#include "tang.h"
#include "tang_board.h"
#ifndef ARV_SOFT_GFX
#include "belenos.h"
extern blns_t arv_blns;                         /* board.c */
#endif

/* picosdl's surface arena (PSDL_ARENA_EXTERN) */
Uint8 *const psdl_arena = (Uint8 *) (BOARD_VRAM_BASE + VRAM_ARENA);
_Static_assert(PSDL_ARENA_BYTES <= VRAM_ARENA_SIZE, "picosdl's arena is larger than its VRAM region");

int arv_vprintf(const char *fmt, va_list ap);
uint32_t arv_presents;

void psdl_backend_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    arv_vprintf(fmt, ap);
    va_end(ap);
}

/*------------------------------- video --------------------------------------*/

void psdl_backend_video_init(int w, int h) { (void) w; (void) h; }

#ifndef ARV_SOFT_GFX
static bool gpu_queued;                         /* GPU work queued since the last wait */

#ifdef SIMULATION
static uint32_t gpu_nibs, gpu_blits, gpu_fills, gpu_waits;
static uint32_t sync_ticks, wait_ticks;         /* machine timer ticks waiting: presents, GPU */
static uint32_t flip_ticks, flip_waits;         /* of sync_ticks: waiting for the previous flip */
#ifdef ARV_PRESENT_COPY
static uint32_t scan_waits;                     /* copies queued behind the end of a refresh */
#endif
#endif

#ifdef ARV_PRESENT_COPY
/* At the end of a displayed frame (inside the vertical blanking) unless the
 * scan is past the image or less than a third into it: then at once, a tear
 * at worst, rather than a frame late */
static void frame_copy(unsigned x, unsigned y, unsigned w, unsigned h)
{
    const blns_surf_t s = { VRAM_BACK + y * FRAME_W + x, FRAME_W, BLNS_SWAP_NONE };
    const blns_surf_t d = { VRAM_FRAME + y * FRAME_W + x, FRAME_W, BLNS_SWAP_NONE };
    bool waited = false;
    if (!w || !h)
        return;
    blns_gpu_present_copy(&arv_blns, &s, &d, (uint16_t) w, (uint16_t) h, BLNS_PRESENT_ADAPTIVE, &waited);
#ifdef SIMULATION
    scan_waits += waited;
#endif
    gpu_queued = true;                          /* reads the game's frame */
}

static void frame_flip(void) {}
#else
/* Two displayed frames (frame pointers 0 and 1, board.c), shown in turn: the
 * whole game frame is copied into the one not shown (a rectangle would leave
 * the rest two presents old), then the refresh flips to it at its next frame
 * start (frame_flip, once the copy is done). Before the copy, the flip before
 * must have been taken: the frame copied into is the one shown until then. */
static unsigned shown;                          /* frame pointer the refresh shows */
static bool     flip_due;                       /* a copy queued, its flip not yet asked */

static void frame_copy(unsigned x, unsigned y, unsigned w, unsigned h)
{
    static const uint32_t base[2] = { VRAM_FRAME, VRAM_FRAME1 };
    const blns_surf_t s = { VRAM_BACK, FRAME_W, BLNS_SWAP_NONE };
    (void) x; (void) y; (void) w; (void) h;
    if (flip_due)                               /* a present without its sync */
        psdl_backend_video_sync();
    if (!blns_flip_done(&arv_blns)) {
#ifdef SIMULATION
        const uint32_t t0 = arv_mtime();
#endif
        (void) blns_flip_wait(&arv_blns);       /* hooked, bounded (wait_polls) */
#ifdef SIMULATION
        flip_waits++;
        flip_ticks += arv_mtime() - t0;
        sync_ticks += arv_mtime() - t0;
#endif
    }
    const blns_surf_t d = { base[shown ^ 1u], FRAME_W, BLNS_SWAP_NONE };
    blns_gpu_copy(&arv_blns, &s, &d, FRAME_W, FRAME_H, BLNS_ROP_S);
    gpu_queued = true;                          /* reads the game's frame */
    flip_due   = true;
}

/* The copy done (blns_gpu_wait): the refresh shows it from its next frame */
static void frame_flip(void)
{
    if (!flip_due)
        return;
    flip_due = false;
    shown ^= 1u;
    blns_flip_async(&arv_blns, shown);
}
#endif

/*------------------------------- drawing ------------------------------------*/

static inline bool in_vram(const void *p)
{
    return p != NULL && (uintptr_t) p - BOARD_VRAM_BASE < VRAM_BYTES;
}

static inline uint32_t vram_off(const void *p)
{
    return (uint32_t) ((uintptr_t) p - BOARD_VRAM_BASE);
}

void arv_gpu_sync(void)
{
    if (!gpu_queued)
        return;
#ifdef SIMULATION
    const uint32_t t0 = arv_mtime();
#endif
    blns_gpu_wait(&arv_blns);
    gpu_queued = false;
    frame_flip();
#ifdef SIMULATION
    gpu_waits++;
    wait_ticks += arv_mtime() - t0;
#endif
}

void psdl_backend_cpu_touch(const void *src, const void *dst)
{
    if (gpu_queued && (in_vram(src) || in_vram(dst)))
        arv_gpu_sync();
}

/* A GPU call's status: drawn (1), or left to the CPU (0). Only a stopped GPU
 * or a stream cut by a timeout is recovered: a refused call (PARAM: a side
 * past caps.size_max; ABSENT; BUSY) sent nothing, and a recover would flush
 * the good blits still queued */
static int gpu_done(blns_status_t st)
{
    if (st == BLNS_OK) {
        gpu_queued = true;
        return 1;
    }
    if (st == BLNS_ERR_GPU || st == BLNS_ERR_STATE) {
        arv_printf("prince: gpu error %d\n", (int) st);
        blns_gpu_recover(&arv_blns);
    }
    return 0;
}

/* blit_nib(): the 4 bpp source expanded (value v: the image's row base + v),
 * keyed (0 skipped), opaque (0 drawn as base), mono (v != 0 drawn as the
 * colour: op 15), XOR (the destination XOR base + v for every v, 0 included:
 * op 5 on the opaque expansion); mirrored (dir -1, sx the first source column
 * read, the rightmost): the source read forward from its leftmost column into
 * the destination walked right to left (X swap, from its rightmost pixel) */
int psdl_backend_gpu_nib(const SDL_Surface *src, int sx, int sy, int dir, Uint8 *dp, int dpitch,
                         int w, int h, int mode, int keyed, Uint8 color)
{
    const uint8_t base = (uint8_t) src->colorkey;
    blns_expand_t e = { .width = (uint16_t) w, .height = (uint16_t) h,
                        .src_pitch = (uint16_t) (src->pitch * 2), .depth = BLNS_EXP_4BPP,
                        .opaque = false, .mask = false,
                        .swap = dir < 0 ? BLNS_SWAP_X : BLNS_SWAP_NONE, .rop = BLNS_ROP_S };
    uint32_t colour = (uint8_t) (base + 1u);
    if (!BLNS_HAS(&arv_blns, GPU) || !in_vram(src->pixels) || !in_vram(dp) || w <= 0 || h <= 0)
        return 0;
    if (mode == PSDL_NIB_COPY) {
        e.opaque = !keyed;
    } else if (mode == PSDL_NIB_MONO) {
        e.rop  = BLNS_ROP_FILL_IF_S;
        colour = color;
    } else {
        e.opaque = true;
        e.rop    = BLNS_ROP_XOR;
    }
    /* the 4 bpp source's pixel address: its bit address / 4 */
    const uint32_t s = blns_vram_ba(&arv_blns, src->pixels) / 4u + (uint32_t) sy * (uint32_t) src->pitch * 2u +
                       (uint32_t) (dir < 0 ? sx - (w - 1) : sx);
    const uint32_t d = vram_off(dp) + (uint32_t) (dir < 0 ? w - 1 : 0);
    blns_status_t st = blns_gpu_expand_begin(&arv_blns, &e, d, (uint16_t) dpitch, colour, base);
    if (st == BLNS_OK)
        st = blns_gpu_expand(&arv_blns, s, d);
#ifdef SIMULATION
    gpu_nibs++;
#endif
    return gpu_done(st);
}

/* An 8 bpp blit between VRAM surfaces: a copy (XOR: op 5), or a transparent
 * copy (keyed); mirrored: the source walked right to left (X swap from sp, its
 * rightmost pixel). Overlapping source and destination bytes stay on the CPU. */
int psdl_backend_gpu_blit(const Uint8 *sp, int spitch, int sdir, Uint8 *dp, int dpitch,
                          int w, int h, int mode, Uint8 key)
{
    if (!BLNS_HAS(&arv_blns, GPU) || !in_vram(sp) || !in_vram(dp) || w <= 0 || h <= 0)
        return 0;
    const uint32_t s_lo = vram_off(sp) - (uint32_t) (sdir < 0 ? w - 1 : 0);
    const uint32_t s_hi = s_lo + (uint32_t) (h - 1) * (uint32_t) spitch + (uint32_t) w;
    const uint32_t d_lo = vram_off(dp);
    const uint32_t d_hi = d_lo + (uint32_t) (h - 1) * (uint32_t) dpitch + (uint32_t) w;
    if (s_lo < d_hi && d_lo < s_hi)
        return 0;
    const blns_surf_t s = { vram_off(sp), (uint16_t) spitch, sdir < 0 ? BLNS_SWAP_X : BLNS_SWAP_NONE };
    const blns_surf_t d = { d_lo, (uint16_t) dpitch, BLNS_SWAP_NONE };
    blns_status_t st;
    if (mode == PSDL_G_KEYED)
        st = blns_gpu_copy_transp(&arv_blns, &s, &d, (uint16_t) w, (uint16_t) h, BLNS_ROP_S, key);
    else
        st = blns_gpu_copy(&arv_blns, &s, &d, (uint16_t) w, (uint16_t) h,
                           mode == PSDL_G_XOR ? BLNS_ROP_XOR : BLNS_ROP_S);
#ifdef SIMULATION
    gpu_blits++;
#endif
    return gpu_done(st);
}

int psdl_backend_gpu_fill(Uint8 *dp, int dpitch, int w, int h, Uint8 v)
{
    if (!BLNS_HAS(&arv_blns, GPU) || !in_vram(dp) || w <= 0 || h <= 0)
        return 0;
    const blns_surf_t d = { vram_off(dp), (uint16_t) dpitch, BLNS_SWAP_NONE };
#ifdef SIMULATION
    gpu_fills++;
#endif
    return gpu_done(blns_gpu_fill(&arv_blns, &d, (uint16_t) w, (uint16_t) h, v, BLNS_ROP_S));
}

#ifdef PSDL_GPU_CHECK
/* The test build's scratch: a displayed frame (overwritten by the next
 * present); with two, the one not shown once the last flip is taken */
void   psdl_backend_gpu_sync(void) { blns_gpu_wait(&arv_blns); gpu_queued = false; frame_flip(); }
Uint8 *psdl_backend_check_scratch(void)
{
#ifdef ARV_PRESENT_COPY
    return arv_vram + VRAM_FRAME;
#else
    psdl_backend_gpu_sync();
    (void) blns_flip_wait(&arv_blns);
    return arv_vram + (shown ? VRAM_FRAME : VRAM_FRAME1);
#endif
}

/* Every blit mode once, through picosdl's entry points (so through the
 * check): a 13 x 7 image at 4 bpp and one at 8 bpp, odd widths, into a
 * 40 x 20 surface, clipped on the left and right, at the first present. The
 * surfaces sit at the end of the image cache (this test build only: the
 * images the game fetched first are overwritten if the cache is that full). */
static void gpu_selftest(void)
{
    static bool done;
    uint8_t *t = arv_vram + VRAM_BYTES - 0x1000u;
    uint32_t r = 0x12345678u;
    if (done)
        return;
    done = true;
    blns_gpu_wait(&arv_blns);
    gpu_queued = false;
    for (unsigned i = 0; i < 0x1000u; i++) {
        r = r * 1664525u + 1013904223u;
        t[i] = (uint8_t) (r >> 24);
    }
    for (unsigned i = 0; i < 7u * 7u; i += 5u)
        t[i] &= 0xF0u;                          /* some 0 values (transparent) */
    SDL_Surface *nib = SDL_CreateRGBSurfaceFrom(t, 13, 7, 8, 7, 0, 0, 0, 0);
    SDL_Surface *s8  = SDL_CreateRGBSurfaceFrom(t + 0x100, 13, 7, 8, 16, 0, 0, 0, 0);
    SDL_Surface *d   = SDL_CreateRGBSurfaceFrom(t + 0x400, 40, 20, 8, 40, 0, 0, 0, 0);
    if (!nib || !s8 || !d)
        return;
    nib->flags |= PSDL_SURF_NIBBLE;
    nib->colorkey = 0x30u;
    s8->colorkey = t[0x100 + 3];                /* a value the image has */
    for (int k = 0; k < 2; k++) {
        SDL_Surface *src = k ? s8 : nib;
        for (int keyed = 0; keyed < 2; keyed++) {
            src->has_colorkey = keyed ? SDL_TRUE : SDL_FALSE;
            for (int x = -3; x <= 30; x += 33) {   /* clipped on the left, then on the right */
                SDL_Rect dr = { x, 2 + keyed, 0, 0 };
                SDL_UpperBlit(src, NULL, d, &dr);
                dr = (SDL_Rect) { x, 5, 0, 0 };
                PSDL_BlitTransp(src, NULL, d, &dr, keyed);
                dr = (SDL_Rect) { x + 1, 8, 0, 0 };
                PSDL_BlitMirrored(src, NULL, d, &dr);
                dr = (SDL_Rect) { x, 11, 0, 0 };
                PSDL_BlitXor(src, NULL, d, &dr);
                dr = (SDL_Rect) { x + 2, 13, 0, 0 };
                if (k == 0)
                    PSDL_BlitMono(src, NULL, d, &dr, 0x5Au);
            }
        }
    }
    SDL_FreeSurface(d);
    SDL_FreeSurface(s8);
    SDL_FreeSurface(nib);
    arv_puts("prince: gpu selftest done\n");
}
#endif

void psdl_backend_video_sync(void)
{
#ifdef SIMULATION
    const uint32_t t0 = arv_mtime();
#endif
    blns_gpu_wait(&arv_blns);
    gpu_queued = false;
    frame_flip();
#ifdef SIMULATION
    sync_ticks += arv_mtime() - t0;
#endif
}

#ifdef SIMULATION
static void gpu_report(void)
{
#ifdef PSDL_GPU_CHECK
    extern unsigned psdl_gpu_check_n[3], psdl_gpu_check_bad[3], psdl_gpu_check_mode[3][8];
    arv_printf("prince: gpu check nib %u/%u blit %u/%u fill %u/%u (checked/differ)\n",
               psdl_gpu_check_n[0], psdl_gpu_check_bad[0], psdl_gpu_check_n[1], psdl_gpu_check_bad[1],
               psdl_gpu_check_n[2], psdl_gpu_check_bad[2]);
    /* nib: keyed, mono, xor, opaque; blit: copy, keyed, xor; +4 mirrored */
    for (int k = 0; k < 2; k++)
        arv_printf("prince: gpu check %s modes %u %u %u %u | mirrored %u %u %u %u\n", k ? "blit" : "nib",
                   psdl_gpu_check_mode[k][0], psdl_gpu_check_mode[k][1], psdl_gpu_check_mode[k][2],
                   psdl_gpu_check_mode[k][3], psdl_gpu_check_mode[k][4], psdl_gpu_check_mode[k][5],
                   psdl_gpu_check_mode[k][6], psdl_gpu_check_mode[k][7]);
#endif
    void pak_cache_report(void);
    extern int font_pixels_used(void);
    pak_cache_report();
#ifdef ARV_PRESENT_COPY
    arv_printf("prince: display copy (one frame): %u copies after a refresh end; font %d bytes\n",
               (unsigned) scan_waits, font_pixels_used());
#else
    arv_printf("prince: display flip (two frames): %u flip waits, %u.%u ms; font %d bytes\n",
               (unsigned) flip_waits, (unsigned) (flip_ticks / (BOARD_MTIME_HZ / 1000u)),
               (unsigned) (flip_ticks / (BOARD_MTIME_HZ / 10000u) % 10u), font_pixels_used());
#endif
    /* waiting times in 0.1 ms of the machine timer (the real one, not the game's) */
    arv_printf("prince: gpu %u nib %u blit %u fill, %u waits; waited %u.%u ms for presents, %u.%u ms for the gpu\n",
               (unsigned) gpu_nibs, (unsigned) gpu_blits, (unsigned) gpu_fills, (unsigned) gpu_waits,
               (unsigned) (sync_ticks / (BOARD_MTIME_HZ / 1000u)),
               (unsigned) (sync_ticks / (BOARD_MTIME_HZ / 10000u) % 10u),
               (unsigned) (wait_ticks / (BOARD_MTIME_HZ / 1000u)),
               (unsigned) (wait_ticks / (BOARD_MTIME_HZ / 10000u) % 10u));
}
#endif
#else
static void frame_copy(unsigned x, unsigned y, unsigned w, unsigned h)
{
    for (unsigned r = y; r < y + h; r++)
        memcpy(arv_vram + VRAM_FRAME + r * FRAME_W + x, arv_vram + VRAM_BACK + r * FRAME_W + x, w);
}

void psdl_backend_video_sync(void) {}
void arv_gpu_sync(void) {}
void psdl_backend_cpu_touch(const void *src, const void *dst) { (void) src; (void) dst; }
int  psdl_backend_gpu_nib(const SDL_Surface *src, int sx, int sy, int dir, Uint8 *dp, int dpitch,
                          int w, int h, int mode, int keyed, Uint8 color)
{
    (void) src; (void) sx; (void) sy; (void) dir; (void) dp; (void) dpitch; (void) w; (void) h;
    (void) mode; (void) keyed; (void) color;
    return 0;
}
int  psdl_backend_gpu_blit(const Uint8 *sp, int spitch, int sdir, Uint8 *dp, int dpitch,
                           int w, int h, int mode, Uint8 key)
{
    (void) sp; (void) spitch; (void) sdir; (void) dp; (void) dpitch; (void) w; (void) h;
    (void) mode; (void) key;
    return 0;
}
int  psdl_backend_gpu_fill(Uint8 *dp, int dpitch, int w, int h, Uint8 v)
{
    (void) dp; (void) dpitch; (void) w; (void) h; (void) v;
    return 0;
}
#endif

#if defined(SIMULATION) && !defined(ARV_SOFT_GFX)
/* The measurement window: the 50 presents from the third after level 1's
 * start (the presents 50 to 100 of a level starting at present 47), the
 * counters at both ends */
static void sim_window(void)
{
    extern uint16_t current_level;
    static uint32_t level_at;
    if (!level_at && (int16_t) current_level == 1)
        level_at = arv_presents;
    if (level_at && (arv_presents == level_at + 2u || arv_presents == level_at + 52u)) {
        arv_printf("prince: window %s at present %u, %u ms\n", arv_presents == level_at + 2u ? "start" : "end",
                   (unsigned) arv_presents, (unsigned) (arv_mtime() / (BOARD_MTIME_HZ / 1000u)));
        gpu_report();
    }
}
#endif

void psdl_backend_video_present(const Uint8 *pixels, int w, int h, int pitch)
{
    (void) pixels; (void) w; (void) h; (void) pitch;
#ifdef PSDL_GPU_CHECK
    gpu_selftest();
#endif
    frame_copy(0, 0, FRAME_W, FRAME_H);
    arv_presents++;
#if defined(SIMULATION) && !defined(ARV_SOFT_GFX)
    if (arv_presents % 50u == 0u)
        gpu_report();
    sim_window();
#endif
    arv_board_poll();
}

void psdl_backend_video_present_rect(const Uint8 *pixels, int pitch, int x, int y, int w, int h)
{
    (void) pixels; (void) pitch;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
#ifdef PSDL_GPU_CHECK
    gpu_selftest();
#endif
    if (x + w > (int) FRAME_W) w = (int) FRAME_W - x;
    if (y + h > (int) FRAME_H) h = (int) FRAME_H - y;
    if (w > 0 && h > 0)
        frame_copy((unsigned) x, (unsigned) y, (unsigned) w, (unsigned) h);
    arv_presents++;
#if defined(SIMULATION) && !defined(ARV_SOFT_GFX)
    if (arv_presents % 50u == 0u)
        gpu_report();
    sim_window();
#endif
    arv_board_poll();
}
int  psdl_backend_video_buffer_busy(const void *pixels) { (void) pixels; return 0; }

void psdl_backend_palette_set(int first, int ncolors, const SDL_Color *colors)
{
    uint32_t rgb[256];
    if (first < 0 || ncolors <= 0 || first + ncolors > 256)
        return;
    for (int i = 0; i < ncolors; i++)
        rgb[i] = (uint32_t) colors[i].r << 16 | (uint32_t) colors[i].g << 8 | colors[i].b;
    arv_palette_set((unsigned) first, (unsigned) ncolors, rgb);
}

/*------------------------------- input --------------------------------------*/

void psdl_backend_input_init(void) {}

void psdl_backend_input_poll(void)
{
    static const struct { uint32_t pad; SDL_Scancode sc; } map[] = {
        { PAD_LEFT, SDL_SCANCODE_LEFT }, { PAD_RIGHT, SDL_SCANCODE_RIGHT },
        { PAD_UP, SDL_SCANCODE_UP },     { PAD_DOWN, SDL_SCANCODE_DOWN },
        { PAD_A, SDL_SCANCODE_LSHIFT },  { PAD_B, SDL_SCANCODE_UP },
        { PAD_Y, SDL_SCANCODE_DOWN },    { PAD_X, SDL_SCANCODE_SPACE },
        { PAD_START, SDL_SCANCODE_RETURN }, { PAD_SELECT, SDL_SCANCODE_ESCAPE },
    };
    static uint32_t pad_q;
    uint8_t usage, mods;
    bool down;
    uint32_t p;

    arv_input_poll();
    arv_music_pump();
    while (arv_key_event(&usage, &mods, &down)) {
        Uint16 mod = 0;
        if (mods & 0x11u) mod |= KMOD_CTRL;
        if (mods & 0x22u) mod |= KMOD_SHIFT;
        if (mods & 0x44u) mod |= KMOD_ALT;
        psdl_push_key((SDL_Scancode) usage, down, mod);
    }
    /* the modifier keys themselves (a HID keyboard reports them as bits) */
    {
        static uint8_t mods_q;
        const uint8_t m = arv_key_mods();
        static const SDL_Scancode msc[8] = {
            SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT, SDL_SCANCODE_LGUI,
            SDL_SCANCODE_RCTRL, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RALT, SDL_SCANCODE_RGUI };
        for (int i = 0; i < 8; i++)
            if ((m ^ mods_q) & (1u << i))
                psdl_push_key(msc[i], (m >> i) & 1u, 0);
        mods_q = m;
    }
    /* the gamepads: a key is down while any button mapped to it is */
    p = arv_pad();
    if (p != pad_q) {
        for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
            int now = 0, before = 0, first = 1;
            for (unsigned k = 0; k < sizeof(map) / sizeof(map[0]); k++)
                if (map[k].sc == map[i].sc) {
                    if (k < i) first = 0;
                    now |= (p & map[k].pad) != 0;
                    before |= (pad_q & map[k].pad) != 0;
                }
            if (first && now != before)
                psdl_push_key(map[i].sc, now, 0);
        }
        pad_q = p;
    }
}

/*------------------------------- time ---------------------------------------*/

int psdl_backend_cpu_load(int core) { (void) core; return 0; }

Uint32 psdl_backend_ticks_ms(void)
{
    static uint32_t last, ms, frac;
    const uint32_t now = arv_mtime(), hz = arv_time_hz() / 1000u;
    frac += now - last;
    last = now;
    ms += frac / hz;
    frac %= hz;
    return ms;
}

Uint64 psdl_backend_ticks_us(void) { return (Uint64) psdl_backend_ticks_ms() * 1000u; }

void psdl_backend_delay_ms(Uint32 ms)
{
    const Uint32 t0 = psdl_backend_ticks_ms();
    while (psdl_backend_ticks_ms() - t0 < ms)
        arv_music_pump();
}

/*------------------------------- audio --------------------------------------*/

void psdl_backend_audio_open(int freq, int channels, int block_frames)
{
    (void) channels; (void) block_frames;
    arv_audio_open((unsigned) freq);
}

void psdl_backend_audio_close(void) {}
void psdl_backend_audio_pause(int pause_on) { arv_audio_pause(pause_on); }
void psdl_backend_audio_lock(void)   { arv_audio_lock(); }
void psdl_backend_audio_unlock(void) { arv_audio_unlock(); }

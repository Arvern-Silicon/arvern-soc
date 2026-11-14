//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gfx.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Drawing and palette of the boot menu (see gfx.h).
//
// The palette is updated without waiting for a frame: once FRAME_NUM (the
// frames sent) has moved since an update, the LUT bank selected then is the
// one shown and the other may be rewritten.
//
// The stars: points in a box (x, y, z in Q12, z = 1 the far end), moving
// towards the viewer and projected at a focal length of 180 pixels; a star
// that passes the near end or leaves the screen starts again at the far end.
// Each frame erases what it drew the frame before (a pixel, a 2 x 2 square
// near the viewer, a streak in hyperspace), then draws again; the erase
// restores the gradient, which depends on the row only. In the drift the CPU
// plots them; in hyperspace (up to 46,000 pixels a frame) the GPU fills their
// runs, and the CPU drawing calls wait for it (gfx_sync).
//
// The rectangles, the text and the 2-bit coverage bitmaps (the wordmark) are
// the GPU's too, the same pixels as the CPU's: fills (the background one per
// band of the gradient), colour expansions with the source pushed through
// the command FIFO (the font and the bitmaps are in the program memory). The
// CPU drawing that is left waits for them (gfx_sync).
//----------------------------------------------------------------------------
#include <string.h>
#include "tang_board.h"
#include "gfx.h"
#include "font_mono.h"
#include "logo_data.h"

uint8_t *vram;

static const uint32_t menu_pal[] = {
    0x101418u, 0xE8E8E8u, 0x8494A4u, 0x2A58A8u, 0x1A3458u, 0xF0C040u, 0xF05050u, 0x40C060u,
    0x40C060u, 0x1A3458u, 0xE8E8E8u, 0xF05050u, 0x40C060u, 0xF0A030u, 0xC8D0D8u, 0x506070u,
};

#define BAR_LO       0x2A58A8u              // the selection's glow, between these
#define BAR_HI       0x4C84E0u
#define GRAD_TOP     0x050812u              // the background
#define GRAD_BOTTOM  0x0C1830u
#define STAR_DIM     0x24304Au
#define STAR_BRIGHT  0xF4F8FFu
#define GLASS_FILL   0x0B111Cu
#define GLASS_EDGE   0x2A3A58u
#define GLASS_HI     0x3E5680u

#ifdef SIMULATION
#define ANIMATE      false
#else
#define ANIMATE      true
#endif

static uint32_t out[256];
static unsigned anim_t;
static uint16_t bank_fn;                    // FRAME_NUM at the last bank flip
static uint32_t frames;
static bool     animate = ANIMATE, dirty = true;
static unsigned level, target, step;        // fade, 0 .. 256
static bool     keep;

void gfx_pal_init(void)
{
    dirty = true;
    memcpy(pal, menu_pal, sizeof(menu_pal));
    for (unsigned i = 0; i < GRAD_NR; i++)
        pal[PAL_GRAD + i] = rgb_lerp(GRAD_TOP, GRAD_BOTTOM, i, GRAD_NR - 1u);
    pal[C_PANEL] = GLASS_FILL;
    pal[C_EDGE]  = GLASS_EDGE;
    pal[C_HI]    = GLASS_HI;
    for (unsigned i = 0; i < STAR_SHADES; i++)
        pal[PAL_STAR + i] = rgb_lerp(STAR_DIM, STAR_BRIGHT, i, STAR_SHADES - 1u);
    for (unsigned i = 1; i < LOGO_PAL_NR; i++)
        pal[PAL_LOGO + i] = logo_pal_light[i];
    for (unsigned l = 1; l <= 3u; l++)
        pal[PAL_WORD + l] = rgb_lerp(menu_pal[C_BAND], 0xF2F2F2u, l, 3u);
}

void gfx_init(void)
{
    vram = (uint8_t *) blns_vram8(&blns);           // plain stores: VRAM is not cached
    // from black: the first frames fade in
    memset(out, 0, sizeof(out));
    blns_lut_load(&blns, 0u, 0, out, 256);
    blns_lut_load(&blns, 1u, 0, out, 256);
    level = target = 0u;
    blns_lut_select(&blns, true, 0u);
    bank_fn = blns_frame_num(&blns);
}

uint32_t gfx_frame_nr(void)
{
    return frames;
}

void gfx_animate(bool on)
{
    animate = on && ANIMATE;
}

void gfx_pal_changed(void)
{
    dirty = true;
}

void gfx_fade(unsigned lv, unsigned frames, bool k)
{
    target = (lv > 256u) ? 256u : lv;
    step   = ((target > level ? target - level : level - target) + frames) / (frames ? frames : 1u);
    keep   = k;
}

bool gfx_fading(void)
{
    return level != target;
}

static uint32_t dim(uint32_t c, unsigned lv)
{
    return ((((c & 0xFF00FFu) * lv) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * lv) >> 8) & 0x00FF00u);
}

static void animate_pal(void)
{
    const unsigned g = anim_t & 63u;
    out[C_BAR] = rgb_lerp(BAR_LO, BAR_HI, (g < 32u) ? g : 63u - g, 31u);
}

//==========================================================
// GLASS PANELS AND STARS
//==========================================================

typedef struct { uint16_t x0, y0, x1, y1; } area_t;

static area_t area[GL_NR];                  // x1 = 0: the slot is free

static bool kept_out(int x, int y)
{
    for (unsigned k = 0; k < GL_NR; k++)
        if (x >= area[k].x0 && x < area[k].x1 && y >= area[k].y0 && y < area[k].y1)
            return true;
    return false;
}

void gfx_keep_out(unsigned slot, unsigned x, unsigned y, unsigned w, unsigned h)
{
    area[slot] = (area_t) { (uint16_t) x, (uint16_t) y, (uint16_t) (x + w), (uint16_t) (y + h) };
}

void gfx_glass(unsigned slot, unsigned x, unsigned y, unsigned w, unsigned h)
{
    gfx_keep_out(slot, x, y, w, h);
    rect(x, y, w, h, C_PANEL);
    rect(x, y, w, 1u, C_HI);
    rect(x, y + h - 1u, w, 1u, C_EDGE);
    rect(x, y + 1u, 1u, h - 2u, C_EDGE);
    rect(x + w - 1u, y + 1u, 1u, h - 2u, C_EDGE);
}

void gfx_glass_off(unsigned slot)
{
    const area_t a = area[slot];
    if (a.x1) {
        area[slot].x1 = 0u;
        rect(a.x0, a.y0, a.x1 - a.x0, a.y1 - a.y0, C_BGA);
    }
}

#define STAR_NR      480u
#define STAR_ONE     4096                   // 1.0 in Q12
#define STAR_FOCAL   180
#define STAR_NEAR    80                     // z where a star starts again
#define STAR_DRIFT   7                      // z per frame
#define STAR_JUMP    123                    // more in hyperspace
#define STREAK_MAX   48                     // longest streak, pixels

enum { S_NONE, S_DOT, S_BIG, S_LINE };

typedef struct {
    int16_t  x, y;
    uint16_t z;
    int16_t  ax, ay, bx, by;                // what was drawn: at a (dot, square), a to b (streak)
    uint8_t  kind, c;
} star_t;

static star_t   star[STAR_NR];
static bool     stars_on, warp;
static unsigned warp_lv;                    // 0 .. 16: the jump's speed
static uint32_t rng = 0x2545F491u;

static int32_t rnd(int32_t lo, int32_t hi)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return lo + (int32_t) (rng % (uint32_t) (hi - lo + 1));
}

static void star_new(star_t *s, bool far)
{
    s->x = (int16_t) rnd(-STAR_ONE * 3 / 2, STAR_ONE * 3 / 2);
    s->y = (int16_t) rnd(-STAR_ONE * 9 / 10, STAR_ONE * 9 / 10);
    s->z = (uint16_t) (far ? STAR_ONE : rnd(STAR_NEAR * 4, STAR_ONE));
}

static inline void plot(int x, int y, uint8_t c, bool erase)
{
    if (x < 0 || y < 0 || x >= (int) SCR_W || y >= (int) SCR_H || kept_out(x, y))
        return;
    vram[y * SCR_W + x] = erase ? bg_at((unsigned) y) : c;
}

static void line(int x0, int y0, int x1, int y1, uint8_t c, bool erase)
{
    const int dx = (x1 > x0) ? x1 - x0 : x0 - x1, dy = (y1 > y0) ? y0 - y1 : y1 - y0;
    const int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        plot(x0, y0, c, erase);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

// In hyperspace the GPU draws the stars (the same pixels): one x/y fill per
// run of a row (a dot, a row of a square, a streak's part of a row), cut
// around the glass panels, in the star's colour or, to erase, the row's
// background. The CPU drawing calls wait for that work first (gfx_sync), and
// for the GPU's rectangles, text and bitmaps.
static bool gpu_queued;                     // GPU work queued since the last wait

void gfx_sync(void)
{
    if (gpu_queued) {
        blns_gpu_wait(&blns);
        gpu_queued = false;
    }
}

// Pixels xa..xb of row y outside the areas k..GL_NR-1
static void run_cut(int xa, int xb, int y, unsigned k)
{
    for (; k < GL_NR; k++) {
        const area_t *a = &area[k];
        if (y < (int) a->y0 || y >= (int) a->y1 || xb < (int) a->x0 || xa >= (int) a->x1)
            continue;
        if (xa < (int) a->x0)
            run_cut(xa, (int) a->x0 - 1, y, k + 1u);
        if (xb >= (int) a->x1)
            run_cut((int) a->x1, xb, y, k + 1u);
        return;
    }
    blns_gpu_fill_xy(&blns, xa, y, (uint16_t) (xb - xa + 1), 1u);
}

// As plot() for every pixel of xa..xb (either order) of row y
static void run_gpu(int xa, int xb, int y, uint8_t c, bool erase)
{
    if (xa > xb) {
        const int t = xa;
        xa = xb;
        xb = t;
    }
    if (y < 0 || y >= (int) SCR_H || xb < 0 || xa >= (int) SCR_W)
        return;
    if (xa < 0)
        xa = 0;
    if (xb >= (int) SCR_W)
        xb = (int) SCR_W - 1;
    blns_gpu_colour_set(&blns, erase ? bg_at((unsigned) y) : c);
    run_cut(xa, xb, y, 0u);
}

// line() as runs: the pixels of each row it crosses
static void line_gpu(int x0, int y0, int x1, int y1, uint8_t c, bool erase)
{
    const int dx = (x1 > x0) ? x1 - x0 : x0 - x1, dy = (y1 > y0) ? y0 - y1 : y1 - y0;
    const int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int e = dx + dy, rx = x0;               // rx: the row's first pixel
    for (;;) {
        const bool end = x0 == x1 && y0 == y1;
        const int  xs = x0, ys = y0, e2 = 2 * e;
        if (!end) {
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
        if (end || y0 != ys) {
            run_gpu(rx, xs, ys, c, erase);
            rx = x0;
        }
        if (end)
            break;
    }
}

static void star_draw(const star_t *s, bool erase, bool gpu)
{
    switch (s->kind) {
    case S_DOT:
        if (gpu)
            run_gpu(s->ax, s->ax, s->ay, s->c, erase);
        else
            plot(s->ax, s->ay, s->c, erase);
        break;
    case S_BIG:
        if (gpu) {
            run_gpu(s->ax, s->ax + 1, s->ay, s->c, erase);
            run_gpu(s->ax, s->ax + 1, s->ay + 1, s->c, erase);
            break;
        }
        plot(s->ax, s->ay, s->c, erase);
        plot(s->ax + 1, s->ay, s->c, erase);
        plot(s->ax, s->ay + 1, s->c, erase);
        plot(s->ax + 1, s->ay + 1, s->c, erase);
        break;
    case S_LINE:
        if (gpu)
            line_gpu(s->ax, s->ay, s->bx, s->by, s->c, erase);
        else
            line(s->ax, s->ay, s->bx, s->by, s->c, erase);
        break;
    default:
        break;
    }
}

// Whose stars: the GPU's in hyperspace (and on the way in and out of it),
// the CPU's in the drift (a pixel store beats a GPU command)
static bool stars_by_gpu(void)
{
    const bool gpu = warp || warp_lv > 0u;
    if (gpu) {
        blns_gpu_xy_begin(&blns, blns_vram_pa(&blns, vram), SCR_W, 0u);
        blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
        gpu_queued = true;
    } else {
        gfx_sync();
    }
    return gpu;
}

static void stars_step(void)
{
    if (warp ? warp_lv < 16u : warp_lv > 0u)
        warp_lv += warp ? 1u : (unsigned) -1;
    const unsigned v = STAR_DRIFT + STAR_JUMP * warp_lv / 16u;
    const bool     gpu = stars_by_gpu();

    for (unsigned i = 0; i < STAR_NR; i++)
        star_draw(&star[i], true, gpu);
    for (unsigned i = 0; i < STAR_NR; i++) {
        star_t *s = &star[i];
        const int32_t z0 = s->z;
        s->kind = S_NONE;
        if (z0 <= (int32_t) (STAR_NEAR + v)) {
            star_new(s, true);
            continue;
        }
        s->z = (uint16_t) (z0 - (int32_t) v);
        const int32_t x = (int32_t) SCR_W / 2 + s->x * STAR_FOCAL / s->z;
        const int32_t y = (int32_t) SCR_H / 2 + s->y * STAR_FOCAL / s->z;
        if (x < 0 || y < 0 || x >= (int32_t) SCR_W || y >= (int32_t) SCR_H) {
            star_new(s, true);
            continue;
        }
        const int32_t c = (STAR_ONE - s->z) * (int32_t) STAR_SHADES * 17 / (16 * STAR_ONE);
        s->c  = (uint8_t) (PAL_STAR + ((c < (int32_t) STAR_SHADES) ? c : (int32_t) STAR_SHADES - 1));
        s->ax = (int16_t) x;
        s->ay = (int16_t) y;
        if (warp_lv > 1u) {
            int32_t px = (int32_t) SCR_W / 2 + s->x * STAR_FOCAL / z0;
            int32_t py = (int32_t) SCR_H / 2 + s->y * STAR_FOCAL / z0;
            const int32_t lx = (px > x) ? px - x : x - px, ly = (py > y) ? py - y : y - py;
            const int32_t len = (lx > ly) ? lx : ly;
            if (len > STREAK_MAX) {
                px = x + (px - x) * STREAK_MAX / len;
                py = y + (py - y) * STREAK_MAX / len;
            }
            s->bx   = (int16_t) px;
            s->by   = (int16_t) py;
            s->kind = S_LINE;
        } else {
            s->kind = (s->z < STAR_ONE * 3 / 10) ? S_BIG : S_DOT;
        }
        star_draw(s, false, gpu);
    }
}

void gfx_stars(bool on)
{
    if (on && !stars_on)
        for (unsigned i = 0; i < STAR_NR; i++) {
            star_new(&star[i], false);
            star[i].kind = S_NONE;
        }
    if (!on && stars_on) {
        const bool gpu = stars_by_gpu();
        for (unsigned i = 0; i < STAR_NR; i++)
            star_draw(&star[i], true, gpu);
    }
    stars_on = on;
    warp     = false;
    warp_lv  = 0u;
}

void gfx_warp(bool on)
{
    warp = on;
}

bool gfx_frame(void)
{
    if (blns_frame_num(&blns) == bank_fn || !(animate || dirty || level != target))
        return false;
    dirty = false;
    memcpy(out, pal, sizeof(out));
    if (animate) {
        anim_t++;
        animate_pal();
        if (stars_on)
            stars_step();
    }
    if (level != target)
        level = (level < target) ? ((target - level > step) ? level + step : target)
                                 : ((level - target > step) ? level - step : target);
    if (level < 256u)
        for (unsigned i = 0; i < 256u; i++)
            if (!keep || i < C_LOAD_DONE || i > C_LOAD_ERR)
                out[i] = dim(out[i], level);
    blns_lut_present(&blns, out, 256);          // the hidden bank, then selected
    bank_fn = blns_frame_num(&blns);
    frames++;
    return true;
}

//==========================================================
// DRAWING (GPU, else CPU; 8 bpp; text at x a multiple of 4)
//==========================================================

// The rectangle as GPU fills: one, or for the background one per band of the
// gradient (rows of the same bg_at); false when the GPU refuses one (the CPU
// then draws the whole rectangle)
static bool rect_gpu(unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c)
{
    for (unsigned r = 0, n; r < h && w; r += n) {
        const uint8_t v = (c == C_BGA) ? bg_at(y + r) : c;
        n = 1u;
        while (r + n < h && (c != C_BGA || bg_at(y + r + n) == v))
            n++;
        const blns_surf_t d = { blns_vram_pa(&blns, vram + (y + r) * SCR_W + x), SCR_W, BLNS_SWAP_NONE };
        if (blns_gpu_fill(&blns, &d, (uint16_t) w, (uint16_t) n, v, BLNS_ROP_S) != BLNS_OK)
            return false;
        gpu_queued = true;
    }
    return true;
}

void rect(unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c)
{
    if (rect_gpu(x, y, w, h, c))
        return;
    gfx_sync();
    for (unsigned r = 0; r < h; r++)
        memset(vram + (y + r) * SCR_W + x, (c == C_BGA) ? bg_at(y + r) : c, w);
}

// A 1/2-bit image pushed behind a colour expansion (BLNS_EXP_*) at (x, y):
// transparent, or opaque on bg; false when the GPU refuses it
static bool expand_imm(unsigned x, unsigned y, unsigned w, unsigned h, uint8_t depth,
                       bool opaque, uint8_t colour, uint8_t bg, const uint32_t *data)
{
    const blns_expand_t e = { .width = (uint16_t) w, .height = (uint16_t) h, .src_pitch = 0u,
                              .depth = depth, .opaque = opaque, .mask = false,
                              .swap = BLNS_SWAP_NONE, .rop = BLNS_ROP_S };
    const blns_pa_t d = blns_vram_pa(&blns, vram + y * SCR_W + x);
    if (blns_gpu_expand_begin(&blns, &e, d, SCR_W, colour, bg) != BLNS_OK ||
        blns_gpu_expand_imm(&blns, d, data) != BLNS_OK)
        return false;
    gpu_queued = true;
    return true;
}

// The 8 pixels of a glyph row, as two words: 0xFF per set pixel, LSB first
static const uint32_t nib_mask[16] = {
    0x00000000u, 0x000000FFu, 0x0000FF00u, 0x0000FFFFu, 0x00FF0000u, 0x00FF00FFu, 0x00FFFF00u, 0x00FFFFFFu,
    0xFF000000u, 0xFF0000FFu, 0xFF00FF00u, 0xFF00FFFFu, 0xFFFF0000u, 0xFFFF00FFu, 0xFFFFFF00u, 0xFFFFFFFFu,
};

static const uint8_t *glyph(char ch)
{
    const unsigned c = (unsigned char) ch;
    return &font_mono[((c >= 32u && c <= 126u) ? c - 32u : '?' - 32u) * FONT_H];
}

// The GPU's text: the glyphs side by side as one 1-bit image (a glyph row is
// a byte, its first pixel the least significant bit, as the GPU reads it),
// expanded opaque on bg, or transparent on the background bands (C_BGA) or
// on the pixels as they are (C_KEEP)
static bool text_gpu(unsigned x, unsigned y, const char *s, uint8_t fg, uint8_t bg, unsigned k)
{
    static uint32_t img[SCR_W / FONT_W * FONT_H / 4u];
    uint8_t *b = (uint8_t *) img;
    for (unsigned r = 0; r < FONT_H; r++)
        for (unsigned i = 0; i < k; i++)
            b[r * k + i] = glyph(s[i])[r];
    if (bg == C_BGA && !rect_gpu(x, y, k * FONT_W, FONT_H, C_BGA))
        return false;
    return expand_imm(x, y, k * FONT_W, FONT_H, BLNS_EXP_1BPP, bg != C_BGA && bg != C_KEEP, fg, bg, img);
}

// At most n characters of s at (x, y), opaque; returns the x after the text
unsigned text(unsigned x, unsigned y, const char *s, uint8_t fg, uint8_t bg, unsigned n)
{
    unsigned k = 0;
    while (s[k] && k < n && x + (k + 1u) * FONT_W <= SCR_W)
        k++;
    if (!k || text_gpu(x, y, s, fg, bg, k))
        return x + k * FONT_W;
    gfx_sync();
    const uint32_t f = fg * 0x01010101u;
    for (; *s && n && x + FONT_W <= SCR_W; s++, n--, x += FONT_W) {
        const uint8_t *g = glyph(*s);
        uint32_t *p = (uint32_t *) (vram + y * SCR_W + x);
        for (unsigned r = 0; r < FONT_H; r++, p += SCR_W / 4u) {
            const uint32_t b = ((bg == C_BGA) ? bg_at(y + r) : bg) * 0x01010101u;
            uint32_t m0 = nib_mask[g[r] & 0xFu], m1 = nib_mask[g[r] >> 4];
            p[0] = (m0 & f) | (~m0 & ((bg == C_KEEP) ? p[0] : b));
            p[1] = (m1 & f) | (~m1 & ((bg == C_KEEP) ? p[1] : b));
        }
    }
    return x;
}

// The small logo: runs of three bytes (y, x, length), one colour per layer
void draw_logo(unsigned x0, unsigned y0)
{
    gfx_sync();
    for (unsigned l = 0; l < SPRITE_LAYERS; l++)
        for (unsigned k = sprite_layer_start[l]; k < sprite_layer_start[l + 1u]; k++) {
            const uint8_t *r = &sprite_runs[3u * k];
            memset(vram + (y0 + r[0]) * SCR_W + x0 + r[1], PAL_LOGO + sprite_layer_idx[l], r[2]);
        }
}

// The GPU's 2-bit bitmap: the first 'cols' columns of each row packed one
// row after the other (the GPU's order: four pixels a byte, the first in
// [1:0]), expanded transparent from colour ramp + 1 (value v: ramp + v)
static bool bits2_gpu(const uint8_t *bits, unsigned h, unsigned stride, unsigned cols,
                      unsigned x0, unsigned y0, uint8_t ramp)
{
    static uint32_t img[320];
    const unsigned rb = 2u * cols;          // bits a row
    uint32_t acc = 0u;
    unsigned nb = 0u, o = 0u;               // bits in acc, words out
    if ((rb * h + 31u) / 32u > sizeof(img) / sizeof(img[0]))
        return false;
    for (unsigned y = 0; y < h; y++)
        for (unsigned i = 0; i < rb; i += 8u) {
            const unsigned k = (rb - i < 8u) ? rb - i : 8u;
            const uint32_t v = bits[y * stride + i / 8u] & ((1u << k) - 1u);
            acc |= v << nb;
            if (nb + k >= 32u) {
                img[o++] = acc;
                acc = (nb + k > 32u) ? v >> (32u - nb) : 0u;
                nb  = nb + k - 32u;
            } else {
                nb += k;
            }
        }
    if (nb)
        img[o] = acc;
    return expand_imm(x0, y0, cols, h, BLNS_EXP_2BPP, false, (uint8_t) (ramp + 1u), 0u, img);
}

// A 2-bit coverage bitmap (four pixels a byte, the leftmost in [1:0]): the
// set pixels in colours ramp + 1 .. ramp + 3, the first 'cols' columns
void draw_bits2(const uint8_t *bits, unsigned w, unsigned h, unsigned stride, unsigned cols,
                unsigned x0, unsigned y0, uint8_t ramp)
{
    if (cols > w)
        cols = w;
    if (!cols || !h || bits2_gpu(bits, h, stride, cols, x0, y0, ramp))
        return;
    gfx_sync();
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = &bits[y * stride];
        uint8_t       *p   = vram + (y0 + y) * SCR_W + x0;
        for (unsigned x = 0; x < cols; x++) {
            unsigned v = (row[x / 4u] >> (2u * (x % 4u))) & 3u;
            if (v)
                p[x] = (uint8_t) (ramp + v);
        }
    }
}

void draw_wordmark(unsigned x0, unsigned y0, uint8_t ramp, unsigned cols)
{
    draw_bits2(wordmark_bits, WORDMARK_W, WORDMARK_H, WORDMARK_STRIDE, cols, x0, y0, ramp);
}

// A preview for an entry without a picture: soft blobs (three sine waves
// summed) in a ramp of one hue, LUT entries PAL_IMG..; seed picks the hue and
// the waves' phases
void draw_blobs_pal(uint32_t seed, unsigned first, unsigned n)
{
    static const uint32_t hue[8] = { 0xC06030u, 0x30A0C0u, 0x4070E0u, 0xD0A030u,
                                     0x60C080u, 0xB050D0u, 0xE05080u, 0x50B0A0u };
    const uint32_t c = hue[seed & 7u];
    for (unsigned i = first; i < first + n && i < PAL_IMG_NR; i++)
        pal[PAL_IMG + i] = rgb_lerp(0x080810u, c, i, PAL_IMG_NR - 1u);
    if (first + n >= PAL_IMG_NR)
        gfx_pal_changed();
}

void draw_blobs_rows(unsigned x0, unsigned y0, unsigned w, uint32_t seed, unsigned ya, unsigned yb)
{
    gfx_sync();
    static int8_t  sn[256];
    static uint8_t lv[763];                 // the sum of three sines (-381 .. 381) -> its LUT entry
    if (!sn[64]) {                          // a sine, 256 steps a turn (a parabola per half)
        for (int i = 0; i < 256; i++) {
            const int x = (i < 128) ? i : i - 128, y = x * (128 - x) * 127 / 4096;
            sn[i] = (int8_t) ((i < 128) ? y : -y);
        }
        for (unsigned i = 0; i < sizeof(lv); i++)
            lv[i] = (uint8_t) (PAL_IMG + i * (PAL_IMG_NR - 1u) / 762u);
    }
    const unsigned p1 = (seed >> 3) & 255u, p2 = (seed >> 11) & 255u, p3 = (seed >> 19) & 255u;
    for (unsigned y = ya; y < yb; y++) {
        uint8_t *v = vram + (y0 + y) * SCR_W + x0;
        const int sy = sn[(y * 85u / 16u + p2) & 255u] + 381;
        // the phases x * 59 / 16 and (x + y) * 33 / 16 by steps of 59 and 33
        // (no multiply per pixel: about 10 cycles a pixel)
        unsigned a = p1 * 16u, b = y * 33u + p3 * 16u;
        for (unsigned x = 0; x < w; x++, a += 59u, b += 33u)
            v[x] = lv[sn[(a >> 4) & 255u] + sy + sn[(b >> 4) & 255u]];
    }
}

void draw_blobs(unsigned x0, unsigned y0, unsigned w, unsigned h, uint32_t seed)
{
    draw_blobs_pal(seed, 0u, PAL_IMG_NR);
    draw_blobs_rows(x0, y0, w, seed, 0u, h);
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K Mode-7 demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Rotation and scaling on Belenos: every screen row is one EXEC_TEXSPAN that
// walks a 256 x 256 texture (power of two: it tiles for free) with its own
// start point and step.
//
//   1 flight      a Mode-7 ground plane under a moving camera, with scaled
//                 logo sprites standing on it (transparent spans)
//   2 rotozoom    the same texture turning and zooming over the whole screen
//
// The ground texture fills the VRAM of frame 1, so the picture is drawn in
// frame 0 only, in the gap after each refresh, ahead of the panel scan. The
// scan therefore stays top-down: on the upside-down panel (SCR_SWAP) the
// picture is drawn turned by 180 degrees instead (rows mirrored, texture
// steps negated). Each frame's commands are recorded into a command list while
// the GPU executes the previous one, and replayed with one CALL at the frame
// end (two lists in turn). The logo's circuit traces cycle through LUT_ROT.
//----------------------------------------------------------------------------
#define LOGO_DATA_TABLES
#include "demo_kit.h"
#include "logo_data.h"
#include "music_data.h"

const mus_tune_t *const *const demo_tunes    = music_tunes;
const unsigned                 demo_tunes_nr = sizeof(music_tunes) / sizeof(music_tunes[0]);

#define TEX              F1                    // ground texture, 256 x 256, pitch 256
#define TEX_L2           8u
#define LIST_WORDS       2176u                 // two command lists, used in turn
#define LIST_A           (TEX + 65536u)        // byte addresses
#define SPR              (LIST_A + 4u * LIST_WORDS)   // logo sprite, 64 x 64, pitch 64
#define LIST_B           (SPR + 4096u)         // all within frame 1's area
#define SPR_TRANSP       255u
_Static_assert(LIST_B + 4u * LIST_WORDS <= VRAM_SPARE, "texture, sprite and lists exceed frame 1");
#define Q16              65536

// Drawn turned by 180 degrees (the panel is upside down, see demo_kit.h)
#define ROT              (SCR_SWAP == (BLNS_SWAP_X | BLNS_SWAP_Y))
#define ROW(y)           (F0 + (uint32_t) (ROT ? ((int) SCR_H - 1 - (y)) : (y)) * SCR_W)

// Palette: 0..43 the logo; then the ground and the sky
#define C_GRASS          64u                   // 8 shades
#define C_ROAD           72u
#define C_LINE           73u
#define C_WATER          74u                   // 3 shades
#define C_PAD            77u
#define C_HAZE           78u
#define C_SKY            96u                   // 32 bands

static int16_t  sin_q14[256];
static uint32_t rnd = 0x2545F491u;

static uint32_t rand32(void)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

// Angles: 65536 per turn; the 256-entry table is interpolated so that slow turns move
// every frame
static int32_t isin(uint32_t a)
{
    const unsigned i  = (a >> 8) & 255u;
    const int32_t  s0 = sin_q14[i], s1 = sin_q14[(i + 1u) & 255u];
    return s0 + (((s1 - s0) * (int32_t) (a & 255u)) >> 8);
}
static int32_t icos(uint32_t a) { return isin(a + 16384u); }

__attribute__((optimize("Os"), noinline)) static void sin_init(void)
{
    const int64_t two_cos = 2LL * 1073418433;            // 2 cos(2 pi / 256), Q30
    int64_t s0 = 0, s1 = 26350943;

    for (unsigned n = 0; n <= 64u; n++) {
        int16_t v = (int16_t) ((s0 + (1 << 15)) >> 16);
        sin_q14[n] = v;
        sin_q14[128u - n] = v;
        sin_q14[(128u + n) & 255u] = (int16_t) -v;
        sin_q14[(256u - n) & 255u] = (int16_t) -v;
        int64_t s2 = ((two_cos * s1) >> 30) - s0;
        s0 = s1;
        s1 = s2;
    }
}

// Q16 step -> 5.11, rounded to the nearest: the GPU adds the step once a
// pixel or a row, so a truncated step drifts one way by up to 1/2048 texel
// each time, a rounded one by half that either way
static int16_t step_511(int32_t d_q16)
{
    int32_t d = (int32_t) (((int64_t) d_q16 + 16) >> 5);
    return (int16_t) ((d > 0x7FFF) ? 0x7FFF : (d < -0x8000) ? -0x8000 : d);
}

//==========================================================
// SET-UP: palette, ground texture, sprite
//==========================================================
__attribute__((optimize("Os"), noinline)) static void build_palette(void)
{
    for (unsigned i = 1; i < LOGO_PAL_NR; i++)
        pal[i] = logo_pal_light[i];
    for (unsigned i = 0; i < 8u; i++)
        pal[C_GRASS + i] = rgb_lerp(0x2a5a1eu, 0x4f8a2cu, i, 7);
    pal[C_ROAD]  = 0x55534eu;
    pal[C_LINE]  = 0xe8e0c0u;
    pal[C_WATER] = 0x1f4f8au;
    pal[C_WATER + 1u] = 0x2f6fb0u;
    pal[C_WATER + 2u] = 0x5a9ad0u;
    pal[C_PAD]   = 0x14181eu;
    pal[C_HAZE]  = 0x9cc0e0u;
    for (unsigned i = 0; i < 32u; i++)
        pal[C_SKY + i] = rgb_lerp(0x1a3a80u, 0x9cc0e0u, i, 31);
}

static void tfill(uint32_t base, unsigned pitch, unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c)
{
    const blns_surf_t d = { base + y * pitch + x, (uint16_t) pitch, BLNS_SWAP_NONE };
    blns_gpu_fill(&blns, &d, (uint16_t) w, (uint16_t) h, c, BLNS_ROP_S);
}

static void runs(const uint8_t *r, const uint16_t *start, const uint8_t *idx, unsigned layers,
                 uint32_t base, unsigned pitch)
{
    for (unsigned l = 0; l < layers; l++) {
        if (start[l] == start[l + 1u])
            continue;
        blns_gpu_span_begin(&blns, base + r[3u * start[l]] * pitch + r[3u * start[l] + 1u],
                            (uint16_t) pitch, idx[l]);
        for (unsigned k = start[l]; k < start[l + 1u]; ) {     // the layer's runs, in batches
            blns_span_t sp[16];
            unsigned    n = 0;
            for (; k < start[l + 1u] && n < 16u; k++, n++)
                sp[n] = (blns_span_t) { base + r[3u * k] * pitch + r[3u * k + 1u], r[3u * k + 2u] };
            blns_gpu_spans(&blns, sp, n);
        }
    }
}

__attribute__((optimize("Os"), noinline)) static void build_texture(void)
{
    // grass in 16 x 16 fields of random shades, speckled
    for (unsigned y = 0; y < 256u; y += 16u)
        for (unsigned x = 0; x < 256u; x += 16u)
            tfill(TEX, 256, x, y, 16, 16, (uint8_t) (C_GRASS + (rand32() & 7u)));
    blns_gpu_wait(&blns);
    volatile uint8_t *t = blns_vram8(&blns) + TEX;
    for (unsigned n = 0; n < 3000u; n++)
        t[rand32() & 0xFFFFu] = (uint8_t) (C_GRASS + (rand32() & 7u));
    // a pond
    for (int y = -20; y <= 20; y++) {
        int w = 0;
        while ((w + 1) * (w + 1) + y * y <= 400)
            w++;
        tfill(TEX, 256, (unsigned) (40 - w), (unsigned) (210 + y), (unsigned) (2 * w + 1), 1,
              (uint8_t) (C_WATER + ((unsigned) (y + 20) / 14u)));
    }
    // roads crossing the tile (they join across the tile edges), dashed lines
    tfill(TEX, 256, 0, 20, 256, 12, C_ROAD);
    tfill(TEX, 256, 236, 0, 12, 256, C_ROAD);
    for (unsigned k = 0; k < 256u; k += 16u) {
        tfill(TEX, 256, k, 25, 8, 2, C_LINE);
        tfill(TEX, 256, 241, k, 2, 8, C_LINE);
    }
    // the logo on a dark pad
    tfill(TEX, 256, 22, 50, 212, 152, C_PAD);
    runs(logo_runs, logo_layer_start, logo_layer_idx, LOGO_LAYERS, TEX + 56u * 256u + 28u, 256);
    // the sprite: 64 x 64, the logo in rows 9..53, transparent around
    tfill(SPR, 64, 0, 0, 64, 64, SPR_TRANSP);
    runs(sprite_runs, sprite_layer_start, sprite_layer_idx, SPRITE_LAYERS, SPR + 9u * 64u, 64);
    blns_gpu_wait(&blns);
}

//==========================================================
// COMMAND LISTS
//==========================================================
static blns_gpu_list_t lists[2] = { { .addr = LIST_A / 4u, .capacity = LIST_WORDS },
                                { .addr = LIST_B / 4u, .capacity = LIST_WORDS } };
static unsigned    list_cur;

// Record the frame into the next list (once the GPU is out of it), wait for the
// end of the frame being sent, then run it; the part of a frame too large for
// the list is pushed through the FIFO after it
static bool frame_list(void (*draw)(const void *), const void *ctx)
{
    blns_gpu_list_t *l = &lists[list_cur];

    blns_gpu_list_wait(&blns, l);                 // the GPU is done with its last call
    blns_gpu_list_begin(&blns, l, 0u);
    draw(ctx);
    bool ok  = (blns_gpu_list_end(&blns) == BLNS_OK);
    bool hit = scene_frame();
    blns_gpu_list_call(&blns, l, false);
    if (!ok) {
        blns_gpu_list_spill(&blns, l);
        draw(ctx);
    }
    list_cur ^= 1u;
    return hit;
}

//==========================================================
// 1: FLIGHT
//==========================================================
// Sky bands of SKY_BAND rows down to the horizon; the focal length is two
// thirds of the screen height, so the ground keeps its look downwards and the
// view widens with the screen (90 degrees at 4:3, 106 at 480 x 272)
#define SKY_BAND         ((int) (SCR_H * 3u / 80u))
#define HORIZON          (8 * SKY_BAND)        // screen row of the horizon
#define FOCAL            ((int) (SCR_H * 2u / 3u))   // pixels
#define SPR_NR           6u
#define SPR_SIZE         40                    // sprite width in texels
#define SPR_FAR          512                   // farthest sprite drawn, texels
#define SPR_MAX          48u

static const int16_t spr_pos[SPR_NR][2] = {
    { 128, 20 }, { 60, 100 }, { 200, 140 }, { 120, 236 }, { 30, 170 }, { 230, 60 },
};

// The sky: 8 bands, +4 palette entries each: one gradient fill
static void sky(void)
{
    if (ROT) {
        blns_gpu_gradient(&blns, true, (uint16_t) SKY_BAND, false, -4);
        fill(ROW(HORIZON - 1), SCR_W, (unsigned) HORIZON, (uint8_t) (C_SKY + 4u * (HORIZON / SKY_BAND - 1)));
    } else {
        blns_gpu_gradient(&blns, true, (uint16_t) SKY_BAND, false, 4);
        fill(F0, SCR_W, (unsigned) HORIZON, (uint8_t) C_SKY);
    }
    blns_gpu_gradient(&blns, false, 0, false, 0);
}

// The ground: row y at distance hcam * FOCAL / (y - HORIZON); its texels step
// by (hcam / dy) along the camera's right vector. VRAM rows [m0, m1) of it.
static void ground(int32_t cu, int32_t cv, uint32_t a, int32_t hcam, int m0, int m1)
{
    const int32_t fx = icos(a), fy = isin(a);            // forward, Q14
    const int32_t rx = -fy,    ry = fx;                  // right
    const blns_tex_t tex = { .addr = TEX, .log2_w = TEX_L2, .log2_h = TEX_L2, .swap = BLNS_SWAP_NONE };
    const int g0 = ROT ? 0 : HORIZON + 4, g1 = ROT ? (int) SCR_H - HORIZON - 4 : (int) SCR_H;

    if (m0 < g0)
        m0 = g0;
    if (m1 > g1)
        m1 = g1;
    if (m0 >= m1)
        return;
    blns_gpu_tex_begin(&blns, &tex, F0, 0, 0);
    for (int m = m0; m < m1; m++) {
        int     y  = ROT ? (int) SCR_H - 1 - m : m;      // screen row
        int32_t zf = hcam / (y - HORIZON);               // texels per pixel, Q16
        int32_t du = (int32_t) (((int64_t) zf * rx) >> 14);
        int32_t dv = (int32_t) (((int64_t) zf * ry) >> 14);
        int64_t z  = (int64_t) zf * FOCAL;               // distance, Q16
        int64_t u  = cu + ((z * fx) >> 14) - (int64_t) du * (SCR_W / 2u);
        int64_t v  = cv + ((z * fy) >> 14) - (int64_t) dv * (SCR_W / 2u);
        if (ROT) {                                       // from the row's last pixel
            u += (int64_t) du * (SCR_W - 1u);
            v += (int64_t) dv * (SCR_W - 1u);
            du = -du;
            dv = -dv;
        }
        blns_gpu_tex_step(&blns, step_511(du), step_511(dv));
        blns_gpu_tex_span(&blns, F0 + (uint32_t) m * SCR_W, SCR_W, (uint16_t) (u >> 10), (uint16_t) (v >> 10));
    }
}

// Logo sprites standing on the ground, far to near: every tiled copy within
// SPR_FAR texels, so that they appear small near the horizon. Placed first as
// rectangles of VRAM (trimmed to the screen in software: a clipped pixel would
// still cost its cycle), then drawn band by band with the ground: one affine
// blit (EXEC_TEXRECT) per sprite and band
typedef struct {
    int16_t x, y, w, h;                                  // VRAM rectangle
    int32_t u0, v0, du, dvr;                             // texel of its first pixel, steps (Q16)
} spr_rect_t;

static spr_rect_t spr_rect[SPR_MAX];
static unsigned   spr_n;

static void sprites_place(int32_t cu, int32_t cv, uint32_t a, int32_t hcam)
{
    const int32_t fx = icos(a), fy = isin(a);
    int32_t z[SPR_MAX], x[SPR_MAX];
    uint8_t order[SPR_MAX];
    unsigned n = 0;

    for (unsigned i = 0; i < SPR_NR; i++) {
        const int32_t dx0 = (((int32_t) spr_pos[i][0] * Q16 - cu) << 8) >> 8;   // wrap to +-128 texels
        const int32_t dy0 = (((int32_t) spr_pos[i][1] * Q16 - cv) << 8) >> 8;
        for (int ty = -2; ty <= 2; ty++) {
            for (int tx = -2; tx <= 2; tx++) {
                const int32_t dx = dx0 + tx * 256 * Q16, dy = dy0 + ty * 256 * Q16;
                const int32_t zi = (int32_t) (((int64_t) dx * fx + (int64_t) dy * fy) >> 14);
                const int32_t xi = (int32_t) (((int64_t) -dx * fy + (int64_t) dy * fx) >> 14);
                if (zi < 8 * Q16 || zi > SPR_FAR * Q16 || n == SPR_MAX)
                    continue;
                if ((int64_t) (xi < 0 ? -xi : xi) * FOCAL >                  // outside the view
                    (int64_t) (zi + SPR_SIZE * Q16) * (int) (SCR_W / 2u))
                    continue;
                unsigned j = n++;
                while (j > 0u && z[order[j - 1u]] < zi) {
                    order[j] = order[j - 1u];
                    j--;
                }
                z[n - 1u] = zi;
                x[n - 1u] = xi;
                order[j] = (uint8_t) (n - 1u);
            }
        }
    }
    spr_n = 0;
    for (unsigned k = 0; k < n; k++) {
        unsigned i  = order[k];
        int      w  = (int) ((int64_t) SPR_SIZE * FOCAL * Q16 / z[i]);
        int      h  = w * 45 / 64;
        int      sx = (int) (SCR_W / 2u) + (int) ((int64_t) x[i] * FOCAL / z[i]) - w / 2;
        int      sy = HORIZON + (int) ((int64_t) hcam * FOCAL / z[i]) - h;
        if (w < 2 || h < 2 || sx >= (int) SCR_W || sx + w <= 0 || sy >= (int) SCR_H || sy + h <= 0)
            continue;
        int32_t  du = (64 * Q16) / w, dvr = (45 * Q16) / h;
        int      xa = (sx < 0) ? 0 : sx, xb = (sx + w > (int) SCR_W) ? (int) SCR_W : sx + w;
        int      ya = (sy < 0) ? 0 : sy, yb = (sy + h > (int) SCR_H) ? (int) SCR_H : sy + h;
        int32_t  u0 = (xa - sx) * du;
        int32_t  v0 = 9 * Q16 + (ya - sy) * dvr;
        if (ROT) {                                       // mirrored, from the far corner
            u0 += (xb - xa - 1) * du;
            v0 += (yb - ya - 1) * dvr;
            du  = -du;
            dvr = -dvr;
            int xr = (int) SCR_W - xb, yr = (int) SCR_H - yb;
            xb = xr + (xb - xa);
            yb = yr + (yb - ya);
            xa = xr;
            ya = yr;
        }
        spr_rect[spr_n++] = (spr_rect_t) { (int16_t) xa, (int16_t) ya, (int16_t) (xb - xa), (int16_t) (yb - ya),
                                           u0, v0, du, dvr };
    }
}

// The sprites' parts in VRAM rows [m0, m1), far to near
static void sprites_band(int m0, int m1)
{
    const blns_tex_t tex = { .addr = SPR, .log2_w = 6, .log2_h = 6, .transp = true,
                             .transparent = SPR_TRANSP, .swap = BLNS_SWAP_NONE };
    bool started = false;

    for (unsigned k = 0; k < spr_n; k++) {
        const spr_rect_t *r = &spr_rect[k];
        int r0 = (r->y > m0) ? r->y : m0, r1 = (r->y + r->h < m1) ? r->y + r->h : m1;
        if (r0 >= r1)
            continue;
        if (!started) {
            blns_gpu_xy_begin(&blns, F0, 0, 0);
            blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
            blns_gpu_tex_set(&blns, &tex, 0);
            started = true;
        }
        int32_t v0 = r->v0 + (r0 - r->y) * r->dvr;
        blns_gpu_texrect_xy(&blns, r->x, r0, (uint16_t) r->w, (uint16_t) (r1 - r0),
                            (uint16_t) (r->u0 >> 10), (uint16_t) (v0 >> 10), step_511(r->du), 0, 0,
                            step_511(r->dvr));
    }
}

typedef struct {
    int32_t  cu, cv;
    uint32_t a;
    int32_t  hcam;
} flight_t;

// In VRAM row order, ahead of the panel scan: the sky and the haze (cheap
// fills), then bands of the ground with the sprites' parts over them
#define BAND             32

static void flight_draw(const void *ctx)
{
    const flight_t *v = ctx;

    sprites_place(v->cu, v->cv, v->a, v->hcam);
    sky();
    fill(ROW(ROT ? HORIZON + 3 : HORIZON), SCR_W, 4, C_HAZE);
    for (int m0 = 0; m0 < (int) SCR_H; m0 += BAND) {
        ground(v->cu, v->cv, v->a, v->hcam, m0, m0 + BAND);
        sprites_band(m0, m0 + BAND);
    }
}

static void scene_flight(void)
{
    int32_t  cu = 128 * Q16, cv = 128 * Q16;
    uint32_t a  = 0;

    for (unsigned f = 0; f < FRAMES(900u); f++) {
        uint32_t t = f;
#ifdef SIMULATION
        t *= 24u;
#endif
        int32_t hcam = 40 * Q16 + isin(t * 128u) * 14 * 4;              // height, Q16 texels
        a = t * 256u / 3u + (uint32_t) (isin(t * 64u) >> 1);              // gentle turns
        cu += (int32_t) ((int64_t) icos(a) * 3 * Q16 / 2 >> 14);
        cv += (int32_t) ((int64_t) isin(a) * 3 * Q16 / 2 >> 14);
        blns_lut_rotate(&blns, LOGO_TRACE_BASE, LOGO_TRACE_BASE + LOGO_TRACE_NR - 1u,
                        (uint8_t) ((t / 3u) % LOGO_TRACE_NR));
        const flight_t view = { cu, cv, a, hcam };
        if (frame_list(flight_draw, &view))
            break;
    }
}

//==========================================================
// 2: ROTOZOOM
//==========================================================
typedef struct {
    int32_t cu, cv, du, dv;
} roto_t;

// One affine blit: texel of the screen's (x, y) = centre + (x - W/2) (du, dv)
// + (y - H/2) (-dv, du), from its first pixel, (du, dv) a pixel and (-dv, du)
// a row (5.11). On the panel (ROT) the frame holds the picture turned by half a
// turn: from the texel of its last pixel, both steps reversed.
static void roto_draw(const void *ctx)
{
    const roto_t    *r   = ctx;
    const blns_tex_t tex = { .addr = TEX, .log2_w = TEX_L2, .log2_h = TEX_L2, .swap = BLNS_SWAP_NONE };
    const int32_t    sx  = ROT ? (int32_t) SCR_W - 1 - (int32_t) (SCR_W / 2u) : -(int32_t) (SCR_W / 2u);
    const int32_t    sy  = ROT ? (int32_t) SCR_H - 1 - (int32_t) (SCR_H / 2u) : -(int32_t) (SCR_H / 2u);
    const int32_t    du  = ROT ? -r->du : r->du, dv = ROT ? -r->dv : r->dv;
    const int64_t    u   = r->cu + (int64_t) r->du * sx - (int64_t) r->dv * sy;
    const int64_t    v   = r->cv + (int64_t) r->dv * sx + (int64_t) r->du * sy;

    blns_gpu_xy_begin(&blns, F0, 0, 0);
    blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
    blns_gpu_tex_set(&blns, &tex, 0);
    blns_gpu_texrect_xy(&blns, 0, 0, SCR_W, SCR_H, (uint16_t) (u >> 10), (uint16_t) (v >> 10),
                        step_511(du), step_511(dv), step_511(-dv), step_511(du));
}

static void scene_rotozoom(void)
{
    for (unsigned f = 0; f < FRAMES(720u); f++) {
        uint32_t t = f;
#ifdef SIMULATION
        t *= 24u;
#endif
        uint32_t a  = t * 128u;
        int32_t  k  = Q16 + (int32_t) ((int64_t) (Q16 * 7 / 10) * isin(t * 256u / 3u) >> 14);   // 0.3 .. 1.7
        int32_t  du = (int32_t) (((int64_t) icos(a) * k) >> 14), dv = (int32_t) (((int64_t) isin(a) * k) >> 14);
        int32_t  cu = 128 * Q16 + (int32_t) ((int64_t) icos(t * 256u / 5u) * 60 * Q16 >> 14);
        int32_t  cv = 128 * Q16 + (int32_t) ((int64_t) isin(t * 256u / 7u) * 60 * Q16 >> 14);

        blns_lut_rotate(&blns, LOGO_TRACE_BASE, LOGO_TRACE_BASE + LOGO_TRACE_NR - 1u,
                        (uint8_t) ((t / 3u) % LOGO_TRACE_NR));
        const roto_t view = { cu, cv, du, dv };
        if (frame_list(roto_draw, &view))
            break;
    }
    blns_lut_rotate(&blns, 0, 0, 0);
}

static void (*const scene[])(void) = { scene_flight, scene_rotozoom };

int main(void)
{
    sin_init();
    build_palette();
    demo_init();
    blns_set_orientation(&blns, SCR_SWAP ^ (ROT ? (BLNS_SWAP_X | BLNS_SWAP_Y) : 0u));   // drawn turned instead
    build_texture();
    demo_run(scene, sizeof(scene) / sizeof(scene[0]));
}

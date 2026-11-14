//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K water demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Water ripples: the 2D wave equation on a 160 x 91 height grid, 3 x 3 pixels
// a cell, shown at 480 x 272 (the bottom edge row two pixels high) with the
// full-resolution aRVern logo standing in the middle. The pool's edges and the
// logo are walls (their cells held at zero), so the waves bounce off them and
// cross each other.
//
//   1 rain        random drops
//   2 two sources two points pulsing in step: interference fringes
//   3 wake        a source moving round the pool
//
// The CPU runs the simulation (two byte buffers, saturated with Zbb min/max)
// and writes the shaded cells into a 160 x 91 image, four per word, kept in
// the VRAM of frame 1 with the bevelled logo. The GPU composes frame 0 in the
// gap after each refresh: the water scaled x3 by textured spans, then the
// logo and its shadow (transparent copies). The LUT does the colours. With the
// palette remap (caps.gpu_remap) the shadow is translucent: the ripples go on
// under it, darker (REMAP DST through a table of darker water shades).
//----------------------------------------------------------------------------
#define LOGO_DATA_TABLES
#include "demo_kit.h"
#include "logo_data.h"
#include "music_data.h"

const mus_tune_t *const *const demo_tunes    = music_tunes;
const unsigned                 demo_tunes_nr = sizeof(music_tunes) / sizeof(music_tunes[0]);

#define CELL             3                     // pixels a cell side
#define GW               ((int) SCR_W / CELL)  // grid: 160 x 91, 3 x 3 pixels a cell
#define GH               (((int) SCR_H + CELL - 1) / CELL)
#define LOGO_X           ((int) (SCR_W - LOGO_W) / 2)   // pixels
#define LOGO_Y           ((int) (SCR_H - LOGO_H) / 2)
#define LOGO_CW          ((unsigned) (LOGO_X % CELL + (int) LOGO_W + CELL - 1) / CELL)   // grid columns the logo covers
#define WATER_IMG        F1                    // the shaded grid, GW x GH
#define LOGO_IMG         (F1 + GW * GH)        // the logo, 200 x 140, transparent 0
#define COMP_LIST        (LOGO_IMG + LOGO_W * LOGO_H)   // byte address of the compose list
#define COMP_LIST_WORDS  1024u
#define SHADOW_TABLE     ((COMP_LIST + 4u * COMP_LIST_WORDS + 255u) & ~255u)  // remap table (bytes)
_Static_assert(SHADOW_TABLE + 256u <= VRAM_SPARE, "the shadow table beyond frame 1");
#define C_SHADOW         44u                   // the logo's shadow on the water
#define C_RIM_LIGHT      45u                   // bevel: lit top-left edge
#define C_RIM_DARK       46u                   //        shaded bottom-right edge
#define RIM              2                     // bevel width, pixels
#define SHADOW_DX        6                     // shadow offset, pixels
#define SHADOW_DY        6
#define WALL_MAX         96u
#define C_WATER          48u                   // 208 water shades from 48 up
#define SHADOW_DIM       9u                    // the translucent shadow: water shade x 9 / 20
#define DU_THIRD         ((int16_t) ((2048 + 2) / 3))   // 1/3 texel a pixel (5.11), rounded up

static int8_t  buf[2][GH][GW];                 // heights: current and previous
static int8_t (*cur)[GW] = buf[0];
static int8_t (*old)[GW] = buf[1];
static uint8_t wall[WALL_MAX][3];             // the logo's cells: grid row, column, length
static unsigned wall_nr;
// Inside the logo's longest wall run of each row nothing is computed: the step
// covers cells 1 .. skip_c0 - 1 and skip_c1 .. GW - 2 (even counts), the
// render words 0 .. skip_w0 - 1 and skip_w1 .. GW / 4 - 1
static uint8_t  skip_c0[GH], skip_c1[GH], skip_w0[GH], skip_w1[GH];
// The wall cells the step writes (outside each row's skipped run), zeroed after
// every step: grid offset and length. A drop writes any cell, the skipped ones
// too: the buffer it fell in has all its walls zeroed once it is written next
static uint16_t wz_off[2 * WALL_MAX];
static uint8_t  wz_len[2 * WALL_MAX];
static unsigned wz_nr;
static bool     poked[2];
static int16_t sin_q14[256];
static uint32_t rnd = 0x7F4A7C15u;

static uint32_t rand32(void)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

static int32_t isin(unsigned a) { return sin_q14[a & 255u]; }
static int32_t icos(unsigned a) { return sin_q14[(a + 64u) & 255u]; }

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

static inline int clamp127(int v)
{
    v = (v > 127) ? 127 : v;
    return (v < -127) ? -127 : v;
}

//==========================================================
// SIMULATION
//==========================================================
// One step: next = (sum of the 4 neighbours) / 2 - previous, damped; written
// over the previous buffer, then the buffers swap. The edge cells and the
// logo's cells stay 0: reflecting walls.
// One row, cells 1 .. GW-2, from row 'c' and the rows around it (c - GW,
// c + GW): written for the 16 registers of RV32E (one pointer per buffer,
// each neighbour loaded once, two cells a turn)
#define U(i)             c[(i) - GW]
#define D(i)             c[(i) + GW]
__attribute__((noinline)) static void step_row(int8_t *o, const int8_t *c, int x0, int x1)
{
    int l = c[x0 - 1], m = c[x0];

    for (int x = x0; x < x1; x += 2) {
        int r = c[x + 1];
        int v = ((U(x) + D(x) + l + r) >> 1) - o[x];
        v -= v >> 6;
        o[x] = (int8_t) clamp127(v);
        l = c[x + 2];
        v = ((U(x + 1) + D(x + 1) + m + l) >> 1) - o[x + 1];
        v -= v >> 6;
        o[x + 1] = (int8_t) clamp127(v);
        m = l;
        l = r;
    }
}

static void step(void)
{
    for (int y = 1; y < GH - 1; y++) {
        step_row(old[y], cur[y], 1, skip_c0[y]);
        step_row(old[y], cur[y], skip_c1[y], GW - 1);
    }
    // the logo's cells are walls
    const unsigned b = (old == buf[1]);
    if (poked[b]) {
        poked[b] = false;
        for (unsigned k = 0; k < wall_nr; k++) {
            int8_t *row = old[wall[k][0]] + wall[k][1];
            for (unsigned i = 0; i < wall[k][2]; i++)
                row[i] = 0;
        }
    } else {
        int8_t *const g = old[0];
        for (unsigned k = 0; k < wz_nr; k++) {
            int8_t *p = g + wz_off[k], *const e = p + wz_len[k];
            do
                *p++ = 0;
            while (p != e);
        }
    }
    int8_t (*t)[GW] = cur;
    cur = old;
    old = t;
}

static void poke(int x, int y, int r, int h)
{
    poked[cur == buf[1]] = true;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r && x + i > 0 && x + i < GW - 1 && y + j > 0 && y + j < GH - 1)
                cur[y + j][x + i] = (int8_t) clamp127(h);
}

//==========================================================
// DISPLAY
//==========================================================
// One row of the image: C_WATER + shade, the shade clamped to 0..207
#define SHADE(a, b, n, uu, dd)  (max_i(min_i(2 * ((a) - (n) + (uu) - (dd)) + ((b) >> 2) + 104 + (int) C_WATER, \
                                             207 + (int) C_WATER), (int) C_WATER))

static inline int min_i(int a, int b) { return (a < b) ? a : b; }
static inline int max_i(int a, int b) { return (a > b) ? a : b; }

__attribute__((noinline)) static void render_row(volatile uint32_t *row, const int8_t *c, int w0, int w1)
{
    volatile uint32_t *out = row + w0, *const end = row + w1;

    c += 4 * w0;
    int a = c[-1], b = c[0];
    while (out != end) {
        int n0 = c[1], n1 = c[2], n2 = c[3], n3 = c[4];
        uint32_t w = (uint32_t) SHADE(a, b, n0, U(0), D(0));
        w |= (uint32_t) SHADE(b, n0, n1, U(1), D(1)) << 8;
        w |= (uint32_t) SHADE(n0, n1, n2, U(2), D(2)) << 16;
        w |= (uint32_t) SHADE(n1, n2, n3, U(3), D(3)) << 24;
        *out++ = w;
        a = n2;
        b = n3;
        c += 4;
    }
}

// Shade from the slope (light from the top left), four cells per VRAM word.
// The edge cells are walls at 0: a row's left and right neighbours outside the
// grid are the border cells of the rows around it, so no test is needed inside
// the loop; the top and bottom rows are flat.
static void render(uint32_t img)
{
    volatile uint32_t *v    = (volatile uint32_t *) (blns_vram8(&blns) + img);
    const uint32_t     flat = (C_WATER + 104u) * 0x01010101u;

    for (int x = 0; x < GW / 4; x++) {
        v[x] = flat;
        v[(GH - 1) * GW / 4 + x] = flat;
    }
    for (int y = 1; y < GH - 1; y++) {
        render_row(v + y * (GW / 4), cur[y], 0, skip_w0[y]);
        render_row(v + y * (GW / 4), cur[y], skip_w1[y], GW / 4);
    }
}

// Frame 0: the water scaled x3, the logo, its shadow. The same 272 textured
// spans and two copies every frame: a command list recorded once, replayed
// with one CALL. The translucent shadow (remap) turns each water pixel under
// it into its darker shade, T[D]; the table leaves the logo's colours as they
// are, so the shadow goes after the logo: the logo's copy, the fast one, comes
// first, ahead of the panel scan. Without the remap the shadow is raster op 15
// (the fill colour wherever the logo is opaque), drawn before the logo.
static blns_gpu_list_t comp_list = { .addr = COMP_LIST / 4u, .capacity = COMP_LIST_WORDS };

__attribute__((optimize("Os"), noinline)) static void compose_record(void)
{
    const blns_tex_t  tex = { .addr = WATER_IMG, .pitch = GW, .free_pitch = true,
                              .swap = BLNS_SWAP_NONE };
    const blns_surf_t src = { LOGO_IMG, LOGO_W, BLNS_SWAP_NONE };
    const blns_surf_t sdw = { F0 + (LOGO_Y + SHADOW_DY) * SCR_W + LOGO_X + SHADOW_DX, 0, BLNS_SWAP_NONE };
    const blns_surf_t dst = { F0 + LOGO_Y * SCR_W + LOGO_X, 0, BLNS_SWAP_NONE };
    const blns_blit_t dim = { .rop = BLNS_ROP_S, .transp = true, .transparent = 0,
                              .remap = BLNS_REMAP_DST };

    blns_gpu_list_begin(&blns, &comp_list, 0u);
    blns_gpu_tex_begin(&blns, &tex, F0, SCR_W, 0);
    blns_gpu_tex_step(&blns, DU_THIRD, 0);
    for (unsigned y = 0; y < SCR_H; y++)
        blns_gpu_tex_span(&blns, F0 + y * SCR_W, SCR_W, 0, (uint16_t) ((y / CELL) << 6));
    if (blns.caps.gpu_remap) {
        blns_gpu_copy_transp(&blns, &src, &dst, LOGO_W, LOGO_H, BLNS_ROP_S, 0);
        blns_gpu_remap_table(&blns, SHADOW_TABLE / 4u);     // in the list: set at each CALL
        blns_gpu_blit(&blns, &src, &sdw, LOGO_W, LOGO_H, &dim);
    } else {
        blns_gpu_colour_set(&blns, C_SHADOW);   // op 15 below: the shadow's colour
        blns_gpu_copy_transp(&blns, &src, &sdw, LOGO_W, LOGO_H, BLNS_ROP_FILL_IF_S, 0);
        blns_gpu_copy_transp(&blns, &src, &dst, LOGO_W, LOGO_H, BLNS_ROP_S, 0);
    }
    blns_gpu_list_end(&blns);
}

// Frame 0 is shown all the time: the simulation step runs while the GPU composes,
// the water image is refilled once the GPU is done with it, and the next
// composition starts at the end of the frame being sent. With WAIT_REFR_DONE
// the GPU waits for that end: the CPU queues the composition and goes on
static uint32_t comp_tag;

static bool show(unsigned t)
{
    if (blns.caps.gpu) {
        wait_fence_frame(comp_tag);
        render(WATER_IMG);
        blns_gpu_wait_refresh(&blns);
        blns_gpu_list_call(&blns, &comp_list, false);
        comp_tag = comp_list.tag;
    } else {
        blns_gpu_wait(&blns);
        render(WATER_IMG);
        wait_frames(1);
        blns_gpu_list_call(&blns, &comp_list, false);
    }
    blns_lut_rotate(&blns, LOGO_TRACE_BASE, LOGO_TRACE_BASE + LOGO_TRACE_NR - 1u,
                    (uint8_t) ((t / 2u) % LOGO_TRACE_NR));
    return scene_check();
}

//==========================================================
// SCENES
//==========================================================
// PROFILE_FRAMES=n (simulation): the rain scene alone, n frames of one step
// each as on the board, for a short trace to profile
#if defined(SIMULATION) && !defined(PROFILE_FRAMES)
#define SPEEDUP          4u
#else
#define SPEEDUP          1u
#endif
#ifdef PROFILE_FRAMES
#define RAIN_FRAMES      (PROFILE_FRAMES)
#else
#define RAIN_FRAMES      FRAMES(720u)
#endif

static void scene_rain(void)
{
    for (unsigned f = 0; f < RAIN_FRAMES; f++) {
        if ((rand32() & 15u) < 2u * SPEEDUP)
            poke(2 + (int) (rand32() % (GW - 4)), 2 + (int) (rand32() % (GH - 4)), 2, -110);
        for (unsigned s = 0; s < SPEEDUP; s++)
            step();
        if (show(f))
            return;
    }
}

static void scene_sources(void)
{
    for (unsigned f = 0; f < FRAMES(900u); f++) {
        for (unsigned s = 0; s < SPEEDUP; s++) {
            unsigned ph = (f * SPEEDUP + s) * 20u;             // in phase, ~13 steps a period
            int      h  = (int) (isin(ph) * 110 >> 14);
            poke(GW / 2 - 24, LOGO_Y / CELL - 4, 1, h);        // both above the logo: fringes
            poke(GW / 2 + 24, LOGO_Y / CELL - 4, 1, h);        // between them, echoes off it
            step();
        }
        if (show(f))
            return;
    }
}

static void scene_wake(void)
{
    for (unsigned f = 0; f < FRAMES(900u); f++) {
        for (unsigned s = 0; s < SPEEDUP; s++) {
            unsigned t = (f * SPEEDUP + s) * 2u;
            int x = GW / 2 + (int) (icos(t / 3u) * 70 >> 14);
            int y = GH / 2 + (int) (isin(t / 3u) * (GH / 2 - 6) >> 14);
            poke(x, y, 1, (int) (isin(t * 24u) * 100 >> 14));
            step();
        }
        if (show(f))
            return;
    }
}

//==========================================================
// SET-UP
//==========================================================
static void logo_row(uint32_t *dst, int y)
{
    const volatile uint32_t *src = (const volatile uint32_t *) (blns_vram8(&blns) + LOGO_IMG + y * (int) LOGO_W);

    for (unsigned i = 0; i < LOGO_W / 4u; i++)
        dst[i] = ((y < 0) || (y >= (int) LOGO_H)) ? 0u : src[i];
}

// One pass over the logo image with a window of 2 * RIM + 1 rows (word reads):
// - bevel: an opaque pixel within RIM of a transparent one above or to the left
//   is a lit rim, below or to the right a shaded one;
// - walls: every grid cell with an opaque pixel, as runs per grid row (grid
//   row and column of the pool).
__attribute__((optimize("Os"), noinline)) static void logo_finish(void)
{
    uint32_t          win[2 * RIM + 1][LOGO_W / 4u];
    volatile uint8_t *px = blns_vram8(&blns) + LOGO_IMG;
    uint8_t           cell[LOGO_CW];

    for (int k = -RIM; k <= RIM; k++)
        logo_row(win[(k + 2 * RIM + 1) % (2 * RIM + 1)], k);
    for (int y = 0; y < (int) LOGO_H; y++) {
#define LP(dy, x) (((x) < 0 || (x) >= (int) LOGO_W) ? 0u : \
                   ((const uint8_t *) win[(y + (dy) + 2 * RIM + 1) % (2 * RIM + 1)])[x])
        const int gy = (LOGO_Y + y) / CELL;
        if (y == 0 || (LOGO_Y + y) % CELL == 0)
            for (unsigned c = 0; c < LOGO_CW; c++)
                cell[c] = 0;
        for (int x = 0; x < (int) LOGO_W; x++) {
            if (LP(0, x) == 0u)
                continue;
            cell[(LOGO_X % CELL + x) / CELL] = 1u;
            bool lit = false, dark = false;
            for (int d = 1; d <= RIM; d++) {
                lit  |= (LP(-d, x) == 0u) || (LP(0, x - d) == 0u);
                dark |= (LP(d, x) == 0u)  || (LP(0, x + d) == 0u);
            }
            if (lit)
                px[y * (int) LOGO_W + x] = C_RIM_LIGHT;
            else if (dark)
                px[y * (int) LOGO_W + x] = C_RIM_DARK;
        }
#undef LP
        if (y == (int) LOGO_H - 1 || (LOGO_Y + y) % CELL == CELL - 1)
            for (unsigned c = 0; c < LOGO_CW && wall_nr < WALL_MAX; c++) {
                if (!cell[c] || (c > 0u && cell[c - 1u]))
                    continue;
                unsigned n = 1;
                while (c + n < LOGO_CW && cell[c + n])
                    n++;
                wall[wall_nr][0] = (uint8_t) gy;
                wall[wall_nr][1] = (uint8_t) (LOGO_X / CELL + c);
                wall[wall_nr][2] = (uint8_t) n;
                wall_nr++;
            }
        logo_row(win[(y + RIM + 1 + 2 * RIM + 1) % (2 * RIM + 1)], y + RIM + 1);   // row y - RIM leaves
    }
}

__attribute__((optimize("Os"), noinline)) static void skip_init(void)
{
    for (unsigned y = 0; y < GH; y++) {
        skip_c0[y] = skip_c1[y] = GW - 1;
        skip_w0[y] = skip_w1[y] = GW / 4;
    }
    for (unsigned k = 0; k < wall_nr; k++) {
        const unsigned y  = wall[k][0];
        const unsigned c0 = wall[k][1], e = c0 + wall[k][2];
        const unsigned s0 = c0 | 1u, s1 = (e & 1u) ? e : e - 1u;         // odd: even counts
        const unsigned w0 = (c0 + 3u) / 4u, w1 = e / 4u;
        if (s1 > s0 && (unsigned) (s1 - s0) > (unsigned) (skip_c1[y] - skip_c0[y])) {
            skip_c0[y] = (uint8_t) s0;
            skip_c1[y] = (uint8_t) s1;
            skip_w0[y] = (uint8_t) ((w1 > w0) ? w0 : GW / 4);
            skip_w1[y] = (uint8_t) ((w1 > w0) ? w1 : GW / 4);
        }
    }
    for (unsigned k = 0; k < wall_nr; k++) {
        const unsigned y  = wall[k][0];
        const unsigned c0 = wall[k][1], e = c0 + wall[k][2];
        const unsigned a[2] = { c0, (c0 > skip_c1[y]) ? c0 : skip_c1[y] };
        const unsigned z[2] = { (e < skip_c0[y]) ? e : skip_c0[y], e };
        for (unsigned i = 0; i < 2u; i++)
            if (z[i] > a[i]) {
                wz_off[wz_nr] = (uint16_t) (y * GW + a[i]);
                wz_len[wz_nr] = (uint8_t) (z[i] - a[i]);
                wz_nr++;
            }
    }
}

__attribute__((optimize("Os"), noinline)) static void setup(void)
{
    for (unsigned i = 1; i < LOGO_PAL_NR; i++)
        pal[i] = logo_pal_light[i];
    pal[C_SHADOW]    = 0x020c24u;
    pal[C_RIM_LIGHT] = 0xfff2dcu;
    pal[C_RIM_DARK]  = 0x1a1008u;
    // water: deep blue troughs, turquoise, white crests
    for (unsigned i = 0; i < 208u; i++)
        pal[C_WATER + i] = (i < 120u) ? rgb_lerp(0x02123au, 0x1a78b4u, i, 119)
                         : (i < 180u) ? rgb_lerp(0x1a78b4u, 0x7ad8f0u, i - 120u, 59)
                                      : rgb_lerp(0x7ad8f0u, 0xffffffu, i - 180u, 27);
    demo_init();
    // the shadow's table: each water shade to about half its level, the other
    // colours (the logo, its rim) unchanged
    for (unsigned i = 0; i < 256u; i++)
        blns_vram8(&blns)[SHADOW_TABLE + i] =
            (uint8_t) ((i >= C_WATER) ? C_WATER + (i - C_WATER) * SHADOW_DIM / 20u : i);
    // the logo image: 0 around it, drawn by GPU spans from its runs
    const blns_surf_t img = { LOGO_IMG, LOGO_W, BLNS_SWAP_NONE };
    blns_gpu_fill(&blns, &img, LOGO_W, LOGO_H, 0, BLNS_ROP_S);
    for (unsigned l = 0; l < LOGO_LAYERS; l++) {
        unsigned a = logo_layer_start[l], b = logo_layer_start[l + 1u];
        if (a == b)
            continue;
        blns_gpu_span_begin(&blns, LOGO_IMG, LOGO_W, logo_layer_idx[l]);
        for (unsigned k = a; k < b; ) {         // the layer's runs, in batches
            blns_span_t sp[16];
            unsigned    n = 0;
            for (; k < b && n < 16u; k++, n++)
                sp[n] = (blns_span_t) { LOGO_IMG + logo_runs[3u * k] * LOGO_W + logo_runs[3u * k + 1u],
                                        logo_runs[3u * k + 2u] };
            blns_gpu_spans(&blns, sp, n);
        }
    }
    blns_gpu_fill(&blns, &(blns_surf_t) { F0, 0, BLNS_SWAP_NONE }, SCR_W, SCR_H, C_WATER + 104u, BLNS_ROP_S);
    blns_gpu_wait(&blns);
    logo_finish();
    skip_init();
    // the image words under the logo that are never rendered: flat water
    const blns_surf_t wimg = { WATER_IMG, GW, BLNS_SWAP_NONE };
    blns_gpu_fill(&blns, &wimg, GW, GH, (uint8_t) (C_WATER + 104u), BLNS_ROP_S);
    blns_gpu_wait(&blns);
    compose_record();
    pal_show();
}

static void (*const scene[])(void) = { scene_rain, scene_sources, scene_wake };

int main(void)
{
    sin_init();
    setup();
#ifdef PROFILE_FRAMES
    demo_run(scene, 1u);                       // the rain scene alone
#else
    demo_run(scene, sizeof(scene) / sizeof(scene[0]));
#endif
}

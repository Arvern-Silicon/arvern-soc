//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (DE0-Nano-SoC 16 bpp UI demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// A user interface at 16 bpp (RGB565), drawn by the GPU with per-channel
// gradients, x/y coordinates, the clip window and lines:
//
//   1 dashboard     gradient background and title, rounded cards with
//                   shadows, an animated progress bar and buttons
//   2 equalizer     16 bars coloured by height (green, yellow, red): each one
//                   is the whole ramp clipped to its height
//   3 colour        a red x green square at an animated blue, its slider, a
//                   cursor with the colour under it
//   4 monitor       a line chart of two signals scrolling by (dashed grid,
//                   a limit line, polylines) and a wireframe gauge of the
//                   load: GPU lines, solid and dashed
//
// One 320 x 240 frame at 16 bpp fills the VRAM but 10 KB: the 1 bpp console
// font and a command list live there. Every frame records its drawing into
// the list; the GPU waits for the end of the frame being sent
// (WAIT_REFR_DONE), runs the list and passes a fence, while the CPU goes on.
//----------------------------------------------------------------------------
#define TEXT_DATA_TABLES
#include "demo_kit.h"
#include "text_data.h"

#define FONT_BYTE        VRAM_SPARE                           // the 1 bpp font
#define LIST_BYTE        (FONT_BYTE + sizeof(mono_bits))
#ifndef LIST_WORDS                                            // (smaller to measure a frame's need)
#define LIST_WORDS       ((VRAM_BYTES - LIST_BYTE) / 4u)
#endif
#define VRAM_BYTES       (VRAM_SPARE + 10240u)

#define RGB(r, g, b)     ((uint32_t) ((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

static blns_gpu_list_t frame_list = { .addr = LIST_BYTE / 4u, .capacity = LIST_WORDS };
static uint32_t    frame_tag;
static int16_t     sin_q14[256];
static uint32_t    rnd = 0x2545F491u;

static uint32_t rand32(void)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

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

static int32_t isin(unsigned a) { return sin_q14[a & 255u]; }

//==========================================================
// DRAWING
//==========================================================
static void colour(uint32_t c)
{
    blns_gpu_colour_set(&blns, c);              // the spans and x/y fills that follow
}

// A channel step from a to b over n steps, in 1/64 of a channel unit
static int chan_step(unsigned a, unsigned b, unsigned n)
{
    int d = (n == 0u) ? 0 : ((int) b - (int) a) * 64 / (int) n;
    return (d > 511) ? 511 : (d < -512) ? -512 : d;
}

// The RGB565 colour i / n of the way from c0 to c1 (as the GPU ramps it)
static uint32_t ramp(uint32_t c0, uint32_t c1, unsigned i, unsigned n)
{
    const int r = (int) (c0 >> 11) + (chan_step(c0 >> 11, c1 >> 11, n) * (int) i) / 64;
    const int g = (int) ((c0 >> 5) & 63u) + (chan_step((c0 >> 5) & 63u, (c1 >> 5) & 63u, n) * (int) i) / 64;
    const int b = (int) (c0 & 31u) + (chan_step(c0 & 31u, c1 & 31u, n) * (int) i) / 64;
    return ((uint32_t) (r < 0 ? 0 : r > 31 ? 31 : r) << 11) | ((uint32_t) (g < 0 ? 0 : g > 63 ? 63 : g) << 5) |
           (uint32_t) (b < 0 ? 0 : b > 31 ? 31 : b);
}

// A rectangle ramped from c0 to c1: down its rows, or along its runs
static void grad_rect(int x, int y, unsigned w, unsigned h, uint32_t c0, uint32_t c1, bool along)
{
    const unsigned n = along ? w - 1u : h - 1u;
    colour(c0);
    blns_gpu_gradient_rgb(&blns, true, 1, along, chan_step(c0 >> 11, c1 >> 11, n),
                          chan_step((c0 >> 5) & 63u, (c1 >> 5) & 63u, n), chan_step(c0 & 31u, c1 & 31u, n));
    blns_gpu_fill_xy(&blns, x, y, (uint16_t) w, (uint16_t) h);
    blns_gpu_gradient(&blns, false, 0u, false, 0);
}

// Rounded corners: the inset of the corner rows, radius R
#define R 8
static uint8_t inset[R];

__attribute__((optimize("Os"), noinline)) static void inset_init(void)
{
    for (int i = 0; i < R; i++) {
        int dy = 2 * (R - i) - 1, k = 0;                       // twice the distance from the centre row
        while ((2 * (R - k) - 1) * (2 * (R - k) - 1) + dy * dy > 4 * R * R)
            k++;
        inset[i] = (uint8_t) k;
    }
}

// A rounded rectangle ramped from c0 (top) to c1 (bottom); the corner rows
// are spans of their own colour
static void round_rect(int x, int y, unsigned w, unsigned h, uint32_t c0, uint32_t c1)
{
    for (int i = 0; i < R; i++) {
        colour(ramp(c0, c1, (unsigned) i, h - 1u));
        blns_gpu_span_xy(&blns, x + inset[i], y + i, (uint16_t) (w - 2u * inset[i]));
        colour(ramp(c0, c1, h - 1u - (unsigned) i, h - 1u));
        blns_gpu_span_xy(&blns, x + inset[i], y + (int) h - 1 - i, (uint16_t) (w - 2u * inset[i]));
    }
    grad_rect(x, y + R, w, h - 2u * R, ramp(c0, c1, R, h - 1u), ramp(c0, c1, h - 1u - R, h - 1u), false);
}

static void card(int x, int y, unsigned w, unsigned h, uint32_t c0, uint32_t c1)
{
    round_rect(x + 4, y + 4, w, h, RGB(8, 8, 20), RGB(8, 8, 20));           // shadow
    round_rect(x, y, w, h, c0, c1);
}

// Text in the 8 x 16 console font, transparent (or on 'bg' with opaque)
// (x/y mode: one blns_gpu_text_xy batch per string, clipped by the window)
static void text(int x, int y, const char *s, uint32_t c, uint32_t bg, bool opaque)
{
    const blns_expand_t e = { .width = MONO_W, .height = MONO_H, .src_pitch = 0,
                              .depth = BLNS_EXP_1BPP, .opaque = opaque, .swap = BLNS_SWAP_NONE };
    const blns_ba_t font = blns_vram_ba(&blns, blns_vram8(&blns) + FONT_BYTE);   // 1 bpp glyphs
    blns_pa_t       src[24];

    blns_gpu_expand_set(&blns, &e, c, bg);
    while (*s) {
        unsigned n = 0;
        for (; *s && n < sizeof(src) / sizeof(src[0]); s++, n++) {
            const unsigned g = (*s >= 32 && *s < 127) ? (unsigned) (*s - 32) : 0u;
            src[n] = font + g * MONO_W * MONO_H;
        }
        blns_gpu_text_xy(&blns, x, y, src, NULL, n);
        x += (int) (n * MONO_W);
    }
}

static void xy_mode(void)
{
    blns_gpu_xy_begin(&blns, F0, 0, 0);
    blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
}

// Frame pacing: the last frame's list done, record the next one; then the
// GPU waits for the end of the frame being sent, runs it and passes a fence
static void frame_begin(void)
{
    wait_fence_frame(frame_tag);
    blns_gpu_list_begin(&blns, &frame_list, 0u);
    xy_mode();
}

static bool frame_end(void)
{
    const blns_status_t st = blns_gpu_list_end(&blns);
#ifdef SIMULATION
    if (st != BLNS_OK) {                        // the frame did not fit the list: a wrong LED
        lks_led_set(&lks, 0xEEu);               // for the stimulus, and stop
        for (;;)
            ;
    }
#else
    (void) st;
#endif
    blns_gpu_wait_refresh(&blns);
    blns_gpu_list_call(&blns, &frame_list, false);
    frame_tag = frame_list.tag;
    return scene_check();
}

#define BG_TOP           RGB(16, 24, 48)
#define BG_BOT           RGB(56, 16, 72)

static void background(void)
{
    xy_mode();
    grad_rect(0, 0, SCR_W, SCR_H, BG_TOP, BG_BOT, false);
}

static void hex2(char *p, unsigned v)
{
    static const char d[] = "0123456789ABCDEF";
    p[0] = d[(v >> 4) & 15u];
    p[1] = d[v & 15u];
}

//==========================================================
// SCENES
//==========================================================
// 1. Dashboard
static void scene_dashboard(void)
{
    static const char *const label[3] = { "PLAY", "STOP", "MENU" };

    blns_gpu_wait(&blns);
    background();
    grad_rect(0, 0, SCR_W, 28, RGB(0, 150, 170), RGB(40, 60, 200), true);
    text(12, 6, "BELENOS  16 BPP UI", RGB(255, 255, 255), 0, false);
    card(10, 40, 140, 88, RGB(70, 80, 120), RGB(40, 44, 70));
    card(170, 40, 140, 88, RGB(70, 80, 120), RGB(40, 44, 70));
    card(10, 140, 300, 88, RGB(60, 70, 110), RGB(30, 34, 60));
    text(22, 50, "LOAD", RGB(200, 210, 255), 0, false);
    text(182, 50, "LEVEL", RGB(200, 210, 255), 0, false);
    blns_gpu_wait(&blns);

    for (unsigned f = 0; f < FRAMES(600u); f++) {
        frame_begin();
        const unsigned load = 50u + (unsigned) (isin(f * 3u) * 45 >> 14);      // 5 .. 95
        const unsigned lvl  = 50u + (unsigned) (isin(f * 5u + 40u) * 48 >> 14);
        // progress bar: the ramp up to the load, the track after it
        grad_rect(22, 74, load, 14, RGB(40, 200, 80), RGB(40, 220, 220), true);
        if (load < 116u) {
            colour(RGB(24, 28, 44));
            blns_gpu_fill_xy(&blns, 22 + (int) load, 74, (uint16_t) (116u - load), 14);
        }
        char num[5] = { (char) ('0' + load / 10u), (char) ('0' + load % 10u), ' ', '%', 0 };
        text(22, 98, num, RGB(255, 255, 255), RGB(52, 58, 90), true);
        // level: a vertical meter ramped by height, clipped to the level
        const int lv_top = 122 - (int) (lvl * 50u / 100u);
        blns_gpu_clip(&blns, 186, lv_top, 205, 121);
        grad_rect(186, 72, 20, 50, RGB(255, 60, 40), RGB(60, 220, 60), false);
        if (lv_top > 72) {
            blns_gpu_clip(&blns, 186, 72, 205, lv_top - 1);
            colour(RGB(24, 28, 44));
            blns_gpu_fill_xy(&blns, 186, 72, 20, 50);
        }
        blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
        // buttons: one pressed at a time (its ramp upside down)
        for (unsigned b = 0; b < 3u; b++) {
            const bool down = ((f / 20u) % 3u) == b;
            const uint32_t hi = RGB(120, 150, 255), lo = RGB(30, 50, 140);
            round_rect(28 + (int) b * 96, 168, 80, 36, down ? lo : hi, down ? hi : lo);
            text(28 + (int) b * 96 + 24, 178, label[b], RGB(255, 255, 255), 0, false);
        }
        if (frame_end())
            return;
    }
}

// 2. Equalizer: 16 bars, the colour set by the height on screen
#define EQ_N             16
#define EQ_TOP           40
#define EQ_BOT           229
#define EQ_MID           130

static void scene_equalizer(void)
{
    static uint16_t peak[EQ_N];

    blns_gpu_wait(&blns);
    background();
    text(96, 12, "EQUALIZER", RGB(255, 255, 255), 0, false);
    blns_gpu_wait(&blns);
    for (unsigned i = 0; i < EQ_N; i++)
        peak[i] = EQ_BOT;
    for (unsigned f = 0; f < FRAMES(600u); f++) {
        frame_begin();
        for (unsigned i = 0; i < EQ_N; i++) {
            const int x = 12 + (int) i * 19;
            int top = EQ_TOP + (int) ((isin(f * (3u + i % 5u) + i * 23u) + 16384) * 120 >> 15) +
                      (int) (rand32() % 20u);
            top = (top > EQ_BOT) ? EQ_BOT : top;
            // the bar: the whole ramp, clipped to [top, bottom]
            blns_gpu_clip(&blns, x, top, x + 13, EQ_BOT);
            grad_rect(x, EQ_TOP, 14, EQ_MID - EQ_TOP, RGB(255, 40, 30), RGB(255, 220, 40), false);
            grad_rect(x, EQ_MID, 14, EQ_BOT - EQ_MID + 1, RGB(255, 220, 40), RGB(40, 220, 60), false);
            // above it: the background, clipped the same way
            if (top > EQ_TOP) {
                blns_gpu_clip(&blns, x, EQ_TOP, x + 13, top - 1);
                grad_rect(x, 0, 14, SCR_H, BG_TOP, BG_BOT, false);
            }
            // the peak falls slowly
            peak[i] = (uint16_t) ((top < peak[i]) ? top : peak[i] + 1);
            blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
            colour(RGB(255, 255, 255));
            blns_gpu_fill_xy(&blns, x, (int) peak[i] - 2, 14, 2);
        }
        if (frame_end())
            return;
    }
}

// 3. Colour: red along x, green down the square, blue animated
#define CP_X             16
#define CP_Y             40
#define CP_N             160

static void scene_colour(void)
{
    char hex[8] = { '#', 0, 0, 0, 0, 0, 0, 0 };

    blns_gpu_wait(&blns);
    background();
    text(16, 12, "COLOUR", RGB(255, 255, 255), 0, false);
    blns_gpu_wait(&blns);
    for (unsigned f = 0; f < FRAMES(600u); f++) {
        frame_begin();
        const unsigned b = 128u + (unsigned) (isin(f * 2u) * 127 >> 14);      // 1 .. 255
        // the square: one span per row, red ramped along it
        blns_gpu_gradient_rgb(&blns, true, 1, true, chan_step(0, 31, CP_N - 1u), 0, 0);
        for (unsigned r = 0; r < CP_N; r++) {
            colour(RGB(0, r * 255u / (CP_N - 1u), b));
            blns_gpu_span_xy(&blns, CP_X, CP_Y + (int) r, CP_N);
        }
        blns_gpu_gradient(&blns, false, 0u, false, 0);
        // blue slider and its knob
        grad_rect(196, CP_Y, 16, CP_N, RGB(0, 0, 255), RGB(0, 0, 0), false);
        colour(RGB(30, 34, 60));
        blns_gpu_fill_xy(&blns, 214, CP_Y - 4, 10, CP_N + 8);
        colour(RGB(255, 255, 255));
        blns_gpu_fill_xy(&blns, 214, CP_Y + (int) ((255u - b) * (CP_N - 1u) / 255u) - 1, 10, 3);
        // the cursor on a circle, the colour under it
        const int cx = (int) (CP_N / 2u) + (int) (isin(f * 3u + 64u) * 60 >> 14);
        const int cy = (int) (CP_N / 2u) + (int) (isin(f * 3u) * 60 >> 14);
        const unsigned cr = (unsigned) cx * 255u / (CP_N - 1u), cg = (unsigned) cy * 255u / (CP_N - 1u);
        colour(RGB(255, 255, 255));
        blns_gpu_fill_xy(&blns, CP_X + cx - 5, CP_Y + cy, 11, 1);
        blns_gpu_fill_xy(&blns, CP_X + cx, CP_Y + cy - 5, 1, 11);
        colour(RGB(cr, cg, b));
        blns_gpu_fill_xy(&blns, 236, CP_Y, 72, 60);
        hex2(&hex[1], cr);
        hex2(&hex[3], cg);
        hex2(&hex[5], b);
        text(240, CP_Y + 70, hex, RGB(255, 255, 255), RGB(30, 34, 60), true);
        if (frame_end())
            return;
    }
}

// 4. Monitor: a line chart of two signals scrolling by and a wireframe gauge
// of the newest load, every stroke a GPU line in x/y mode: dashed grid lines
// (the line pattern), a limit line with opaque dashes (the gaps in a second
// colour), the signals and the dial as polylines (each joint drawn once)
#define MN_CW            ((int) SCR_W * 3 / 5 - 15)            // chart card width
#define MN_GX            (10 + MN_CW + 10)                     // gauge card
#define MN_GW            ((int) SCR_W - 10 - MN_GX)
#define MN_Y             40
#define MN_H             ((int) SCR_H - 14 - MN_Y)
#define MN_C0            RGB(70, 80, 120)                      // the cards' ramp
#define MN_C1            RGB(40, 44, 70)
#define MN_PANEL         RGB(16, 18, 34)                       // the plot and the dial
#define MN_EDGE          RGB(100, 112, 170)
#define PL_X             20                                    // plot
#define PL_Y             (MN_Y + 30)
#define PL_W             (MN_CW - 20)
#define PL_H             (MN_H - 40)
#define PL_STEP          4                                     // pixels between samples
#define PL_N             ((PL_W - 1) / PL_STEP + 2)            // samples drawn (one clipped)
#define PL_SUB           4                                     // frames per sample: 1 pixel a frame
#define PL_GRID          16                                    // samples between vertical grid lines
#define PL_LIMIT         80                                    // the limit line, percent
#define GA_S             (MN_GW - 20)                          // the dial, a square
#define GA_X             (MN_GX + 10)
#define GA_Y             PL_Y
#define GA_CX            (GA_X + GA_S / 2)
#define GA_CY            (GA_Y + GA_S / 2 + GA_S / 16)
#define GA_R             (GA_S / 2 - 6)
#define GA_A0            96u                                   // 0 %: 135 degrees (clockwise from +x)
#define GA_SWEEP         192u                                  // 100 %: 270 degrees on
#define ARC_N            48                                    // ring segments

static uint8_t mn_hist[2][PL_N];                               // percent, oldest first

// The two signals at sample s, percent
static unsigned mn_load(unsigned s)
{
    return (unsigned) (50 + (isin(s * 3u) * 30 >> 14) + (isin(s * 11u + 20u) * 10 >> 14) +
                       (int) (rand32() % 9u) - 4);
}

static unsigned mn_temp(unsigned s)
{
    return (unsigned) (38 + (isin(s + 64u) * 22 >> 14) + (int) (rand32() % 5u) - 2);
}

static void mn_push(unsigned s)
{
    for (unsigned i = 0; i + 1u < PL_N; i++) {
        mn_hist[0][i] = mn_hist[0][i + 1u];
        mn_hist[1][i] = mn_hist[1][i + 1u];
    }
    mn_hist[0][PL_N - 1] = (uint8_t) mn_load(s);
    mn_hist[1][PL_N - 1] = (uint8_t) mn_temp(s);
}

// Plot row of a percentage, and a point on the dial: 1/16 pixel (12.4)
static int mn_row(unsigned v) { return PL_Y + PL_H - 1 - (int) v * (PL_H - 1) / 100; }

static void mn_polar(int32_t *xy, unsigned a, int r)
{
    xy[0] = BLNS_SUB(GA_CX) + 8 + (isin(a + 64u) * r >> 10);
    xy[1] = BLNS_SUB(GA_CY) + 8 + (isin(a) * r >> 10);
}

// The angle of v percent (v in 1/16 percent)
static unsigned mn_angle(unsigned v16) { return GA_A0 + v16 * GA_SWEEP / 1600u; }

static uint32_t mn_zone(unsigned v)
{
    return (v < 60u) ? RGB(60, 220, 90) : (v < PL_LIMIT) ? RGB(255, 210, 40) : RGB(255, 70, 50);
}

static const uint32_t mn_col[2] = { RGB(120, 255, 140), RGB(80, 190, 255) };   // load, temperature

static void scene_monitor(void)
{
    static const blns_line_t reset = { .phase_reset = true };
    const blns_line_t        tape  = { .opaque = true, .phase_reset = true, .bg = RGB(110, 30, 30) };
    int32_t                  xy[2 * (PL_N > ARC_N + 1 ? PL_N : ARC_N + 1)];
    char                     num[10] = { 'L', 'O', 'A', 'D', ' ', 0, 0, 0, '%', 0 };

    blns_gpu_wait(&blns);
    background();
    text(16, 12, "MONITOR", RGB(255, 255, 255), 0, false);
    card(10, MN_Y, (unsigned) MN_CW, (unsigned) MN_H, MN_C0, MN_C1);
    card(MN_GX, MN_Y, (unsigned) MN_GW, (unsigned) MN_H, MN_C0, MN_C1);
    text(PL_X, MN_Y + 8, "LOAD", mn_col[0], 0, false);
    text(PL_X + 6 * (int) MONO_W, MN_Y + 8, "TEMP", mn_col[1], 0, false);
    colour(MN_EDGE);                                           // the panels' frames
    blns_gpu_rect_outline_xy(&blns, PL_X - 1, PL_Y - 1, PL_W + 2u, PL_H + 2u, NULL);
    blns_gpu_rect_outline_xy(&blns, GA_X - 1, GA_Y - 1, GA_S + 2u, GA_S + 2u, NULL);
    blns_gpu_wait(&blns);
    for (unsigned s = 0; s < PL_N; s++)                        // a full chart from the start
        mn_push(s);

    for (unsigned f = 0; f < FRAMES(600u); f++) {
        const unsigned s  = PL_N + f / PL_SUB, ph = f % PL_SUB;    // the newest sample, the scroll
        if (ph == 0u && f)
            mn_push(s);
        frame_begin();
        colour(MN_PANEL);
        blns_gpu_fill_xy(&blns, PL_X, PL_Y, PL_W, PL_H);
        blns_gpu_fill_xy(&blns, GA_X, GA_Y, GA_S, GA_S);

        // the chart, clipped to the plot: the samples PL_STEP pixels apart,
        // moving left by a pixel a frame
        blns_gpu_clip(&blns, PL_X, PL_Y, PL_X + PL_W - 1, PL_Y + PL_H - 1);
        const int x0 = PL_X - (int) (ph * PL_STEP / PL_SUB);
        colour(RGB(56, 64, 104));                              // the grid: 4 on, 4 off
        blns_gpu_line_pattern(&blns, 0x0Fu, 8u);
        for (unsigned v = 25u; v < 100u; v += 25u)
            blns_gpu_line_px(&blns, PL_X, mn_row(v), PL_X + PL_W - 1, mn_row(v), &reset);
        for (unsigned i = 0; i < PL_N; i++)                    // vertical lines ride with the samples
            if ((s - (PL_N - 1u) + i) % PL_GRID == 0u)
                blns_gpu_line_px(&blns, x0 + (int) i * PL_STEP, PL_Y, x0 + (int) i * PL_STEP,
                                 PL_Y + PL_H - 1, &reset);
        colour(RGB(255, 80, 60));                              // the limit: red and dark red
        blns_gpu_line_pattern(&blns, 0x3Fu, 12u);
        blns_gpu_line_px(&blns, PL_X, mn_row(PL_LIMIT), PL_X + PL_W - 1, mn_row(PL_LIMIT), &tape);
        blns_gpu_line_solid(&blns);
        for (int k = 1; k >= 0; k--) {                         // the load on top
            for (unsigned i = 0; i < PL_N; i++) {
                xy[2 * i]      = BLNS_SUB(x0 + (int) i * PL_STEP) + 8;
                xy[2 * i + 1u] = BLNS_SUB(mn_row(mn_hist[k][i])) + 8;
            }
            colour(mn_col[k]);
            blns_gpu_polyline_xy(&blns, xy, PL_N, false, NULL);
        }

        // the gauge, clipped to the dial: the newest load, eased between samples
        blns_gpu_clip(&blns, GA_X, GA_Y, GA_X + GA_S - 1, GA_Y + GA_S - 1);
        const unsigned prev = mn_hist[0][PL_N - 2], last = mn_hist[0][PL_N - 1];
        const unsigned v16  = (prev * (PL_SUB - ph) + last * ph) * 16u / PL_SUB;
        for (unsigned i = 0; i <= ARC_N; i++)                  // the ring
            mn_polar(&xy[2 * i], GA_A0 + i * GA_SWEEP / ARC_N, GA_R);
        colour(MN_EDGE);
        blns_gpu_polyline_xy(&blns, xy, ARC_N + 1u, false, NULL);
        for (unsigned t = 0; t <= 100u; t += 5u) {             // the ticks, coloured by zone
            int32_t a[4];
            mn_polar(&a[0], mn_angle(t * 16u), GA_R - 2);
            mn_polar(&a[2], mn_angle(t * 16u), GA_R - ((t % 25u) ? 5 : 10));
            colour(mn_zone(t));
            blns_gpu_line_xy(&blns, a[0], a[1], a[2], a[3], NULL);
        }
        // the value: a segmented arc (3 on, 1 off) inside the ticks, two
        // pixels thick, in the colour of its zone
        const unsigned an = v16 * ARC_N / 1600u;
        if (an) {
            colour(mn_zone(v16 / 16u));
            blns_gpu_line_pattern(&blns, 0x7u, 4u);
            for (int r = GA_R - 14; r >= GA_R - 15; r--) {
                for (unsigned i = 0; i <= an; i++)
                    mn_polar(&xy[2 * i], GA_A0 + i * GA_SWEEP / ARC_N, r);
                blns_gpu_polyline_xy(&blns, xy, an + 1u, false, &reset);
            }
            blns_gpu_line_solid(&blns);
        }
        // the needle: a wireframe triangle from the hub to near the ring
        const unsigned na = mn_angle(v16);
        mn_polar(&xy[0], na, GA_R - 7);
        mn_polar(&xy[2], na + 64u, 4);
        mn_polar(&xy[4], na - 64u, 4);
        colour(RGB(255, 255, 255));
        blns_gpu_polyline_xy(&blns, xy, 3u, true, NULL);
        blns_gpu_rect_outline_xy(&blns, GA_CX - 2, GA_CY - 2, 5u, 5u, NULL);
        blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
        // its reading under the dial, opaque on the card's ramp
        const unsigned v = (v16 + 8u) / 16u;
        num[5] = (char) ((v >= 100u) ? '1' : ' ');
        num[6] = (char) ((v >= 10u) ? '0' + v / 10u % 10u : ' ');
        num[7] = (char) ('0' + v % 10u);
        const int ty = GA_Y + GA_S + 8;
        text(GA_CX - 9 * (int) MONO_W / 2, ty, num, RGB(255, 255, 255),
             ramp(MN_C0, MN_C1, (unsigned) (ty - MN_Y), (unsigned) MN_H - 1u), true);
        if (frame_end())
            return;
    }
}

//==========================================================
// SET-UP
//==========================================================
static void (*const scene[])(void) = { scene_dashboard, scene_equalizer, scene_colour, scene_monitor };

int main(void)
{
    sin_init();
    inset_init();
    demo_init();
    blns_set_mode(&blns, BLNS_BPP_16);
    volatile uint8_t *v = blns_vram8(&blns) + FONT_BYTE;
    for (unsigned i = 0; i < sizeof(mono_bits); i++)
        v[i] = mono_bits[i];
    if (!blns.caps.gpu) {                       // a bitstream without the GPU
        background();
        text(40, 112, "NEEDS THE GPU", RGB(255, 80, 80), 0, false);
        for (;;)
            wait_frames(1);
    }
    demo_run(scene, sizeof(scene) / sizeof(scene[0]));
}

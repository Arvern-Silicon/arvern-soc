//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      screen.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// One frame in the VRAM (F0), drawn in place: the background and the panels
// once, the text when it changes (the CPU, 8 x 16 font), the bars every frame
// by the GPU. A bar's column is redrawn whole by two commands: a fill of the
// empty part and a copy of the lit part from a sprite of the full bar (in the
// spare VRAM), whose lines carry the colour ramp, one line in two; the peak
// cap is a fill on top, and the reflection under the floor another sprite.
// The bars are drawn just after a refresh starts, ahead of the scan.
//----------------------------------------------------------------------------
#include <string.h>
#include "demo_kit.h"
#include "font_mono.h"
#include "screen.h"

// Palette
#define GRAD      0u                        // 0-31: the background gradient (the boot menu's)
#define GRAD_NR   32u
#define C_PANEL   32u
#define C_EDGE    33u
#define C_HI      34u
#define C_TEXT    35u
#define C_DIM     36u
#define C_ORANGE  37u
#define C_PEAK    38u
#define BAR       64u                       // 64-127: the bars' ramp
#define BAR_NR    64u

// Layout
#define TOP_Y     6u
#define VIS_Y     38u
#define VIS_H     176u
#define BOT_Y     218u
#define HELP_Y    248u
#define HELP_H    22u
#define PANEL_X   10u
#define PANEL_W   460u
#define PANEL_H   28u
#define BAR_X0    32u
#define BAR_Y0    48u
#define BAR_H     132u
#define BAR_STEP  13u                       // bar pitch: 11 lit, 2 apart
#define BAR_W     11u
#define REFL_Y    (BAR_Y0 + BAR_H + 2u)
#define REFL_H    24u
#define PROG_X    20u
#define PROG_W    206u
#define PROG_Y    230u
#define TIME_X    234u
#define LOAD_X    342u
#define OUT_X     414u
#define TEXT_DY   6u                        // text line inside a panel

// Sprites in the spare VRAM
#define SPR_BAR   VRAM_SPARE
#define SPR_REFL  (SPR_BAR + BAR_W * BAR_H)

static volatile uint8_t *vram;
static uint16_t smooth[SPEC_BANDS];         // displayed level, Q8 of a bar height
static uint32_t peak[SPEC_BANDS], vel[SPEC_BANDS];   // Q16 of a bar height
static uint8_t  hold[SPEC_BANDS];
static uint32_t shown_s = ~0u;
static unsigned shown_load = ~0u;
static int      bars_on;

static void rect(unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c)
{
    for (unsigned r = 0; r < h; r++)
        memset((uint8_t *) (vram + (y + r) * SCR_W + x), c, w);
}

static void glass(unsigned x, unsigned y, unsigned w, unsigned h)
{
    rect(x, y, w, h, C_PANEL);
    rect(x, y, w, 1u, C_HI);
    rect(x, y + h - 1u, w, 1u, C_EDGE);
    rect(x, y + 1u, 1u, h - 2u, C_EDGE);
    rect(x + w - 1u, y + 1u, 1u, h - 2u, C_EDGE);
}

// Text on the panel colour; returns the x after it
static unsigned text(unsigned x, unsigned y, const char *s, uint8_t fg, unsigned max)
{
    for (; *s && max; s++, max--, x += FONT_W) {
        const unsigned c = (unsigned char) *s;
        const uint8_t *g = &font_mono[((c >= 32u && c <= 126u) ? c - 32u : '?' - 32u) * FONT_H];
        for (unsigned r = 0; r < FONT_H; r++) {
            volatile uint8_t *p = vram + (y + r) * SCR_W + x;
            for (unsigned b = 0; b < FONT_W; b++)
                p[b] = ((g[r] >> b) & 1u) ? fg : C_PANEL;
        }
    }
    return x;
}

static char *put_num(char *p, uint32_t v)
{
    char d[10];
    unsigned n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *p++ = d[--n];
    return p;
}

static char *put_time(char *p, uint32_t s)
{
    p = put_num(p, s / 60u);
    *p++ = ':';
    *p++ = (char) ('0' + (s % 60u) / 10u);
    *p++ = (char) ('0' + s % 10u);
    return p;
}

static void palette(void)
{
    static const uint32_t ramp[6] = { 0x0E3A6Au, 0x1F8FB8u, 0x7FE0E8u, 0xF4F8FFu, 0xF0A030u, 0xE2702Au };
    for (unsigned i = 0; i < GRAD_NR; i++)
        pal[GRAD + i] = rgb_lerp(0x050812u, 0x0C1830u, i, GRAD_NR - 1u);
    pal[C_PANEL]  = 0x0B111Cu;
    pal[C_EDGE]   = 0x2A3A58u;
    pal[C_HI]     = 0x3E5680u;
    pal[C_TEXT]   = 0xE8E8E8u;
    pal[C_DIM]    = 0x8494A4u;
    pal[C_ORANGE] = 0xE2702Au;
    pal[C_PEAK]   = 0xF4F8FFu;
    for (unsigned i = 0; i < BAR_NR; i++) {           // five segments of the ramp
        const unsigned seg = i * 5u / BAR_NR, t = i * 5u - seg * BAR_NR;
        pal[BAR + i] = rgb_lerp(ramp[seg], ramp[seg + 1u], t, BAR_NR);
    }
}

static void bars_clear(void)
{
    fill((VIS_Y + 1u) * SCR_W + PANEL_X + 1u, PANEL_W - 2u, VIS_H - 2u, C_PANEL);
    blns_gpu_wait(&blns);
    memset(smooth, 0, sizeof(smooth));
    memset(peak, 0, sizeof(peak));
}

void screen_init(void)
{
    palette();
    demo_init();
    vram = blns_vram8(&blns);
    for (unsigned y = 0; y < SCR_H; y++)
        memset((uint8_t *) (vram + y * SCR_W), GRAD + y * GRAD_NR / SCR_H, SCR_W);
    glass(PANEL_X, TOP_Y, PANEL_W, PANEL_H);
    glass(PANEL_X, VIS_Y, PANEL_W, VIS_H);
    glass(PANEL_X, BOT_Y, PANEL_W, PANEL_H);
    glass(PANEL_X, HELP_Y, PANEL_W, HELP_H);
    rect(PROG_X, PROG_Y, PROG_W, 4u, C_EDGE);

    // The full bar, lit one line in two from its foot (row r from the foot:
    // lit when odd, its colour by height), and the reflection, lit when even
    for (unsigned y = 0; y < BAR_H; y++) {
        const unsigned r = BAR_H - 1u - y;
        const uint8_t  c = (r & 1u) ? (uint8_t) (BAR + r * BAR_NR / BAR_H) : C_PANEL;
        memset((uint8_t *) (vram + SPR_BAR + y * BAR_W), c, BAR_W);
    }
    for (unsigned y = 0; y < REFL_H; y++)
        memset((uint8_t *) (vram + SPR_REFL + y * BAR_W), (y & 1u) ? C_PANEL : BAR + 4u, BAR_W);
}

void screen_track(const char *title, const char *artist, unsigned idx, unsigned n,
                  const char *format, uint32_t rate, unsigned channels)
{
    char     l[48], *p = l;
    unsigned x;

    blns_gpu_wait(&blns);
    rect(PANEL_X + 1u, TOP_Y + 1u, PANEL_W - 2u, PANEL_H - 2u, C_PANEL);
    // right: "WAV 44.1k stereo"
    while (*format && p < l + 6)
        *p++ = *format++;
    *p++ = ' ';
    const uint32_t r = (rate + 5u) / 10u;            // in 10 Hz: 22050 -> "22.05"
    p = put_num(p, r / 100u);
    if (r % 100u) {
        *p++ = '.';
        *p++ = (char) ('0' + (r % 100u) / 10u);
        if (r % 10u)
            *p++ = (char) ('0' + r % 10u);
    }
    memcpy(p, (channels > 1u) ? "k stereo" : "k mono", (channels > 1u) ? 9u : 7u);
    const unsigned rx = PANEL_X + PANEL_W - 10u - FONT_W * (unsigned) strlen(l);
    (void) text(rx, TOP_Y + TEXT_DY, l, C_DIM, 24u);
    // left: "3/12  Title - Artist", cut to fit
    p = put_num(l, idx);
    *p++ = '/';
    p = put_num(p, n);
    *p = 0;
    x = text(PANEL_X + 10u, TOP_Y + TEXT_DY, l, C_DIM, 8u) + 2u * FONT_W;
    const unsigned room = (rx - FONT_W - x) / FONT_W;
    x = text(x, TOP_Y + TEXT_DY, title, C_TEXT, room);
    if (artist[0] && (x + 4u * FONT_W < rx - FONT_W)) {
        x = text(x, TOP_Y + TEXT_DY, " - ", C_DIM, 3u);
        (void) text(x, TOP_Y + TEXT_DY, artist, C_DIM, (rx - FONT_W - x) / FONT_W);
    }
    shown_s = ~0u;
    if (!bars_on) {
        bars_clear();
        bars_on = 1;
    }
}

void screen_message(const char *line1, const char *line2)
{
    bars_clear();
    bars_on = 0;
    const unsigned w1 = (unsigned) strlen(line1), w2 = (unsigned) strlen(line2);
    (void) text((SCR_W - w1 * FONT_W) / 2u, VIS_Y + VIS_H / 2u - FONT_H - 2u, line1, C_TEXT, 50u);
    (void) text((SCR_W - w2 * FONT_W) / 2u, VIS_Y + VIS_H / 2u + 2u, line2, C_DIM, 50u);
}

void screen_sound(const char *name, unsigned volume)
{
    char t[8];
    memcpy(t, name, 4u);
    t[4] = ' ';
    t[5] = (char) ('0' + volume);
    t[6] = 0;
    rect(OUT_X, BOT_Y + TEXT_DY, PANEL_X + PANEL_W - 2u - OUT_X, FONT_H, C_PANEL);
    (void) text(OUT_X, BOT_Y + TEXT_DY, t, C_ORANGE, 6u);
}

void screen_help(const char *line)
{
    rect(PANEL_X + 1u, HELP_Y + 3u, PANEL_W - 2u, FONT_H, C_PANEL);
    (void) text((SCR_W - FONT_W * (unsigned) strlen(line)) / 2u, HELP_Y + 3u, line, C_DIM, 56u);
}

static void bar(unsigned b, unsigned level)
{
    const unsigned x = BAR_X0 + b * BAR_STEP;
    // fast attack, a smooth fall (x 0.82 a frame)
    smooth[b] = (uint16_t) ((level >= ((smooth[b] * 210u) >> 8)) ? level : (smooth[b] * 210u) >> 8);
    const unsigned h = (smooth[b] * BAR_H + 128u) >> 8, rh = (h / 4u < REFL_H) ? h / 4u : REFL_H;

    const blns_surf_t col  = { BAR_Y0 * SCR_W + x, 0, BLNS_SWAP_NONE };
    const blns_surf_t lit  = { (BAR_Y0 + BAR_H - h) * SCR_W + x, 0, BLNS_SWAP_NONE };
    const blns_surf_t sbar = { SPR_BAR + (BAR_H - h) * BAR_W, BAR_W, BLNS_SWAP_NONE };
    if (h < BAR_H)
        (void) blns_gpu_fill(&blns, &col, BAR_W, (uint16_t) (BAR_H - h), C_PANEL, BLNS_ROP_S);
    if (h)
        (void) blns_gpu_copy(&blns, &sbar, &lit, BAR_W, (uint16_t) h, BLNS_ROP_S);

    const blns_surf_t refl = { REFL_Y * SCR_W + x, 0, BLNS_SWAP_NONE };
    const blns_surf_t rest = { (REFL_Y + rh) * SCR_W + x, 0, BLNS_SWAP_NONE };
    const blns_surf_t srfl = { SPR_REFL, BAR_W, BLNS_SWAP_NONE };
    if (rh)
        (void) blns_gpu_copy(&blns, &srfl, &refl, BAR_W, (uint16_t) rh, BLNS_ROP_S);
    if (rh < REFL_H)
        (void) blns_gpu_fill(&blns, &rest, BAR_W, (uint16_t) (REFL_H - rh), C_PANEL, BLNS_ROP_S);

    // the peak: held 10 frames, then falling faster and faster
    const uint32_t hq = (uint32_t) h << 16;
    if (hq >= peak[b]) {
        peak[b] = hq;
        vel[b]  = 0u;
        hold[b] = 10u;
    } else if (hold[b]) {
        hold[b]--;
    } else {
        vel[b] += (BAR_H << 16) / 250u;
        peak[b] = (peak[b] > vel[b]) ? peak[b] - vel[b] : 0u;
    }
    unsigned py = BAR_Y0 + BAR_H - (peak[b] >> 16);
    py = (py >= BAR_Y0 + 3u) ? py - 3u : BAR_Y0;
    const blns_surf_t cap = { py * SCR_W + x, 0, BLNS_SWAP_NONE };
    (void) blns_gpu_fill(&blns, &cap, BAR_W, 2u, C_PEAK, BLNS_ROP_S);
}

void screen_frame(const uint8_t *level, uint32_t elapsed_s, uint32_t total_s, unsigned load, int paused)
{
    if (paused)
        load = 1000u;                                   // the field shows "Paused"
    if (bars_on)
        for (unsigned b = 0; b < SPEC_BANDS; b++)
            bar(b, level[b]);

    // the progress: the bar and its knob
    const unsigned w = (total_s && elapsed_s < total_s) ? elapsed_s * PROG_W / total_s : (total_s ? PROG_W : 0u);
    rect(PROG_X, PROG_Y - 2u, PROG_W + 3u, 8u, C_PANEL);
    rect(PROG_X, PROG_Y, PROG_W, 4u, C_EDGE);
    rect(PROG_X, PROG_Y, w, 4u, C_ORANGE);
    rect(PROG_X + w, PROG_Y - 2u, 3u, 8u, C_PEAK);

    if (elapsed_s != shown_s) {
        char t[16], *p = put_time(t, elapsed_s);
        *p++ = ' ';
        *p++ = '/';
        *p++ = ' ';
        p = put_time(p, total_s);
        while (p < t + 13)
            *p++ = ' ';
        *p = 0;
        (void) text(TIME_X, BOT_Y + TEXT_DY, t, C_TEXT, 13u);
        shown_s = elapsed_s;
    }
    if (load != shown_load) {
        char t[12] = "CPU ", *p = put_num(t + 4, (load > 100u) ? 100u : load);
        *p++ = '%';
        while (p < t + 8)
            *p++ = ' ';
        *p = 0;
        if (load == 1000u)
            memcpy(t, "Paused  ", 9u);
        (void) text(LOAD_X, BOT_Y + TEXT_DY, t, C_DIM, 8u);
        shown_load = load;
    }
}

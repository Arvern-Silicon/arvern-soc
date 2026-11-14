//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K text demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Text on Belenos: every glyph is one GPU EXEC_EXPAND, so the CPU only pushes
// a few FIFO words per character: three from a font kept in the VRAM, or the
// glyph's own words from ROM behind it (the immediate source, see txt.h).
//
//   1 console     1 bpp font, opaque, typed in, scrolled by a GPU copy
//   2 typography  2 bpp antialiased text, a 4 bpp headline shimmering (LUT_ROT)
//   3 rotation    a marquee running round the screen: text in four directions
//   4 rotozoom    the title spun and zoomed in by EXEC_TEXSPAN
//   5 credits
//----------------------------------------------------------------------------
#include "txt.h"
#include "text_data.h"

//==========================================================
// PALETTE
//==========================================================
#define C_BG             0u                    // background
#define C_GREEN          1u                    // console colours (1 bpp)
#define C_AMBER          2u
#define C_WHITE          3u
#define C_CYAN           4u
#define C_DIM            5u
#define R_WHITE          8u                    // 2 bpp ramps: 3 entries each
#define R_ORANGE         11u
#define R_CYAN           14u
#define R_GOLD           17u
#define G_CHROME         32u                   // 4 bpp headline: 15 entries
#define G_BANDS          64u                   // background gradient: 32 entries

static void ramp(unsigned at, uint32_t bg, uint32_t ink)
{
    for (unsigned l = 1; l <= 3u; l++)
        pal[at + l - 1u] = rgb_lerp(bg, ink, l, 3);
}

static void palette(uint32_t bg)
{
    pal[C_BG]    = bg;
    pal[C_GREEN] = 0x50ff80u;
    pal[C_AMBER] = 0xffb030u;
    pal[C_WHITE] = 0xf0f0f0u;
    pal[C_CYAN]  = 0x40e0ffu;
    pal[C_DIM]   = 0x506070u;
    ramp(R_WHITE,  bg, 0xf4f4f4u);
    ramp(R_ORANGE, bg, 0xf08030u);
    ramp(R_CYAN,   bg, 0x50d8ffu);
    ramp(R_GOLD,   bg, 0xffd060u);
    // chrome: bright at the top, dark in the middle, bright again
    for (unsigned i = 0; i < 15u; i++) {
        unsigned d = (i < 7u) ? i : 14u - i;
        pal[G_CHROME + i] = rgb_lerp(0xfff4d0u, 0x8a5a20u, d, 7);
    }
    for (unsigned i = 0; i < 32u; i++)
        pal[G_BANDS + i] = rgb_lerp(0x141c40u, 0x3a1446u, i, 31);
}

static void clear(uint32_t frame, uint8_t colour)
{
    fill(frame, SCR_W, SCR_H, colour);
}

static unsigned centre(const font_t *f, const char *s)
{
    return (SCR_W - txt_width(f, s)) / 2u;
}

//==========================================================
// 1: CONSOLE
//==========================================================
static const char *const boot[] = {
    "aRVern boot ROM 1.0.1",
    "hart 0: RV32EMC_Zba_Zbb_Zbs_Zbc_Zcb",
    "M mode, RISC-V external debug",
    "33 MHz, ROM 160 KB, RAM 128 KB",
    "",
    "Belenos graphic controller 1.0",
    "VRAM 272 KB, LUT 2 x 256, DPI panel 480 x 272",
    "GPU: fill copy expand affine gradient",
    "remap, mask plane, immediate data",
    "",
    "> cat /proc/this_console",
    "Each character is one EXEC_EXPAND:",
    "1 bit per pixel, opaque, 8 x 16 cell,",
    "its 4 words pushed from ROM behind it.",
    "The CPU moves on; the GPU draws it.",
    "A screen of 60 x 17 characters;",
    "scrolling is one GPU copy.",
    "",
    "> run demo_text",
    "Ready.",
};
#define BOOT_NR          (sizeof(boot) / sizeof(boot[0]))
#define CON_COLS         (SCR_W / MONO_W)
#define CON_ROWS         (SCR_H / MONO_H)

static void scene_console(void)
{
    const blns_surf_t up_src = { F0 + MONO_H * SCR_W, 0, BLNS_SWAP_NONE };
    const blns_surf_t up_dst = { F0, 0, BLNS_SWAP_NONE };
#ifdef SIMULATION
    const unsigned per_frame = 12;
#else
    const unsigned per_frame = 2;
#endif
    unsigned row = 0;

    palette(0x020604u);
    pal_show();
    clear(F0, C_BG);
    txt_upload();

    for (unsigned l = 0; l < BOOT_NR; l++) {
        const char *s = boot[l];
        uint8_t col = (s[0] == '>') ? C_AMBER : (l == 0u || l == 5u) ? C_WHITE : C_GREEN;
        if (row == CON_ROWS) {                            // scroll one line up
            blns_gpu_copy(&blns, &up_src, &up_dst, SCR_W, SCR_H - MONO_H, BLNS_ROP_S);
            fill(F0 + (SCR_H - MONO_H) * SCR_W, SCR_W, MONO_H, C_BG);
            row--;
        }
        unsigned x = 0;
        for (; s[x]; x++) {
            char one[2] = { s[x], 0 };
            txt_draw(&font_mono, txt_at(F0, x * MONO_W, row * MONO_H), one, col, C_BG, true, TXT_RIGHT);
            // cursor after the character
            fill(txt_at(F0, (x + 1u) * MONO_W, row * MONO_H), MONO_W, MONO_H, C_DIM);
            if ((x % per_frame) == per_frame - 1u && scene_frame())
                return;
        }
        fill(txt_at(F0, x * MONO_W, row * MONO_H), MONO_W, MONO_H, C_BG);
        row++;
        for (unsigned f = 0; f < FRAMES(12u); f++)
            if (scene_frame())
                return;
    }
    // blinking cursor
    for (unsigned f = 0; f < FRAMES(120u); f++) {
        fill(txt_at(F0, 6u * MONO_W, (row - 1u) * MONO_H), MONO_W, MONO_H, ((f / 20u) & 1u) ? C_BG : C_GREEN);
        if (scene_frame())
            return;
    }
}

//==========================================================
// 2: TYPOGRAPHY
//==========================================================
static const char *const para[] = {                 // PARA_COLS cells at most
    "Antialiased text, 2 bits",
    "per pixel, on a palette",
    "ramp mapped by the GPU.",
    "",
    "Headline: 4 bits per pixel,",
    "15 chrome steps shimmering",
    "by palette rotation.",
};

#define PARA_NR          (sizeof(para) / sizeof(para[0]))
#define PARA_COLS        29u
#define PARA_X           ((SCR_W - PARA_COLS * SANS_W) / 2u)
// headline, 16, paragraphs, 12, console line: centred vertically
#define TYPO_Y           ((SCR_H - (BIG_H + 16u + PARA_NR * SANS_H + 12u + MONO_H)) / 2u)
#define TYPO_PARA_Y      (TYPO_Y + BIG_H + 16u)

static void scene_typography(void)
{
    const uint32_t mid = 0x271842u;

    palette(mid);
    for (unsigned i = 0; i < 32u; i++)
        fill(F0 + (i * SCR_H / 32u) * SCR_W, SCR_W, (i + 1u) * SCR_H / 32u - i * SCR_H / 32u,
             (uint8_t) (G_BANDS + i));
    pal_show();
    txt_upload();

    txt_draw(&font_big, txt_at(F0, centre(&font_big, "TYPOGRAPHY"), TYPO_Y), "TYPOGRAPHY",
             G_CHROME, 0, false, TXT_RIGHT);
    for (unsigned i = 0; i < PARA_NR; i++) {
        uint8_t col = (i < 3u) ? R_WHITE : R_GOLD;
        txt_draw(&font_sans, txt_at(F0, PARA_X, TYPO_PARA_Y + i * SANS_H), para[i], col, 0, false, TXT_RIGHT);
    }
    txt_draw(&font_mono, txt_at(F0, PARA_X, TYPO_PARA_Y + PARA_NR * SANS_H + 12u), "1 bpp console font, 8 x 16 cells", C_CYAN, 0, false, TXT_RIGHT);

    for (unsigned f = 0; f < FRAMES(420u); f++) {
        blns_lut_rotate(&blns, G_CHROME, G_CHROME + 14u, (uint8_t) ((f / 2u) % 15u));
        if (scene_frame())
            break;
    }
    blns_lut_rotate(&blns, 0, 0, 0);
}

//==========================================================
// 3: ROTATION
//==========================================================
// A ring of characters round the screen edge, clockwise: the top row left to
// right, the right column downwards, the bottom row upside down, the left
// column upwards
static const char marquee[] =
    "TEXT IN ANY DIRECTION * EXEC_EXPAND DESTINATION SWAPS * "
    "ROTATED 90, 180, 270 DEGREES * THE SAME FONT, THE SAME GLYPHS * ";
#define RING_TOP         (SCR_W / MONO_W)
#define RING_SIDE        ((SCR_H - 2u * MONO_H) / MONO_W)
#define RING_NR          (2u * RING_TOP + 2u * RING_SIDE)

static void ring_part(char *buf, unsigned from, unsigned n, unsigned shift)
{
    for (unsigned i = 0; i < n; i++)
        buf[i] = marquee[(from + i + shift) % (sizeof(marquee) - 1u)];
    buf[n] = 0;
}

static void scene_rotation(void)
{
    char buf[RING_TOP + 1u];

    palette(0x0a0a18u);
    pal_show();
    clear(F0, C_BG);
    txt_upload();
    txt_draw(&font_big, txt_at(F0, centre(&font_big, "ROTATED"), SCR_H / 2u - 50u), "ROTATED", G_CHROME, 0, false, TXT_RIGHT);
    txt_draw(&font_sans, txt_at(F0, centre(&font_sans, "the destination walk turns"), SCR_H / 2u),
             "the destination walk turns", R_WHITE, 0, false, TXT_RIGHT);
    txt_draw(&font_sans, txt_at(F0, centre(&font_sans, "each glyph as it is drawn"), SCR_H / 2u + 22u),
             "each glyph as it is drawn", R_CYAN, 0, false, TXT_RIGHT);

    for (unsigned f = 0, shift = 0; f < FRAMES(480u); f++) {
        if ((f & 3u) == 0u) {
            ring_part(buf, 0, RING_TOP, shift);
            txt_draw(&font_mono, txt_at(F0, 0, 0), buf, C_AMBER, C_BG, true, TXT_RIGHT);
            ring_part(buf, RING_TOP, RING_SIDE, shift);
            txt_draw(&font_mono, txt_at(F0, SCR_W - MONO_H, MONO_H), buf, C_GREEN, C_BG, true, TXT_DOWN);
            ring_part(buf, RING_TOP + RING_SIDE, RING_TOP, shift);
            txt_draw(&font_mono, txt_at(F0, 0, SCR_H - MONO_H), buf, C_AMBER, C_BG, true, TXT_LEFT);
            ring_part(buf, 2u * RING_TOP + RING_SIDE, RING_SIDE, shift);
            txt_draw(&font_mono, txt_at(F0, 0, MONO_H), buf, C_GREEN, C_BG, true, TXT_UP);
            shift++;
        }
        if (scene_frame())
            break;
    }
}

//==========================================================
// 4: ROTOZOOM
//==========================================================
// The title as an 8 bpp image at VRAM_SPARE (TITLE_W x TITLE_H, pitch TITLE_W,
// 0 outside the ink), drawn rotated and scaled as one affine blit over the
// screen box that holds the turned image: the GPU skips the texels outside the
// image (tex.bound: a free-pitch texture would otherwise be read on, past the
// end of the VRAM, GPU error 5), and the 0 texels (transparent).
#define Q16              65536

// Q16 step -> 5.11, rounded to the nearest: the GPU adds the step once a
// pixel or a row, so a truncated step drifts one way by up to 1/2048 texel
// each time, a rounded one by half that either way
static int16_t step_511(int32_t d_q16)
{
    int32_t d = (int32_t) (((int64_t) d_q16 + 16) >> 5);
    return (int16_t) ((d > 0x7FFF) ? 0x7FFF : (d < -0x8000) ? -0x8000 : d);
}

static void rotozoom(uint32_t frame, unsigned angle, int32_t k, uint8_t shade)
{
    // texel = centre + k * R(-angle) * (pixel - screen centre), Q16
    const int32_t c = icos(angle), s = isin(angle);
    const int32_t dux = (int32_t) (((int64_t) c * k) >> 14), dvx = (int32_t) (((int64_t) -s * k) >> 14);
    const int32_t duy = (int32_t) (((int64_t) s * k) >> 14), dvy = (int32_t) (((int64_t) c * k) >> 14);
    const blns_tex_t tex = { .addr = VRAM_SPARE, .pitch = TITLE_W, .free_pitch = true,
                             .transp = true, .transparent = 0, .swap = BLNS_SWAP_NONE,
                             .bound = true, .box_w = TITLE_W, .box_h = TITLE_H };

    // The screen box: the image's corners through the inverse map, pixel -
    // screen centre = M^-1 (texel - image centre), M = (dux duy; dvx dvy), a
    // pixel of margin each side
    const int64_t det = (int64_t) dux * dvy - (int64_t) duy * dvx;
    int x0 = (int) SCR_W, y0 = (int) SCR_H, x1 = -1, y1 = -1;
    if (det <= 0)
        return;
    for (unsigned i = 0; i < 4u; i++) {
        const int64_t tu = (int64_t) ((i & 1u) ? (int) TITLE_W / 2 : -(int) TITLE_W / 2) * Q16;
        const int64_t tv = (int64_t) ((i & 2u) ? (int) TITLE_H / 2 : -(int) TITLE_H / 2) * Q16;
        const int     x  = (int) (SCR_W / 2u) + (int) ((dvy * tu - duy * tv) / det);
        const int     y  = (int) (SCR_H / 2u) + (int) ((dux * tv - dvx * tu) / det);
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    x0 = (x0 > 1) ? x0 - 2 : 0;
    y0 = (y0 > 1) ? y0 - 2 : 0;
    x1 = (x1 + 2 < (int) SCR_W) ? x1 + 2 : (int) SCR_W - 1;
    y1 = (y1 + 2 < (int) SCR_H) ? y1 + 2 : (int) SCR_H - 1;
    if (x1 < x0 || y1 < y0)
        return;
    // the box's first pixel: signed 10.6 with bound
    const int64_t u0 = (int64_t) (TITLE_W / 2u) * Q16 + (int64_t) dux * (x0 - (int) SCR_W / 2) +
                       (int64_t) duy * (y0 - (int) SCR_H / 2);
    const int64_t v0 = (int64_t) (TITLE_H / 2u) * Q16 + (int64_t) dvx * (x0 - (int) SCR_W / 2) +
                       (int64_t) dvy * (y0 - (int) SCR_H / 2);
    blns_gpu_xy_begin(&blns, frame, 0, 0);
    blns_gpu_clip(&blns, 0, 0, (int) SCR_W - 1, (int) SCR_H - 1);
    blns_gpu_tex_set(&blns, &tex, shade);
    blns_gpu_texrect_xy(&blns, x0, y0, (uint16_t) (x1 - x0 + 1), (uint16_t) (y1 - y0 + 1),
                        (uint16_t) (int16_t) (u0 >> 10), (uint16_t) (int16_t) (v0 >> 10),
                        step_511(dux), step_511(dvx), step_511(duy), step_511(dvy));
}

static void scene_rotozoom(void)
{
    const unsigned spin = FRAMES(240u), total = FRAMES(420u);
    uint32_t back = F1;

    palette(0x06040cu);
    pal_show();
    clear(F0, C_BG);
    txt_upload();
    txt_title_image(R_WHITE);                         // ramp 8..10: SHADE picks the ramp

    for (unsigned f = 0; f < total; f++) {
        unsigned angle = 0;
        int32_t  k     = Q16;                        // texels per pixel
        if (f < spin) {
            unsigned r = spin - f;                    // frames left until rest
            angle = (r * r * 256u / (spin * spin)) * 3u;
            k     = Q16 + (int32_t) ((int64_t) 14 * Q16 * r * r / ((int64_t) spin * spin));   // up to 15 texels per pixel
        } else {
            k = Q16 * 3 / 4 + (int32_t) ((Q16 / 4) * icos((f - spin) * 2u) >> 14) / 4;
        }
        uint8_t shade = (uint8_t) (3u * ((f / FRAMES(60u)) & 3u));   // white, orange, cyan, gold
        clear(back, C_BG);
        rotozoom(back, angle, k, shade);
        blns_gpu_wait(&blns);
        show_frame(back);
        back = (back == F0) ? F1 : F0;
        if (scene_check())
            break;
    }
    blns_gpu_wait(&blns);
    show_frame(F0);
}

//==========================================================
// 5: CREDITS
//==========================================================
static void scene_credits(void)
{
    palette(0x0a0a18u);
    pal_show();
    clear(F0, C_BG);
    txt_upload();
    txt_draw(&font_big, txt_at(F0, centre(&font_big, "THANK YOU"), SCR_H / 2u - 60u), "THANK YOU", G_CHROME, 0, false, TXT_RIGHT);
    txt_draw(&font_sans, txt_at(F0, centre(&font_sans, "aRVern RISC-V core + Belenos"), SCR_H / 2u),
             "aRVern RISC-V core + Belenos", R_ORANGE, 0, false, TXT_RIGHT);
    txt_draw(&font_sans, txt_at(F0, centre(&font_sans, "fonts: IBM Plex Sans"), SCR_H / 2u + 26u),
             "fonts: IBM Plex Sans", R_WHITE, 0, false, TXT_RIGHT);
    txt_draw(&font_mono, txt_at(F0, centre(&font_mono, "github.com/Arvern-Silicon"), SCR_H / 2u + 70u),
             "github.com/Arvern-Silicon", C_CYAN, 0, false, TXT_RIGHT);
    for (unsigned f = 0; f < FRAMES(300u); f++) {
        blns_lut_rotate(&blns, G_CHROME, G_CHROME + 14u, (uint8_t) ((f / 2u) % 15u));
        if (scene_frame())
            break;
    }
    blns_lut_rotate(&blns, 0, 0, 0);
}

static void (*const scene[])(void) = {
    scene_console, scene_typography, scene_rotation, scene_rotozoom, scene_credits,
};

int main(void)
{
    sin_init();
    palette(0);
    demo_init();
    demo_run(scene, sizeof(scene) / sizeof(scene[0]));
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      scenes.c  (DE0-Nano-SoC aRVern logo demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The demo scenes. Each one shows a Belenos feature; it draws in F0 unless it
// says otherwise and leaves the title screen (gradient, logo, wordmark,
// subtitle) in F0 for the next one.
//----------------------------------------------------------------------------
#include "gfx.h"
#include "scenes.h"
#include "logo_data.h"

// Title screen layout
#define LOGO_X           ((SCR_W - LOGO_W) / 2u)
#define LOGO_Y           12u
#define WORD_X           ((SCR_W - WORDMARK_W) / 2u)
#define WORD_Y           (LOGO_Y + LOGO_H + 10u)
#define SUB_X            ((SCR_W - TXT_SUB_W) / 2u)
#define SUB_Y            (WORD_Y + WORDMARK_H + 12u)

static void draw_title(uint32_t frame)
{
    draw_gradient(frame);
    draw_logo_layers(frame + LOGO_Y * SCR_W + LOGO_X, SCR_W, 0, LOGO_LAYERS);
    draw_text(TXT_WORDMARK, frame + WORD_Y * SCR_W + WORD_X, PAL_WHITE, WORDMARK_W);
    draw_text(TXT_SUB, frame + SUB_Y * SCR_W + SUB_X, PAL_ORANGE, TXT_SUB_W);
}

//==========================================================
// 1: PAINT-IN
//==========================================================
// The background gradient is filled by the GPU, then the logo is painted one
// colour layer at a time (one GPU fill per run of pixels). The wordmark is
// typed in by GPU colour expansion, a few columns per frame, then the subtitle
// appears.

void scene_paint(void)
{
    const uint32_t logo = F0 + LOGO_Y * SCR_W + LOGO_X;
    unsigned per_frame;

    pal_theme(0);
    pal_show();
    draw_gradient(F0);
    if (scene_frame())
        goto done;

    per_frame = (LOGO_LAYERS + FRAMES(90u) - 1u) / FRAMES(90u);     // about 1.5 s
    for (unsigned l = 0; l < LOGO_LAYERS; l += per_frame) {
        draw_logo_layers(logo, SCR_W, l, l + per_frame);
        if (scene_frame())
            goto done;
    }

    per_frame = (WORDMARK_W + FRAMES(40u) - 1u) / FRAMES(40u);
    for (unsigned c = per_frame; c < WORDMARK_W + per_frame; c += per_frame) {
        draw_text(TXT_WORDMARK, F0 + WORD_Y * SCR_W + WORD_X, PAL_WHITE, c);
        if (scene_frame())
            goto done;
    }
    draw_text(TXT_SUB, F0 + SUB_Y * SCR_W + SUB_X, PAL_ORANGE, TXT_SUB_W);

    for (unsigned f = 0; f < FRAMES(90u); f++)
        if (scene_frame())
            break;
    return;
done:
    // Skipped: leave the complete title screen for the next scene
    draw_title(F0);
}

//==========================================================
// 2: POWER-UP
//==========================================================
// Each circuit trace has its own LUT entry, numbered from the outermost trace
// to the RV mark. Pulses of light run along that order: per frame the CPU
// rewrites 24 palette entries and nothing else; the VRAM is untouched. Once
// the base level has risen, the pulse pattern stays in the LUT and LUT_ROT
// turns it: one register write per frame.

#define TRACE_DIM        0x0e2236u
#define TRACE_BASE_RGB   0x4286bdu
#define TRACE_PEAK       0xd8f6ffu
#define PULSE_LEN        6u                    // traces lit behind a pulse front
#define PULSE_GAP        12u                   // trace positions between pulses (divides LOGO_TRACE_NR)

static void traces_set(unsigned front, uint32_t base_rgb)
{
    for (unsigned k = 0; k < LOGO_TRACE_NR; k++) {
        // distance behind the nearest pulse front, in trace positions
        unsigned d = (front + PULSE_GAP * 4u - k) % PULSE_GAP;
        pal[LOGO_TRACE_BASE + k] = (d < PULSE_LEN) ? rgb_lerp(TRACE_PEAK, base_rgb, d, PULSE_LEN)
                                                   : base_rgb;
    }
}

void scene_power(void)
{
    const unsigned steps = FRAMES(300u);

    pal_theme(0);
    // Power off: every trace dark, then pulses, the base level rising
    for (unsigned f = 0; f < steps; f++) {
        unsigned rise  = (f < steps / 2u) ? f : steps / 2u;
        unsigned front = f * 2u / ((steps < 32u) ? 1u : 4u);
        if (f >= steps / 2u) {
            // entry k + offset holds the colour trace k needs: the pattern of front 0
            unsigned ofs = (LOGO_TRACE_NR - front % LOGO_TRACE_NR) % LOGO_TRACE_NR;
            blns_lut_rotate(&blns, LOGO_TRACE_BASE, LOGO_TRACE_BASE + LOGO_TRACE_NR - 1u, (uint8_t) ofs);
            if (f == steps / 2u) {
                traces_set(0, TRACE_BASE_RGB);
                pal_show();                    // the rotation starts with this palette
            } else
                wait_frames(1);
            if (scene_check())
                break;
            continue;
        }
        traces_set(front, rgb_lerp(TRACE_DIM, TRACE_BASE_RGB, rise, steps / 2u));
        pal_show();
        if (scene_check())
            break;
    }
    blns_lut_rotate(&blns, 0, 0, 0);
    pal_theme(0);
    pal_show();
}

//==========================================================
// 3: THEMES
//==========================================================
// Every palette change is built in the LUT bank the refresh is not showing,
// then the banks swap at a frame start: fade to black, dark theme fading in,
// then a crossfade back to the light theme, all without touching the VRAM.

static bool blend(const uint32_t *a, const uint32_t *b, unsigned frames)
{
    for (unsigned f = 1; f <= frames; f++) {
        pal_blend(a, b, f, frames);
        pal_show();
        if (scene_check())
            return true;
    }
    return false;
}

static bool hold(unsigned frames)
{
    for (unsigned f = 0; f < frames; f++)
        if (scene_frame())
            return true;
    return false;
}

static uint32_t black_pal[256];

void scene_themes(void)
{
    if (blend(theme_pal[0], black_pal, FRAMES(45u)) ||
        blend(black_pal, theme_pal[1], FRAMES(45u)) ||
        hold(FRAMES(120u))                          ||
        blend(theme_pal[1], theme_pal[0], FRAMES(90u)))
        { }
    pal_theme(0);
    pal_show();
}

//==========================================================
// 4: SPIN
//==========================================================
// DISP_CFG swaps change the order the refresh scans the frame: mirror,
// upside-down, mirror again, upright (set_orientation keeps the panel's
// column scan, SCR_SWAP).

void scene_spin(void)
{
    static const uint32_t orient[] = {
        BLNS_SWAP_X,
        BLNS_SWAP_X | BLNS_SWAP_Y,
        BLNS_SWAP_Y,
        BLNS_SWAP_NONE,
    };

    for (unsigned r = 0; r < 2u; r++)
        for (unsigned i = 0; i < 4u; i++) {
            set_orientation(orient[i]);
            if (hold(FRAMES(50u)))
                goto done;
        }
done:
    set_orientation(BLNS_SWAP_NONE);
}

//==========================================================
// 5: SPRITES
//==========================================================
// The small logo is drawn once into F1 (off screen) on a transparent (0)
// background. Each frame, right after the refresh has sent the frame, the GPU
// erases the logos (gradient fills), then copies them at their new places
// with colour 0 transparent. One more logo is drawn with the XOR raster
// operation: drawing it twice restores the background, so it needs no erase.
// Its colours have bit 7 set, so over the gradient (64..127) the XOR lands in
// 128..255, the bright cyan ramp: a glowing hologram.
//
// With the palette remap (caps.gpu_remap) the hologram is the same sheet
// drawn through a table T[i] = i | 0x80 (T[0] = 0: the background leaves the
// screen unchanged), and each logo casts a translucent shadow: the sheet
// copied SDW pixels down and right with REMAP DST, so where the logo is
// opaque the pixel under it becomes T[D], the gradient SDW_SHADE bands darker.
// Without it the hologram has its own sheet copy (bit 7 set in every colour)
// and the logos no shadow.

#define SPR_NR           4u
#define SHEET            F1                    // sprite sheet, pitch SPRITE_W
#define SHEET_XOR        (F1 + SPRITE_H * SPRITE_W)
#define SDW              6u                    // shadow offset, pixels
#define SDW_SHADE        20u                   // the shadow: that many gradient bands darker
#define REMAP_HOLO       ((F1 + 2u * SPRITE_H * SPRITE_W + 255u) & ~255u)  // remap tables (bytes)
#define REMAP_SHADOW     (REMAP_HOLO + 256u)

typedef struct { int x, y, dx, dy; } spr_t;

static bool remap;                             // the remap tables are there

static void bounce(spr_t *s)
{
    s->x += s->dx;
    s->y += s->dy;
    if (s->x < 0)                              { s->x = 0;                            s->dx = -s->dx; }
    if (s->x > (int) (SCR_W - SPRITE_W))       { s->x = (int) (SCR_W - SPRITE_W);     s->dx = -s->dx; }
    if (s->y < 0)                              { s->y = 0;                            s->dy = -s->dy; }
    if (s->y > (int) (SCR_H - SPRITE_H))       { s->y = (int) (SCR_H - SPRITE_H);     s->dy = -s->dy; }
}

static void remap_tables(void)
{
    volatile uint8_t *v = blns_vram8(&blns);

    for (unsigned i = 0; i < 256u; i++) {
        unsigned d = (i >= PAL_GRAD && i < PAL_GRAD + PAL_GRAD_NR) ? i + SDW_SHADE : i;
        v[REMAP_HOLO + i]   = (uint8_t) (i ? (i | PAL_XOR) : 0u);
        v[REMAP_SHADOW + i] = (uint8_t) ((d < PAL_GRAD + PAL_GRAD_NR) ? d : PAL_GRAD + PAL_GRAD_NR - 1u);
    }
}

static void copy_sprite(const spr_t *s, blns_rop_t rop, bool transp)
{
    const blns_surf_t src = { (transp || remap) ? SHEET : SHEET_XOR, SPRITE_W, BLNS_SWAP_NONE };
    const blns_surf_t dst = { F0 + (uint32_t) s->y * SCR_W + (uint32_t) s->x, 0, BLNS_SWAP_NONE };

    if (transp) {
        blns_gpu_copy_transp(&blns, &src, &dst, SPRITE_W, SPRITE_H, rop, PAL_BLACK);
    } else if (remap) {
        const blns_blit_t holo = { .rop = rop, .remap = BLNS_REMAP_SRC };
        blns_gpu_remap_table(&blns, REMAP_HOLO / 4u);
        blns_gpu_blit(&blns, &src, &dst, SPRITE_W, SPRITE_H, &holo);
    } else {
        blns_gpu_copy(&blns, &src, &dst, SPRITE_W, SPRITE_H, rop);
    }
}

// The shadow's part inside the screen (the copy does not clip)
static unsigned sdw_w(const spr_t *s) { return ((unsigned) s->x + SDW + SPRITE_W > SCR_W) ? SCR_W - (unsigned) s->x - SDW : SPRITE_W; }
static unsigned sdw_h(const spr_t *s) { return ((unsigned) s->y + SDW + SPRITE_H > SCR_H) ? SCR_H - (unsigned) s->y - SDW : SPRITE_H; }

static void shadow_sprite(const spr_t *s)
{
    const blns_surf_t src = { SHEET, SPRITE_W, BLNS_SWAP_NONE };
    const blns_surf_t dst = { F0 + ((uint32_t) s->y + SDW) * SCR_W + (uint32_t) s->x + SDW, 0, BLNS_SWAP_NONE };
    const blns_blit_t sdw = { .rop = BLNS_ROP_S, .transp = true, .transparent = PAL_BLACK,
                              .remap = BLNS_REMAP_DST };
    blns_gpu_blit(&blns, &src, &dst, (uint16_t) sdw_w(s), (uint16_t) sdw_h(s), &sdw);
}

void scene_sprites(void)
{
    spr_t spr[SPR_NR] = {
        {  20,  20,  3,  2 }, { 200,  40, -2,  3 }, {  60, 150,  2, -3 }, { 230, 170, -3, -2 },
    };
    spr_t xs = { 128, 96, 4, -3 };
    const blns_surf_t sheet = { SHEET, SPRITE_W, BLNS_SWAP_NONE };
    bool skip = false;

    // Sheet: transparent background, the small logo on top; below it the
    // XOR copy (background 0 leaves the screen unchanged), or the remap tables
    blns_gpu_wait(&blns);
    remap = blns.caps.gpu_remap;
    if (remap)
        remap_tables();
    blns_gpu_fill(&blns, &sheet, SPRITE_W, 2u * SPRITE_H, PAL_BLACK, BLNS_ROP_S);
    draw_sprite(SHEET, SPRITE_W, 0);
    if (!remap)
        draw_sprite(SHEET_XOR, SPRITE_W, PAL_XOR);
    draw_gradient(F0);
    copy_sprite(&xs, BLNS_ROP_XOR, false);

    for (unsigned f = 0; f < FRAMES(480u) && !skip; f++) {
        skip = scene_frame();                   // the refresh has just sent a frame
        copy_sprite(&xs, BLNS_ROP_XOR, false);  // XOR again: the background is back
        for (unsigned i = 0; i < SPR_NR; i++)
            draw_gradient_rect(F0, (unsigned) spr[i].x, (unsigned) spr[i].y,
                               remap ? sdw_w(&spr[i]) + SDW : SPRITE_W,
                               remap ? sdw_h(&spr[i]) + SDW : SPRITE_H);
        for (unsigned i = 0; i < SPR_NR; i++)
            bounce(&spr[i]);
        if (remap) {                            // the shadows first: on the gradient only
            blns_gpu_remap_table(&blns, REMAP_SHADOW / 4u);
            for (unsigned i = 0; i < SPR_NR; i++)
                shadow_sprite(&spr[i]);
        }
        for (unsigned i = 0; i < SPR_NR; i++)
            copy_sprite(&spr[i], BLNS_ROP_S, true);
        bounce(&xs);
        copy_sprite(&xs, BLNS_ROP_XOR, false);
    }
    blns_gpu_wait(&blns);
}

//==========================================================
// 6: CREDITS
//==========================================================
// Two credits pages, aRVern core then Belenos, each scrolled in from below.
// A page is drawn in F1, below the visible frame, and moving the refresh frame
// pointer down one line per step scrolls it in with no copy. Once it is shown,
// the GPU copies F1 into F0 and the pointer jumps back to F0 (the same picture),
// so F1 is free for the next page. The last scroll brings the title screen back.

static const txt_id_t page_core[]    = { TXT_A0, TXT_A1, TXT_A2, TXT_A3, TXT_A4, TXT_GIT };
static const txt_id_t page_belenos[] = { TXT_B0, TXT_B1, TXT_B2, TXT_B3, TXT_B4, TXT_GIT };

static void draw_page(uint32_t frame, const txt_id_t *line, unsigned nr)
{
    unsigned y = 14;

    draw_gradient(frame);
    draw_text(TXT_WORDMARK, frame + y * SCR_W + WORD_X, PAL_WHITE, WORDMARK_W);
    y += WORDMARK_H + 14u;
    for (unsigned i = 0; i < nr; i++) {
        unsigned w = text_w(line[i]);
        draw_text(line[i], frame + y * SCR_W + (SCR_W - w) / 2u, (i == 0u) ? PAL_ORANGE : PAL_WHITE, w);
        y += text_h(line[i]) + ((i == 0u) ? 12u : 9u);
    }
    draw_sprite(frame + (SCR_H - SPRITE_H - 6u) * SCR_W + (SCR_W - SPRITE_W) / 2u, SCR_W, 0);
    blns_gpu_wait(&blns);
}

static bool scroll(unsigned from, unsigned to, unsigned frames)
{
    for (unsigned f = 1; f <= frames; f++) {
        // ease in and out: smoothstep of f / frames
        unsigned t  = f * 256u / frames;
        unsigned e  = t * t * (3u * 256u - 2u * t) / (256u * 256u);
        unsigned ln = (from * (256u - e) + to * e) / 256u;
        blns_set_frame(&blns, 0, ln * SCR_W);
        if (scene_frame())
            return true;
    }
    return false;
}

// Scroll F1 in, then show the same picture from F0 so F1 can take the next one
static bool scroll_in_f1(void)
{
    const blns_surf_t src = { F1, 0, BLNS_SWAP_NONE };
    const blns_surf_t dst = { F0, 0, BLNS_SWAP_NONE };

    if (scroll(0, SCR_H, FRAMES(90u)))
        return true;
    blns_gpu_copy(&blns, &src, &dst, SCR_W, SCR_H, BLNS_ROP_S);
    blns_gpu_wait(&blns);
    blns_set_frame(&blns, 0, F0);
    return false;
}

void scene_credits(void)
{
    draw_page(F1, page_core, sizeof(page_core) / sizeof(page_core[0]));
    if (scroll_in_f1() || hold(FRAMES(240u)))
        goto skip;
    draw_page(F1, page_belenos, sizeof(page_belenos) / sizeof(page_belenos[0]));
    if (scroll_in_f1() || hold(FRAMES(240u)))
        goto skip;
    draw_title(F1);
    if (scroll_in_f1())
        goto skip;
    return;
skip:
    // Back to the title screen at once
    blns_set_frame(&blns, 0, F0);
    draw_title(F0);
}

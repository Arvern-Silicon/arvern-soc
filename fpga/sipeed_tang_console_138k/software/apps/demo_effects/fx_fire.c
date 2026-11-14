//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_fire.c   (Tang Console 138K Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 4: VOLCANO
//
// The A of the aRVern logo is a volcano: the logo stands on the bottom edge
// of the screen and its crater erupts. The fire is the classic heat map
// (240 x 70 cells, each shown as 2 x 2 pixels, down to the crater): every
// cell becomes the average of the three cells below it and the one two rows
// below, taken one cell left, right or straight below at random, minus a
// random cooling, so the heat rises in flickering tongues and the plume
// widens as it climbs. The only source is the crater mouth, fed with random
// hot cells. The eruption goes in phases: smouldering, rumbling, a full
// eruption with lava bombs thrown out on ballistic arcs (each one pours heat
// into the map where it flies, leaving a fiery trail), a wilder climax (a
// wider crater, more and higher bombs, the ground shaking: the refresh frame
// pointer jolts down a few lines), then it dies out. Before the eruption the
// ground burns: the fire of Demo_fire (2001) along the bottom of the screen,
// around and behind the volcano, every line recomputed from the one under it
// as (itself + 2 x (left + centre + right)) / 7 minus a decay, the line under
// it reseeded every frame with blocks of random width and heat; its seed fades
// as the volcano rumbles and the fields burn out before the eruption.
//
// Display: each heat row is written once at double width into the even lines
// of F0, then one GPU copy with a pitch of two lines doubles every line. The
// logo (painted once off screen in F1, colours moved to 129..171) is copied over
// it with colour 0 transparent, its colours lit by the eruption. The next
// state is computed in RAM while the refresh sends a frame; the VRAM is
// written only in the gap after it, so nothing tears. The burning ground is
// computed four pixels a word (fx_fire_asm.S) on its state in F1 after the
// logo, with a display copy beside it (heat / 4 in palette entries 64..127,
// the 2001 colours) that one GPU copy puts into F0 under the logo.
//----------------------------------------------------------------------------
#include "fx.h"

#define LOGO_X           ((SCR_W - LOGO_W) / 2u)
#define LOGO_Y           (SCR_H - LOGO_H)
#define SHAKE_MAX        5u                    // lines the picture jolts at the climax
#define LOGO_SHEET       (F1 + 8u * SCR_W)     // the logo, pitch LOGO_W (F1 rows 0..7
                                               // stay black: what a shake shows below)
#define PAL_LOGO         0x80u                 // logo colour c at PAL_LOGO + c

// The crater mouth in the logo: columns 90..108 at row 5
#define CRATER_X         ((LOGO_X + 99u) / 2u) // heat cell of its centre
#define CRATER_HALF      5                     // heat cells each side
#define CRATER_Y         ((LOGO_Y + 5u) / 2u)  // feed row (heat cells)

#define FW               (SCR_W / 2u)          // the screen width, two pixels a cell
#define FH               (CRATER_Y + 2u)       // down to the two feed rows
#define HEAT_MAX         63u
#define BOMB_NR          32u

// The burning ground: BAND_H lines along the bottom, its state and display
// copy in F1 after the logo sheet
#define BAND_H           64u
#define BAND_Y           (SCR_H - BAND_H)
#define BW               (SCR_W / 4u)          // words a line
#define BAND_STATE       (LOGO_SHEET + LOGO_W * LOGO_H)
#define BAND_DISP        (BAND_STATE + BAND_H * SCR_W)
#define PAL_GROUND       0x40u                 // heat h shown as PAL_GROUND + h / 4
#define GROUND_DECAY     2u                    // per line: the flames fit the band
_Static_assert(BAND_DISP + BAND_H * SCR_W <= 2u * FRAME_PX, "the burning ground exceeds frame 1");

// The 2001 palette, every fourth entry
static const uint32_t ground_pal[64] = {
    0x000000u, 0x0B0000u, 0x210000u, 0x3C0001u, 0x560303u, 0x6E0405u,
    0x860006u, 0x9D0007u, 0xB20108u, 0xC10A08u, 0xC81608u, 0xCD2507u,
    0xD13507u, 0xD54806u, 0xD75C05u, 0xDA6E05u, 0xDE8104u, 0xE18A03u,
    0xE39102u, 0xE59A01u, 0xE7A200u, 0xE9AB00u, 0xEBB400u, 0xEEBD00u,
    0xF0C500u, 0xF2CE00u, 0xF4D500u, 0xF6DD00u, 0xF8E500u, 0xFAEC00u,
    0xFBF200u, 0xFDF700u, 0xFEFC00u, 0xFFFF02u, 0xFFFF0Bu, 0xFFFF17u,
    0xFFFF25u, 0xFFFF35u, 0xFFFF45u, 0xFFFF56u, 0xFFFF67u, 0xFFFF77u,
    0xFFFF82u, 0xFFFF88u, 0xFFFF8Eu, 0xFFFF94u, 0xFFFF9Bu, 0xFFFFA1u,
    0xFFFFA8u, 0xFFFFAEu, 0xFFFFB5u, 0xFFFFBBu, 0xFFFFC2u, 0xFFFFC8u,
    0xFFFFCEu, 0xFFFFD4u, 0xFFFFDAu, 0xFFFFE0u, 0xFFFFE6u, 0xFFFFEBu,
    0xFFFFEFu, 0xFFFFF3u, 0xFFFFF8u, 0xFFFFFBu,
};

// (itself + 2 x (left + centre + right)) / 7 minus the decay
static uint8_t ground_tab[255u + 2u * 3u * 255u + 1u];

void fx_fire_line(uint32_t *d, const uint32_t *b, uint32_t *end, unsigned left, const uint8_t *tab);

typedef struct {
    int32_t x, y;                              // heat-cell position, 8.8
    int16_t vx, vy;                            // speed, 8.8
    uint8_t live;
} bomb_t;

typedef struct {
    uint8_t heat[FH][FW];
    uint8_t lo[FH], hi[FH];                    // non-zero cells of each row (lo > hi: none)
    uint32_t gline[2][BW + 1u];                // the ground: a line and the one under it
    bomb_t  bomb[BOMB_NR];
} fire_mem_t;
FX_MEM_CHECK(fire_mem_t);
static fire_mem_t *mem;
#define heat             (mem->heat)
#define bomb             (mem->bomb)
#define lo               (mem->lo)
#define hi               (mem->hi)

// Row y holds heat in [x0, x1] (or wider)
static void mark(unsigned y, unsigned x0, unsigned x1)
{
    if (x0 < lo[y]) lo[y] = (uint8_t) x0;
    if (x1 > hi[y]) hi[y] = (uint8_t) x1;
}

static void fire_palette(void)
{
    // heat at which each key colour is reached
    static const uint8_t  at[6]  = { 0, 10, 20, 30, 42, 63 };
    static const uint32_t key[6] = { 0x000000u, 0x701000u, 0xd84008u, 0xf8a020u, 0xffe070u, 0xffffffu };
    for (unsigned i = 0, k = 0; i <= HEAT_MAX; i++) {
        while (i > at[k + 1u])
            k++;
        pal[i] = rgb_lerp(key[k], key[k + 1u], i - at[k], at[k + 1u] - at[k]);
    }
    for (unsigned i = HEAT_MAX + 1u; i < 256u; i++)
        pal[i] = 0;
    for (unsigned i = 0; i < 64u; i++)
        pal[PAL_GROUND + i] = ground_pal[i];
}

// One step of the burning ground: the line under the band reseeded as the
// 2001 original (blocks of a random width below 20, each of a random heat,
// scaled by 'env' / 256), then every line from the bottom up. Returns
// whether any heat is left.
static bool ground_step(unsigned env)
{
    volatile uint32_t *st = blns_vram32(&blns) + BAND_STATE / 4u;
    volatile uint32_t *dp = blns_vram32(&blns) + BAND_DISP / 4u;
    uint32_t *below = mem->gline[0], *cur = mem->gline[1];
    uint8_t  *seed  = (uint8_t *) below;
    unsigned  size  = fx_rand() % 20u, n = 0;
    uint32_t  any   = 0;

    while (n < SCR_W) {
        unsigned len = size ? size : SCR_W;
        if (len > SCR_W - n)
            len = SCR_W - n;
        uint8_t v = (uint8_t) (((fx_rand() & 255u) * env) >> 8);
        for (unsigned k = 0; k < len; k++)
            seed[n + k] = v;
        n += len;
    }
    below[BW] = 0;
    for (int i = (int) BAND_H - 1; i >= 0; i--) {
        volatile uint32_t *srow = st + (unsigned) i * BW;
        volatile uint32_t *drow = dp + (unsigned) i * BW;
        for (unsigned k = 0; k < BW; k++)
            cur[k] = srow[k];
        cur[BW] = 0;
        fx_fire_line(cur, below, cur + BW, 0u, ground_tab);
        for (unsigned k = 0; k < BW; k++) {
            uint32_t w = cur[k];
            srow[k] = w;
            drow[k] = ((w >> 2) & 0x3F3F3F3Fu) | 0x40404040u;
            any |= w;
        }
        uint32_t *t = below;
        below = cur;
        cur = t;
    }
    return any != 0u;
}

// One step. The crater is fed over 'width' cells each side of its centre,
// a cell hot with probability 'hot' out of 256. A cell takes heat from at
// most two cells away in the two rows below, so each row is computed only
// over the non-zero cells of those rows widened by two: the rest stays 0.
static void fire_step(int width, unsigned hot)
{
    static const int8_t offset[4] = { -1, 0, 0, 1 };

    for (unsigned y = FH - 2u; y < FH; y++) {
        for (int x = -CRATER_HALF - 3; x <= CRATER_HALF + 3; x++)
            heat[y][(int) CRATER_X + x] =
                (uint8_t) ((x >= -width && x <= width && (fx_rand() & 255u) < hot) ? HEAT_MAX : 0u);
        mark(y, CRATER_X - CRATER_HALF - 3u, CRATER_X + CRATER_HALF + 3u);
    }

    for (unsigned y = 0; y < FH - 2u; y++) {
        const uint8_t *b1 = heat[y + 1u], *b2 = heat[y + 2u];
        uint8_t       *d  = heat[y];
        int a = (lo[y + 1u] < lo[y + 2u]) ? lo[y + 1u] : lo[y + 2u];
        int b = (hi[y + 1u] > hi[y + 2u]) ? hi[y + 1u] : hi[y + 2u];
        a = (a - 2 < 2) ? 2 : a - 2;
        b = (b + 2 > (int) FW - 3) ? (int) FW - 3 : b + 2;
        // what the row held outside the new range turns to 0
        for (int x = lo[y]; x <= hi[y]; x++)
            if (x < a || x > b)
                d[x] = 0;
        lo[y] = (uint8_t) FW;
        hi[y] = 0;
        uint32_t       r  = fx_rand();
        for (unsigned x = (unsigned) a; (int) x <= b; x++) {
            if ((x & 7u) == 0u)
                r = fx_rand();
            // 4 random bits: sample offset (-1, 0, 0, +1) and cooling
            unsigned s    = (unsigned) ((int) x + offset[r & 3u]);
            unsigned cool = (((r >> 2) & 3u) == 0u) ? 2u : ((r >> 3) & 1u);
            unsigned v    = ((unsigned) b1[s - 1u] + b1[s] + b1[s + 1u] + b2[s]) >> 2;
            d[x] = (uint8_t) ((v > cool) ? v - cool : 0u);
            if (d[x]) {
                if (x < lo[y]) lo[y] = (uint8_t) x;
                hi[y] = (uint8_t) x;
            }
            r >>= 4;
        }
    }
}

// Lava bombs: thrown from the crater, falling back under gravity; each one
// pours heat into the cells it crosses. A free bomb is thrown with
// probability 1 / 'every' per frame, at a speed scaled by 'force' / 4.
static void bombs_step(unsigned every, int force)
{
    for (unsigned i = 0; i < BOMB_NR; i++) {
        bomb_t *b = &bomb[i];
        if (!b->live) {
            if (every && (fx_rand() % every) == 0u) {
                b->x    = (int32_t) CRATER_X << 8;
                b->y    = (int32_t) CRATER_Y << 8;
                b->vx   = (int16_t) (((int) (fx_rand() % 512u) - 256) * force / 4);    // -1..+1 cell a frame
                b->vy   = (int16_t) ((-384 - (int) (fx_rand() % 384u)) * force / 4);  // 1.5..3 cells up
                b->live = 1;
            }
            continue;
        }
        b->x += b->vx;
        b->y += b->vy;
        b->vy = (int16_t) (b->vy + 12);                                     // gravity
        int cx = (int) (b->x >> 8), cy = (int) (b->y >> 8);
        if (cx < 2 || cx >= (int) FW - 3 || cy >= (int) FH - 2) {
            b->live = 0;
            continue;
        }
        if (cy >= 0) {
            heat[cy][cx] = heat[cy][cx + 1] = (uint8_t) HEAT_MAX;
            mark((unsigned) cy, (unsigned) cx, (unsigned) cx + 1u);
            if (cy + 1 < (int) FH - 2) {
                heat[cy + 1][cx] = heat[cy + 1][cx + 1] = (uint8_t) HEAT_MAX;
                mark((unsigned) cy + 1u, (unsigned) cx, (unsigned) cx + 1u);
            }
        }
    }
}

static void fire_show(bool ground)
{
    volatile uint32_t *vram = blns_vram32(&blns);
    const blns_surf_t src  = { F0,         2u * SCR_W, BLNS_SWAP_NONE };
    const blns_surf_t dst  = { F0 + SCR_W, 2u * SCR_W, BLNS_SWAP_NONE };
    const blns_surf_t lsrc = { LOGO_SHEET, LOGO_W,     BLNS_SWAP_NONE };
    const blns_surf_t ldst = { F0 + LOGO_Y * SCR_W + LOGO_X, 0, BLNS_SWAP_NONE };

    for (unsigned y = 0; y < FH - 2u; y++) {
        volatile uint32_t *row = vram + (2u * y * SCR_W) / 4u;
        const uint8_t     *h   = heat[y];
        for (unsigned x = 0; x < FW; x += 2u) {
            uint32_t a = h[x], b = h[x + 1u];
            row[x / 2u] = a | (a << 8) | (b << 16) | (b << 24);
        }
    }
    blns_gpu_copy(&blns, &src, &dst, SCR_W, FH - 2u, BLNS_ROP_S);
    if (ground) {
        const blns_surf_t gsrc = { BAND_DISP, SCR_W, BLNS_SWAP_NONE };
        const blns_surf_t gdst = { F0 + BAND_Y * SCR_W, 0, BLNS_SWAP_NONE };
        blns_gpu_copy(&blns, &gsrc, &gdst, SCR_W, BAND_H, BLNS_ROP_S);
    }
    blns_gpu_copy_transp(&blns, &lsrc, &ldst, LOGO_W, LOGO_H, BLNS_ROP_S, 0);
}

void fx_fire(void)
{
    mem = fx_mem();
    const unsigned frames = FRAMES(1500u);
    unsigned glow = 150;

    for (unsigned y = 0; y < FH; y++) {
        for (unsigned x = 0; x < FW; x++)
            heat[y][x] = 0;
        lo[y] = (uint8_t) FW;
        hi[y] = 0;
    }
    for (unsigned i = 0; i < BOMB_NR; i++)
        bomb[i].live = 0;
    for (unsigned s = 0; s < sizeof(ground_tab); s++)
        ground_tab[s] = (uint8_t) ((s / 7u > GROUND_DECAY) ? s / 7u - GROUND_DECAY : 0u);
    fx_clear();
    fx_logo_draw(LOGO_SHEET, LOGO_W, PAL_LOGO);    // F1, behind the black palette
    fill(BAND_STATE, SCR_W, BAND_H, 0);
    blns_gpu_wait(&blns);
    // the ground burns until the rumble, then its seed fades by mid-rumble
    const unsigned g_fade = frames * 5u / 20u, g_out = frames * 13u / 40u;
    bool ground = true;
    fire_palette();
    fx_logo_light(PAL_LOGO, glow);
    pal_show();

    for (unsigned f = 0; f < frames; f++) {
        // phases out of 20: smoulder 0-3, rumble 4-6, erupt 7-12, climax 13-16,
        // dying 17-19
        unsigned ph = f * 20u / frames;
        int      width, force = 4;
        unsigned hot, target, every = 0, shake = 0;
        if (ph < 4u)        { width = 1;                    hot = 110u; target = 150u; }
        else if (ph < 7u)   { width = 1 + (int) (ph - 3u);  hot = 170u; target = 175u; }
        else if (ph < 13u)  { width = CRATER_HALF + 2;      hot = 245u; target = 215u + (fx_rand() & 40u);
                              every = 16u; }
        else if (ph < 17u)  { width = CRATER_HALF + 3;      hot = 256u; target = 225u + (fx_rand() & 31u);
                              every = 3u; force = 6; shake = fx_rand() % (SHAKE_MAX + 1u); }
        else                { width = (int) (19u - ph);     hot = 90u - 30u * (ph - 17u); target = 150u; }

        fire_step(width, hot);                 // RAM only: while the refresh runs
        bombs_step(every, force);
        bool shown = ground;                   // a last copy once it has burnt out
        if (ground) {
            unsigned env = (f < g_fade) ? 256u : (f < g_out) ? 256u * (g_out - f) / (g_out - g_fade) : 0u;
            ground = ground_step(env) || env;
#ifdef SIMULATION
            for (unsigned k = 0; k < 3u && ground; k++)
                ground = ground_step(env) || env;
#endif
        }
#ifdef SIMULATION
        for (unsigned k = 0; k < 3u; k++) {
            fire_step(width, hot);
            bombs_step(every, force);
        }
#endif
        glow = (glow * 3u + target) / 4u;
        fx_logo_light(PAL_LOGO, glow);
        blns_set_frame(&blns, 0, F0 + shake * SCR_W);
        pal_show();                            // waits for the end of a frame
        fire_show(shown);                      // VRAM: in the gap after it
        blns_gpu_wait(&blns);
        if (scene_check())
            break;
    }
    blns_set_frame(&blns, 0, F0);
}

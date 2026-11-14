//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      ray.c       (DE0-Nano-SoC Belenos raycaster demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The raycaster (the classic "Wolfenstein" method), in fixed point.
//
// For each of the 320 screen columns a ray leaves the player through the
// camera plane and walks the 16 x 16 map cell by cell (DDA) until it enters a
// wall. Its perpendicular distance gives the height of the wall slice, where
// it hit along the wall gives the texture column. The column is one GPU
// textured span walked down the screen (the column swap), the texture step
// set per column; ceiling and floor are gradient bands filled by the GPU
// first.
//
// Wall types: the aRVern poster comes from tex_data.h; brick, a circuit
// board, stone blocks and metal panels are generated into RAM at start-up.
// The circuit traces use four "glow" colours along a diagonal pattern, and
// the palette animation rotates their brightness, so light runs along them;
// the posters' logo traces pulse.
//
// Palette: 6 shades (by distance; north-south walls one shade darker) of the
// 32 texture colours at 32 * shade + c, the ceiling at 192..223 and the
// floor at 224..255.
//
// Minimap: with the palette remap (caps.gpu_remap) the view shows through
// it, darker: one FILL with REMAP DST over its area turns every pixel D into
// T[D], a table after the textures giving the same colour MM_DARK shades
// darker (a ceiling or floor band towards its dark end); the walls and the
// player are drawn over it. Without the remap it sits on a dark plate. Its
// frame is a GPU rectangle outline; the player's view is shown by GPU lines:
// the field of view's two edges dashed, the heading solid up to the wall.
//----------------------------------------------------------------------------
#include "ray.h"
#define TEX_DATA_TABLES
#include "tex_data.h"

// Render size: the refresh doubles every pixel (DISP_CFG.PIX_DOUBLE), so the
// scene is drawn at 160 x 120 and shown at 320 x 240
#define RW               (SCR_W / 2u)
#define RH               (SCR_H / 2u)
#define MM_CELL          2
#define MM_DARK          3u                    // the minimap's shade: 3 shades darker

#define MAP_W            16
#define MAP_H            16
#define SHADES           6u
#define PAL_CEIL         192u
#define PAL_FLOOR        224u
#define FOV_PLANE        43000                 // camera plane half-width, Q16 (about 66 degrees)

// Texture colours: those of tex_data.h (named there), then the generated
// walls' own
#define C_MORTAR         TEX_C_MORTAR
#define C_STEEL          TEX_C_STEEL
#define C_DARK           TEX_C_PLATE
#define C_LIGHT          TEX_C_LIGHT
#define C_TAN            TEX_C_TAN
#define C_TRACE          TEX_C_TRACE           // the logo's circuit traces
#define C_GOLD           TEX_C_GOLD
#define C_WHITE          TEX_C_WHITE
#define C_GREEN0         26u
#define C_GREEN1         27u
#define C_GLOW           28u                   // 28..31: the animated trace colours
#define RAM_TEX_NR       4u                    // brick, circuit, stone, metal
#define WALL_NR          5u                    // map values 1..5
#define T                64u                   // generated texture size

static uint8_t        ram_tex[RAM_TEX_NR][T * T];     // column by column
static int32_t        zbuf[RW];                    // wall distance per column, Q14
static const uint8_t *wall_texels[WALL_NR];
static uint8_t        wall_log2[WALL_NR];
static uint32_t       wall_vram[WALL_NR];             // texture pixel address in the VRAM
static uint32_t       poster_vram;
static uint32_t       mm_table;                       // the minimap's remap table (byte address), 0: none

// The textures sit in the VRAM after frame 0's RW x RH pixels (frame 1 starts at F1)
#define TEX_VRAM         4096u * ((RW * RH + 4095u) / 4096u)
static uint32_t       rnd = 0x9E3779B9u;

static uint32_t rand32(void)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

int16_t sin_q14[ANG_NR];

// 0 empty, 1 brick, 2 brick with an aRVern logo, 3 circuit board,
// 4 stone, 5 metal
static const char map[MAP_H][MAP_W + 1] = {
    "1111121113331111",
    "1000000010000002",
    "1011110010444401",
    "1010000000400001",
    "1210333110404121",
    "1000300000004001",
    "1110302111404011",
    "1000000100000001",
    "1055550101111101",
    "1020000100000101",
    "1050111101110101",
    "1000100000010001",
    "1110101112010111",
    "1000005000000001",
    "1000205000110021",
    "1111111211555511",
};

static inline int cell(int mx, int my)
{
    if (mx < 0 || my < 0 || mx >= MAP_W || my >= MAP_H)
        return 1;
    return map[my][mx] - '0';
}

int ray_wall(int32_t x, int32_t y)
{
    return cell(x >> 16, y >> 16) != 0;
}

//----------------------------------------------------------------------------
// Generated walls (start-up code, built for size: it runs once)
//----------------------------------------------------------------------------
#define COLD             __attribute__((optimize("Os"), noinline))
COLD static void put(uint8_t *t, unsigned x, unsigned y, uint8_t c)
{
    t[(x & (T - 1u)) * T + (y & (T - 1u))] = c;
}

COLD static void rect(uint8_t *t, unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c)
{
    for (unsigned j = 0; j < h; j++)
        for (unsigned i = 0; i < w; i++)
            put(t, x + i, y + j, c);
}

// Brick: 16 x 8 bricks in six colours, every other row offset, mortar lines
COLD static void gen_brick(uint8_t *t)
{
    static const uint8_t brick[6] = { TEX_C_BRICK0, TEX_C_BRICK1, TEX_C_BRICK2,
                                      TEX_C_BRICK3, TEX_C_BRICK4, TEX_C_BRICK5 };
    rect(t, 0, 0, T, T, C_MORTAR);
    for (unsigned row = 0; row < T / 8u; row++)
        for (unsigned b = 0; b <= T / 16u; b++)
            rect(t, b * 16u + ((row & 1u) ? 8u : 0u) + 1u, row * 8u + 1u, 15, 7, brick[rand32() % 6u]);
}

// Stone: rows of blocks of varied widths, three stone colours, speckles
COLD static void gen_stone(uint8_t *t)
{
    static const uint8_t stone[3] = { C_STEEL, C_MORTAR, C_TAN };
    rect(t, 0, 0, T, T, C_DARK);
    for (unsigned row = 0; row < 4u; row++) {
        unsigned x = rand32() % 16u;
        while (x < T + 16u) {
            unsigned w = 14u + rand32() % 12u;
            uint8_t  c = stone[rand32() % 3u];
            for (unsigned j = 1; j < 16u; j++)
                for (unsigned i = 1; i < w; i++)
                    put(t, x + i, row * 16u + j, ((rand32() & 7u) == 0u) ? C_DARK : c);
            x += w;
        }
    }
}

// Metal: 2 x 2 bevelled panels with a rivet in each corner
COLD static void gen_metal(uint8_t *t)
{
    for (unsigned py = 0; py < T; py += 32u)
        for (unsigned px = 0; px < T; px += 32u) {
            rect(t, px, py, 32, 32, C_STEEL);
            for (unsigned y = 2; y < 30u; y += 3u)              // brushed lines
                rect(t, px + 2u, py + y, 28, 1, C_MORTAR);
            rect(t, px, py, 32, 1, C_LIGHT);
            rect(t, px, py, 1, 32, C_LIGHT);
            rect(t, px, py + 31u, 32, 1, C_DARK);
            rect(t, px + 31u, py, 1, 32, C_DARK);
            static const uint8_t rv[2] = { 4, 26 };
            for (unsigned a = 0; a < 2u; a++)
                for (unsigned b = 0; b < 2u; b++) {
                    rect(t, px + rv[a] + 1u, py + rv[b] + 1u, 2, 2, C_DARK);
                    rect(t, px + rv[a], py + rv[b], 2, 2, C_WHITE);
                }
        }
}

// Circuit board: green board with a dot grid, a chip, and traces in the four
// glow colours (by diagonal position) ending on gold pads
COLD static void gen_circuit(uint8_t *t)
{
    rect(t, 0, 0, T, T, C_GREEN0);
    for (unsigned y = 4; y < T; y += 8u)
        for (unsigned x = 4; x < T; x += 8u)
            put(t, x, y, C_GREEN1);
    for (unsigned n = 0; n < 9u; n++) {
        unsigned x = rand32() % T, y = rand32() % T, d = rand32() & 3u;
        rect(t, x - 1u, y - 1u, 3, 3, C_GOLD);
        for (unsigned seg = 0; seg < 4u; seg++) {
            unsigned len = 6u + rand32() % 14u;
            for (unsigned k = 0; k < len; k++) {
                x += (d == 0u) ? 1u : (d == 2u) ? T - 1u : 0u;
                y += (d == 1u) ? 1u : (d == 3u) ? T - 1u : 0u;
                put(t, x, y, (uint8_t) (C_GLOW + (((x + y) >> 2) & 3u)));
            }
            d = (d + ((rand32() & 1u) ? 1u : 3u)) & 3u;    // turn left or right
        }
        rect(t, x - 1u, y - 1u, 3, 3, C_GOLD);
    }
    rect(t, 22, 24, 20, 16, C_DARK);                       // the chip and its pins
    for (unsigned i = 24; i < 41u; i += 3u) {
        rect(t, i, 21, 1, 3, C_LIGHT);
        rect(t, i, 40, 1, 3, C_LIGHT);
    }
}

// Each texture is column-major: texel (tx, row) at tx * size + row. For
// EXEC_TEXSPAN the stepping coordinate u is the row, v the column (pitch = size)
void ray_upload(void)
{
    volatile uint8_t *vram = blns_vram8(&blns);
    uint32_t          at   = TEX_VRAM;

    for (unsigned i = 0; i < WALL_NR; i++) {
        unsigned n = 1u << (2u * wall_log2[i]);
        unsigned j = 0;
        while (j < i && wall_texels[j] != wall_texels[i])
            j++;
        if (j < i) {
            wall_vram[i] = wall_vram[j];
            continue;
        }
        wall_vram[i] = at;
        for (unsigned k = 0; k < n; k++)
            vram[at + k] = wall_texels[i][k];
        at += n;
    }
    poster_vram = wall_vram[1];                         // map value 2: the poster

    // the minimap's table, on the next 256-byte boundary: a texture colour
    // MM_DARK shades darker, a ceiling band towards the horizon, a floor band
    // away from it (12 of its 32 bands: about as dark as MM_DARK shades)
    if (blns.caps.gpu_remap) {
        at = (at + 255u) & ~255u;
        for (unsigned i = 0; i < 256u; i++) {
            unsigned s = i / 32u, d;
            if (i < PAL_CEIL)
                d = 32u * ((s + MM_DARK < SHADES) ? s + MM_DARK : SHADES - 1u) + i % 32u;
            else if (i < PAL_FLOOR)
                d = (i + 12u < PAL_FLOOR) ? i + 12u : PAL_FLOOR - 1u;
            else
                d = (i >= PAL_FLOOR + 12u) ? i - 12u : PAL_FLOOR;
            vram[at + i] = (uint8_t) d;
        }
        mm_table = at;
    }
}

void ray_animate(unsigned t)
{
    // the four glow colours: a bright pulse moving through them
    for (unsigned k = 0; k < 4u; k++) {
        unsigned d   = (t / 4u + 4u - k) & 3u;
        uint32_t rgb = (d == 0u) ? 0xb0ffffu : (d == 1u) ? 0x40c0e0u : 0x106080u;
        for (unsigned s = 0; s < SHADES; s++)
            pal[32u * s + C_GLOW + k] = rgb_lerp(rgb, 0x05060au, s, SHADES + 1u);
    }
    // the posters' logo traces pulse
    uint32_t tr = rgb_lerp(tex_pal[C_TRACE], 0xa8e8ffu, (unsigned) (128 + (rsin(t * 16u) >> 7)), 256);
    for (unsigned s = 0; s < SHADES; s++)
        pal[32u * s + C_TRACE] = rgb_lerp(tr, 0x05060au, s, SHADES + 1u);
}

COLD // Sine table: a quarter wave by the Chebyshev recurrence
// sin((n + 1) d) = 2 cos(d) sin(n d) - sin((n - 1) d), in Q30, then mirrored
COLD static void sin_init(void)
{
    const int64_t two_cos = 2LL * 1073721611;            // 2 cos(2 pi / 1024), Q30
    int64_t s0 = 0, s1 = 6588356;                      // sin(0), sin(2 pi / 1024), Q30

    for (unsigned n = 0; n <= ANG_NR / 4u; n++) {
        int16_t v = (int16_t) ((s0 + (1 << 15)) >> 16);  // Q30 to Q14, rounded
        sin_q14[n] = v;
        sin_q14[ANG_NR / 2u - n] = v;
        sin_q14[(ANG_NR / 2u + n) & ANG_MASK] = (int16_t) -v;
        sin_q14[(ANG_NR - n) & ANG_MASK] = (int16_t) -v;
        int64_t s2 = ((two_cos * s1) >> 30) - s0;
        s0 = s1;
        s1 = s2;
    }
}

void ray_init(void)
{
    sin_init();
    static const uint32_t extra[2] = { 0x0c3018u, 0x1c5a30u };     // board greens

    // map value 1 brick, 2 poster (stored), 3 circuit, 4 stone, 5 metal
    gen_brick(ram_tex[0]);
    gen_circuit(ram_tex[1]);
    gen_stone(ram_tex[2]);
    gen_metal(ram_tex[3]);
    static const uint8_t ram_of[WALL_NR] = { 0, 255, 1, 2, 3 };
    for (unsigned i = 0; i < WALL_NR; i++) {
        wall_texels[i] = (ram_of[i] == 255u) ? tex_texels[0] : ram_tex[ram_of[i]];
        wall_log2[i]   = (ram_of[i] == 255u) ? tex_log2[0]   : 6u;
    }

    // texture colours, 6 shades towards a dark fog
    for (unsigned s = 0; s < SHADES; s++) {
        for (unsigned c = 0; c < TEX_COLOURS; c++)
            pal[32u * s + c] = rgb_lerp(tex_pal[c], 0x05060au, s, SHADES + 1u);
        for (unsigned c = 0; c < 2u; c++)
            pal[32u * s + C_GREEN0 + c] = rgb_lerp(extra[c], 0x05060au, s, SHADES + 1u);
    }
    ray_animate(0);
    for (unsigned i = 0; i < 32u; i++) {
        pal[PAL_CEIL + i]  = rgb_lerp(0x404858u, 0x0c0e14u, i, 31);
        pal[PAL_FLOOR + i] = rgb_lerp(0x0e0c0au, 0x5a4c3eu, i, 31);
    }
}

// Ceiling down to the horizon, floor below it: each a palette ramp of 32
// two-line bands, one GPU gradient fill (the colour + 1 every 2 lines) of up
// to 64 lines from the horizon, its last colour filled on beyond them. The
// ceiling ends at its darkest colour on the horizon, the floor starts there.
#define BAND_LINES       2u
#define RAMP_LINES       (32u * BAND_LINES)

static void background(uint32_t frame, unsigned horizon)
{
    const unsigned cg = (horizon < RAMP_LINES) ? horizon : RAMP_LINES;      // ceiling ramp lines
    const unsigned c0 = 31u - (cg - 1u) / BAND_LINES;                        // its first colour
    const unsigned fh = RH - horizon;
    const unsigned fg = (fh < RAMP_LINES) ? fh : RAMP_LINES;                 // floor ramp lines

    if (horizon > cg)
        fill(frame, RW, horizon - cg, (uint8_t) (PAL_CEIL + c0));
    if (fh > fg)
        fill(frame + (horizon + fg) * RW, RW, fh - fg, (uint8_t) (PAL_FLOOR + 31u));
    blns_gpu_gradient(&blns, true, BAND_LINES, false, 1);
    fill(frame + (horizon - cg) * RW, RW, cg, (uint8_t) (PAL_CEIL + c0));
    fill(frame + horizon * RW, RW, fg, (uint8_t) PAL_FLOOR);
    blns_gpu_gradient(&blns, false, 0u, false, 0);
}

static inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

int32_t ray_frame(uint32_t frame, const player_t *p, int bob, int32_t *left, int32_t *right)
{
    // direction and camera plane, Q14
    const int32_t dx = rcos(p->a), dy = rsin(p->a);
    const int32_t px = -dy * FOV_PLANE >> 16, py = dx * FOV_PLANE >> 16;
    int32_t ahead = 0;

    background(frame, (unsigned) ((int) RH / 2 + bob));
    unsigned cur_kind = 0, cur_shade = ~0u;    // the walls go over the fills (FIFO order)

    for (unsigned x = 0; x < RW; x++) {
        // ray direction, Q14: dir + plane * (2x / W - 1)
        int32_t cam = ((int32_t) (2u * x) - (int32_t) RW) * 16384 / (int32_t) RW;
        int32_t rx  = dx + (px * cam >> 14);
        int32_t ry  = dy + (py * cam >> 14);
        // distance (Q14) between two vertical / horizontal grid lines along the ray
        int32_t ddx = (iabs(rx) < 4) ? (1 << 30) : (1 << 28) / iabs(rx);
        int32_t ddy = (iabs(ry) < 4) ? (1 << 30) : (1 << 28) / iabs(ry);
        int     mx  = p->x >> 16, my = p->y >> 16;
        int     sx  = (rx < 0) ? -1 : 1, sy = (ry < 0) ? -1 : 1;
        int32_t fx  = (p->x & 0xFFFF) >> 8, fy = (p->y & 0xFFFF) >> 8;     // Q8 fractions
        int32_t sdx = (int32_t) (((int64_t) (rx < 0 ? fx : 256 - fx) * ddx) >> 8);
        int32_t sdy = (int32_t) (((int64_t) (ry < 0 ? fy : 256 - fy) * ddy) >> 8);
        int     side = 0, kind = 1;

        for (int n = 0; n < 64; n++) {
            if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
            else           { sdy += ddy; my += sy; side = 1; }
            kind = cell(mx, my);
            if (kind)
                break;
        }
        int32_t dist = side ? sdy - ddy : sdx - ddx;           // perpendicular, Q14
        if (dist < 256)
            dist = 256;
        zbuf[x] = dist;
        if (x == RW / 2u)
            ahead = dist << 2;
        if (x == RW / 4u)
            *left = dist << 2;
        if (x == 3u * RW / 4u)
            *right = dist << 2;

        // texture column: where the ray met the wall, 0..63
        int32_t hit = side ? p->x + (int32_t) (((int64_t) dist * rx) >> 12)
                           : p->y + (int32_t) (((int64_t) dist * ry) >> 12);
        unsigned lg = wall_log2[kind - 1];
        unsigned tx = ((uint32_t) hit & 0xFFFFu) >> (16u - lg);
        // read the texture left to right as seen from the ray (map y grows southwards)
        if ((side == 0 && rx < 0) || (side == 1 && ry > 0))
            tx = (1u << lg) - 1u - tx;

        // slice height and shade
        int32_t  h     = (int32_t) ((RH << 14) / (uint32_t) dist);
        unsigned shade = (unsigned) (dist >> 14) + (unsigned) side;
        if (shade > SHADES - 1u)
            shade = SHADES - 1u;
        unsigned base  = 32u * shade;
        int32_t  y0    = ((int32_t) RH - h) / 2 + bob;
        int32_t  step  = (int32_t) ((1u << (16u + lg)) / (uint32_t) h);   // texels per pixel, 16.16
        int32_t  tpos  = 0;
        int32_t  ya    = y0, yb = y0 + h;
        if (ya < 0) {
            tpos = -ya * step;
            ya   = 0;
        }
        if (yb > (int32_t) RH)
            yb = (int32_t) RH;
        if (yb <= ya)
            continue;

        // one textured span down the column: u steps along the texture column tx
        if ((unsigned) kind != cur_kind) {
            const blns_tex_t tex = { .addr = wall_vram[kind - 1], .log2_w = (uint8_t) lg,
                                     .log2_h = (uint8_t) lg, .swap = BLNS_SWAP_CL };
            blns_gpu_tex_begin(&blns, &tex, frame, 0, (uint8_t) base);
            cur_kind  = (unsigned) kind;
            cur_shade = base;
        } else if (base != cur_shade) {
            blns_gpu_tex_shade(&blns, (uint8_t) base);
            cur_shade = base;
        }
        blns_gpu_tex_step(&blns, (int16_t) ((step >> 5) > 0x7FFF ? 0x7FFF : (step >> 5)), 0);
        blns_gpu_tex_span(&blns, frame + (uint32_t) ya * RW + x, (uint16_t) (yb - ya),
                          (uint16_t) (tpos >> 10), (uint16_t) (tx << 6));
    }
    return ahead;
}

//----------------------------------------------------------------------------
// Floating logos: the logo region of the poster texture, spinning and bobbing
//----------------------------------------------------------------------------
#define SPR_NR           8u
#define SPR_U0           8                     // logo region of the poster texture
#define SPR_V0           22
#define SPR_TW           112
#define SPR_TH           84
#define SPR_SIZE         38000                 // width in map units, Q16 (0.58 of a cell)

static const uint8_t spr_cell[SPR_NR][2] = {
    { 6, 1 }, { 5, 3 }, { 12, 3 }, { 3, 7 }, { 10, 7 }, { 5, 9 }, { 7, 11 }, { 10, 13 },
};

void ray_sprites(uint32_t frame, const player_t *p, int bob, unsigned t)
{
    const int32_t dx = rcos(p->a), dy = rsin(p->a);       // Q14
    int32_t  depth[SPR_NR], lat[SPR_NR];
    uint8_t  order[SPR_NR];
    unsigned n = 0;

    // camera space: depth along the view, lateral to the right (Q14 map units)
    for (unsigned i = 0; i < SPR_NR; i++) {
        int32_t ox = ((int32_t) spr_cell[i][0] * ONE + ONE / 2 - p->x) >> 2;   // Q14
        int32_t oy = ((int32_t) spr_cell[i][1] * ONE + ONE / 2 - p->y) >> 2;
        depth[i] = (ox * dx + oy * dy) >> 14;
        lat[i]   = (oy * dx - ox * dy) >> 14;
        if (depth[i] > 4096) {                                 // more than a quarter cell ahead
            unsigned j = n++;
            while (j > 0u && depth[order[j - 1u]] < depth[i]) {
                order[j] = order[j - 1u];
                j--;
            }
            order[j] = (uint8_t) i;
        }
    }
    for (unsigned k = 0; k < n; k++) {                         // far to near
        unsigned i  = order[k];
        int32_t  d  = depth[i];
        // screen centre, size; the plane half-width is FOV_PLANE (Q16)
        int      cx = (int) (RW / 2u) + (int) ((int64_t) lat[i] * (RW / 2u) * 65536 / ((int64_t) d * FOV_PLANE));
        int      w  = (int) ((int64_t) SPR_SIZE * (RW / 2u) / ((int64_t) d * FOV_PLANE >> 14));
        int      h  = w * SPR_TH / SPR_TW;
        int32_t  sc = rcos(t * 4u + i * 128u);                 // spin
        int      ws = (int) ((int64_t) w * (sc < 0 ? -sc : sc) >> 14);
        if (ws < 2 || h < 2)
            continue;
        int      cy = (int) RH / 2 + bob + (int) (rsin(t * 6u + i * 90u) * (int32_t) h >> 17);
        int      x0 = cx - ws / 2, y0 = cy - h / 2;
        unsigned shade = (unsigned) (d >> 14);
        if (shade > SHADES - 1u)
            shade = SHADES - 1u;
        unsigned base = 32u * shade;
        int32_t  vstep = (SPR_TH << 16) / h;
        // vertical clipping: the rows of the logo inside the frame
        int      r0 = (y0 < 0) ? -y0 : 0;
        int      r1 = (y0 + h > (int) RH) ? (int) RH - y0 : h;
        if (r1 <= r0)
            continue;
        // one textured column per visible screen column, the plate colour transparent
        const blns_tex_t tex = { .addr = poster_vram, .log2_w = 7, .log2_h = 7,
                                 .transp = true, .transparent = TEX_C_PLATE,
                                 .swap = BLNS_SWAP_CL };
        blns_gpu_tex_begin(&blns, &tex, frame, 0, (uint8_t) base);
        blns_gpu_tex_step(&blns, (int16_t) (vstep >> 5), 0);
        for (int c = 0; c < ws; c++) {
            int x = x0 + c;
            if (x < 0 || x >= (int) RW || zbuf[x] <= d)
                continue;
            int u = c * SPR_TW / ws;
            if (sc < 0)
                u = SPR_TW - 1 - u;                            // the back: mirrored
            blns_gpu_tex_span(&blns, frame + (uint32_t) (y0 + r0) * RW + (uint32_t) x,
                              (uint16_t) (r1 - r0),
                              (uint16_t) ((SPR_V0 << 6) + ((r0 * vstep) >> 10)),
                              (uint16_t) ((SPR_U0 + u) << 6));
        }
    }
}

//----------------------------------------------------------------------------
// Minimap: 4 x 4 displayed pixels per cell in the top-right corner
//----------------------------------------------------------------------------
#define MM_X0            ((int) RW - MAP_W * MM_CELL - 6 * MM_CELL / 4)
#define MM_Y0            (6 * MM_CELL / 4)

// One view ray on the minimap, in 1/16 pixel: from the player along the ray
// of render column 'col', 'dist' (Q14 cells, perpendicular) long
#define MM_CONE          (5 << 14)             // the field of view's edges: 5 cells deep

static void mm_ray(const player_t *p, unsigned col, int32_t dist, int32_t x0, int32_t y0,
                   const blns_line_t *opt)
{
    const int32_t dx = rcos(p->a), dy = rsin(p->a);                       // Q14
    const int32_t qx = -dy * FOV_PLANE >> 16, qy = dx * FOV_PLANE >> 16;  // the camera plane
    const int32_t cam = ((int32_t) (2u * col) - (int32_t) RW) * 16384 / (int32_t) RW;
    const int32_t rx = dx + (qx * cam >> 14), ry = dy + (qy * cam >> 14);
    // the hit: dist * r (Q14 x Q14 >> 12: Q16 map units), MM_CELL pixels a cell
    const int32_t ex = x0 + (int32_t) (((int64_t) dist * rx * MM_CELL) >> 24);
    const int32_t ey = y0 + (int32_t) (((int64_t) dist * ry * MM_CELL) >> 24);
    blns_gpu_line_xy(&blns, x0, y0, ex, ey, opt);
}

void ray_minimap(uint32_t frame, const player_t *p)
{
    static const uint8_t wall_col[WALL_NR] = { TEX_C_BRICK2, TEX_C_GOLD, C_GREEN1, TEX_C_TAN, TEX_C_STEEL };
    const uint32_t org = frame + (uint32_t) MM_Y0 * RW + (uint32_t) MM_X0;

    blns_gpu_xy_begin(&blns, frame, 0, TEX_C_LIGHT);                       // the frame's lines
    if (mm_table) {                            // a light frame, the view darker inside it
        const blns_surf_t mm = { org, 0, BLNS_SWAP_NONE };
        blns_gpu_rect_outline_xy(&blns, MM_X0 - 1, MM_Y0 - 1, MAP_W * MM_CELL + 2u, MAP_H * MM_CELL + 2u, NULL);
        blns_gpu_remap_table(&blns, mm_table / 4u);
        blns_gpu_remap_rect(&blns, &mm, MAP_W * MM_CELL, MAP_H * MM_CELL);
    } else {
        fill(org - RW - 1u, MAP_W * MM_CELL + 2u, MAP_H * MM_CELL + 2u, TEX_C_LIGHT);
        fill(org, MAP_W * MM_CELL, MAP_H * MM_CELL, TEX_C_PLATE);
    }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; ) {
            int k = cell(x, y), e = x + 1;
            while (e < MAP_W && cell(e, y) == k)
                e++;
            if (k)
                fill(org + (uint32_t) (y * MM_CELL) * RW + (uint32_t) (x * MM_CELL),
                     (unsigned) ((e - x) * MM_CELL), MM_CELL, wall_col[k - 1]);
            x = e;
        }
    for (unsigned i = 0; i < SPR_NR; i++)
        fill(org + (uint32_t) (spr_cell[i][1] * MM_CELL + 1) * RW + (uint32_t) (spr_cell[i][0] * MM_CELL + 1),
             2, 2, TEX_C_GOLD);
    // the view, GPU lines clipped to the map: the field of view's edges dashed
    // (2 pixels on, 2 off), the rays of the first and last render columns 5
    // cells deep across the walls, and the heading solid, up to the wall the
    // middle column's ray met
    const int32_t x0 = BLNS_SUB(MM_X0) + (p->x * MM_CELL >> 12), y0 = BLNS_SUB(MM_Y0) + (p->y * MM_CELL >> 12);
    const blns_line_t dash = { .phase_reset = true };
    blns_gpu_colour_set(&blns, TEX_C_WHITE);
    blns_gpu_clip(&blns, MM_X0, MM_Y0, MM_X0 + MAP_W * MM_CELL - 1, MM_Y0 + MAP_H * MM_CELL - 1);
    blns_gpu_line_pattern(&blns, 0x3u, 4u);
    mm_ray(p, 0u, MM_CONE, x0, y0, &dash);
    mm_ray(p, RW - 1u, MM_CONE, x0, y0, &dash);
    blns_gpu_line_solid(&blns);
    mm_ray(p, RW / 2u, zbuf[RW / 2u], x0, y0, NULL);
    blns_gpu_clip(&blns, BLNS_GPU_XY_MIN, BLNS_GPU_XY_MIN, BLNS_GPU_XY_MAX, BLNS_GPU_XY_MAX);
    // the player over them
    int px = (int) ((p->x * MM_CELL) >> 16), py = (int) ((p->y * MM_CELL) >> 16);
    fill(org + (uint32_t) (py - 1) * RW + (uint32_t) (px - 1), 3, 3, TEX_C_WHITE);
}

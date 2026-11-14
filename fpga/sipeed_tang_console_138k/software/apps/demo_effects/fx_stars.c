//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_stars.c  (Tang Console 138K Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 1: STARFIELD
//
// A flight through 360 stars. Each star has a 3D position; the perspective
// projection (x / z, y / z) puts it on screen, its brightness grows as 1 / z
// and it is two pixels wide, three close up. While the speed is high a star
// is drawn as a streak from where it was a few steps ago: two GPU lines, one
// row apart. Every frame is drawn in the frame the refresh is not showing (one
// clear, then a few hundred small fills and lines) and shown from the next
// refresh on, so the panel never sees a frame half drawn, whatever its scan
// direction. The speed rises to a warp, then the flight slows down and the
// aRVern logo approaches out of the distance: drawn at its perspective size
// (1 / distance) every frame, scaled by the CPU from its runs
// (fx_logo_scaled), it grows from a speck to full size and lights up as it
// nears, the stars flowing past around it.
//
// Palette: 0 black, 1..32 brightness from deep blue to white, the logo at
// 128 + c.
//----------------------------------------------------------------------------
#include "fx.h"

#define STAR_NR          360u
#define Z_FAR            1024
#define Z_NEAR           16
#define FOCAL            ((int) (SCR_W / 2u))  // projection: screen = world * FOCAL / z
#define WORLD_X          1024                  // star field: +-WORLD_X by +-WORLD_Y,
#define WORLD_Y          ((int) (WORLD_X * SCR_H / SCR_W)) // the screen's shape
#define SHADES           32
#define TRAIL            3                     // streak: the position TRAIL steps back

typedef struct {
    int16_t  x, y, z;                          // world position, z away from the viewer
    uint8_t  size;
} star_t;

#define PAL_LOGO         0x80u
#define LOGO_Z_FAR       4000                  // logo distance when it appears
#define LOGO_Z_NEAR      200                   // ... when it reaches full size

typedef struct {
    star_t         star[STAR_NR];
    fx_logo_rows_t rows;
} stars_mem_t;
FX_MEM_CHECK(stars_mem_t);
static stars_mem_t *mem;
#define star             (mem->star)

static void respawn(star_t *s, int z)
{
    s->x = (int16_t) ((int) (fx_rand() % (2u * WORLD_X)) - WORLD_X);
    s->y = (int16_t) ((int) (fx_rand() % (2u * WORLD_Y)) - WORLD_Y);
    s->z = (int16_t) z;
}

static bool on_screen(int x, int y, int size)
{
    return x >= 0 && y >= 0 && x <= (int) SCR_W - size && y <= (int) SCR_H - size;
}

// The dots, batched: consecutive dots of one shade go out as one
// blns_gpu_fills_xy (x/y mode on the back frame); a dot of another shade, a
// streak or the end of the frame sends the batch first, so the draw order and
// the pixels stay those of one fill per dot
static blns_rect_t dots[32];
static unsigned    dots_n;
static uint8_t     dots_c;

static void dots_flush(void)
{
    if (!dots_n)
        return;
    blns_gpu_colour_set(&blns, dots_c);
    blns_gpu_fills_xy(&blns, dots, dots_n);
    dots_n = 0;
}

static void dot(int x, int y, unsigned size, uint8_t c)
{
    if (dots_n && (c != dots_c || dots_n == sizeof(dots) / sizeof(dots[0])))
        dots_flush();
    dots_c = c;
    dots[dots_n++] = (blns_rect_t) { (int16_t) x, (int16_t) y, (uint16_t) size, (uint16_t) size };
}

static void draw(star_t *s, int speed)
{
    int sx    = (int) (SCR_W / 2u) + s->x * FOCAL / s->z;
    int sy    = (int) (SCR_H / 2u) + s->y * FOCAL / s->z;
    int shade = SHADES * 96 / s->z;
    uint8_t c = (uint8_t) ((shade > SHADES) ? SHADES : (shade < 8 ? 8 : shade));

    s->size = (s->z < 70) ? 3u : 2u;
    if (!on_screen(sx, sy, s->size))
        return;
    if (speed >= 12) {
        int zt = s->z + speed * TRAIL;
        int tx = (int) (SCR_W / 2u) + s->x * FOCAL / zt;
        int ty = (int) (SCR_H / 2u) + s->y * FOCAL / zt;
        // two rows thick: the same line at y and y + 1, in the session colour
        // (x/y mode on the back frame). The tail lies between the screen
        // centre and the head, so the whole streak is on screen
        dots_flush();
        blns_gpu_colour_set(&blns, c);
        blns_gpu_line_px(&blns, sx, sy, tx, ty, NULL);
        blns_gpu_line_px(&blns, sx, sy + 1, tx, ty + 1, NULL);
    } else {
        dot(sx, sy, s->size, c);
    }
}

void fx_stars(void)
{
    mem = fx_mem();
    const unsigned frames = FRAMES(900u);
    uint32_t back = F1;
    fx_box_t box;

    fx_clear();
    for (int i = 0; i < SHADES; i++)
        pal[1 + i] = rgb_lerp(0x182040u, 0xffffffu, (unsigned) i, SHADES - 1);
    fx_logo_light(PAL_LOGO, 0);
    pal_show();
    fx_logo_index(&mem->rows);

    for (unsigned i = 0; i < STAR_NR; i++)
        respawn(&star[i], Z_NEAR + (int) (fx_rand() % (unsigned) (Z_FAR - Z_NEAR)));

    for (unsigned f = 0; f < frames; f++) {
        // timeline out of 256: cruise 0-39, warp 40-139, slowing 140-255;
        // the logo appears at 140 and reaches full size at 240
        unsigned ph = f * 256u / frames;
        int      speed;
        if (ph < 40u)
            speed = 4;
        else if (ph < 140u)
            speed = 4 + (int) ((fx_sin((ph - 40u) * 128u / 100u) * 36) >> 14);
        else
            speed = 4 - (int) ((ph - 140u) * 3u / 116u);
#ifdef SIMULATION
        speed *= 4;
#endif
        int logo_z = 0;
        if (ph >= 140u)
            logo_z = (ph >= 240u) ? LOGO_Z_NEAR
                                  : LOGO_Z_FAR - (int) (ph - 140u) * (LOGO_Z_FAR - LOGO_Z_NEAR) / 100;
        if (logo_z)
            fx_logo_light(PAL_LOGO, (unsigned) (256 * LOGO_Z_NEAR / logo_z));
        // the back frame: cleared, the stars moved and drawn, the logo on top
        fill(back, SCR_W, SCR_H, 0);
        blns_gpu_xy_begin(&blns, back, 0, 0);      // the dots' and streaks' surface
        for (unsigned i = 0; i < STAR_NR; i++) {
            star_t *st = &star[i];
            st->z = (int16_t) (st->z - speed);
            if (st->z < Z_NEAR)
                respawn(st, Z_FAR);
            draw(st, speed);
        }
        dots_flush();
        if (logo_z) {
            unsigned w = (unsigned) (LOGO_W * LOGO_Z_NEAR / logo_z);
            fx_logo_scaled(back, (int) (SCR_W / 2u), (int) (SCR_H / 2u), w, &mem->rows, PAL_LOGO, &box);
        }
        blns_gpu_wait(&blns);
        blns_set_frame(&blns, 0, back);
        back = (back == F0) ? F1 : F0;
        pal_show();                            // waits for the end of a frame
        if (scene_check())
            break;
    }
    blns_set_frame(&blns, 0, F0);
}

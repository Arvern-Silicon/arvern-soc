//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c      (Tang Console 138K Belenos raycaster demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// demo_raycaster -- a walk through a maze in the style of the early
// first-person games, the aRVern logo on some of the walls, rendered by the
// aRVern core and the Belenos graphic controller on the RGB panel.
//
// By default an autopilot walks the corridors from cell to cell: at each
// cell centre it goes on straight or takes a side way at random (back only
// from a dead end), turns on the spot, then walks to the next centre.
// Floating aRVern logos spin in some corridors; a minimap sits in the
// top-right corner (SW[0] hides it). Switches take over:
// SW[3] walks forward, SW[2] turns left, SW[1] turns right. KEY[1] turns
// around. The LEDs show the heading. Double-buffered: each view is drawn in the hidden frame, then the
// refresh frame pointer flips to it.
//----------------------------------------------------------------------------
#include "ray.h"
#include "music_data.h"

const mus_tune_t *const *const demo_tunes    = music_tunes;
const unsigned                 demo_tunes_nr = sizeof(music_tunes) / sizeof(music_tunes[0]);

#define SPEED            (ONE / 24)            // map units per frame
#define TURN             8u                    // angle steps per frame
#define MARGIN           (ONE / 4)             // keep this far from walls

static player_t me = { 1 * ONE + ONE / 2, 1 * ONE + ONE / 2, 0 };
static unsigned around;                        // steps left of a turn-around

// Autopilot: the cell it walks to and the heading it walks with (0, 256, 512
// or 768: +x, +y, -x, -y); a fresh start re-aims from the current cell
static int      goal_x, goal_y;
static unsigned goal_a;
static bool     pilot_on;

static uint32_t rnd = 0x2545F491u;
uint32_t fx_rand_bits(void)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

static void move(int32_t dist)
{
    int32_t nx = me.x + (int32_t) ((int64_t) rcos(me.a) * dist >> 14);
    int32_t ny = me.y + (int32_t) ((int64_t) rsin(me.a) * dist >> 14);
    int32_t mx = (nx > me.x) ? MARGIN : -MARGIN;
    int32_t my = (ny > me.y) ? MARGIN : -MARGIN;

    // slide along walls: each axis on its own
    if (!ray_wall(nx + mx, me.y))
        me.x = nx;
    if (!ray_wall(me.x, ny + my))
        me.y = ny;
}

static const int step_x[4] = { 1, 0, -1, 0 };
static const int step_y[4] = { 0, 1, 0, -1 };

static int open_cell(int cx, int cy)
{
    return !ray_wall(cx * ONE + ONE / 2, cy * ONE + ONE / 2);
}

// At a cell centre: the next cell, straight on more often than not
static void choose(void)
{
    unsigned d = goal_a / (ANG_NR / 4u), back = (d + 2u) & 3u, pick[4], n = 0;
    int cx = goal_x, cy = goal_y;

    if (open_cell(cx + step_x[d], cy + step_y[d]) && (fx_rand_bits() % 10u) < 6u) {
        pick[n++] = d;
    } else {
        for (unsigned k = 0; k < 4u; k++)
            if (k != back && open_cell(cx + step_x[k], cy + step_y[k]))
                pick[n++] = k;
    }
    d = n ? pick[fx_rand_bits() % n] : back;
    goal_x = cx + step_x[d];
    goal_y = cy + step_y[d];
    goal_a = d * (ANG_NR / 4u);
}

static int32_t toward(int32_t v, int32_t target, int32_t step)
{
    if (v < target)
        return (target - v > step) ? v + step : target;
    return (v - target > step) ? v - step : target;
}

static void autopilot(void)
{
    if (!pilot_on) {                           // aim from where the walk stands
        goal_x   = me.x >> 16;
        goal_y   = me.y >> 16;
        goal_a   = ((me.a + ANG_NR / 8u) & ANG_MASK) / (ANG_NR / 4u) * (ANG_NR / 4u);
        pilot_on = true;
    }
    // turn on the spot to the heading, the shortest way
    unsigned diff = (goal_a - me.a) & ANG_MASK;
    if (diff != 0u) {
        if (diff <= TURN || diff >= ANG_NR - TURN)
            me.a = goal_a;
        else
            me.a = (me.a + ((diff < ANG_NR / 2u) ? TURN : ANG_NR - TURN)) & ANG_MASK;
        return;
    }
    // walk to the goal cell centre, then pick the next one
    int32_t gx = goal_x * ONE + ONE / 2, gy = goal_y * ONE + ONE / 2;
    me.x = toward(me.x, gx, SPEED);
    me.y = toward(me.y, gy, SPEED);
    if (me.x == gx && me.y == gy)
        choose();
}

#define PASS_FRAMES      1800u                 // frames of one measured pass

int main(void)
{
    uint32_t back = F1;
    bool     key_prev = false;
    int32_t  left, right;
    unsigned t = 0, step = 0;                  // animation time, walking phase

    ray_init();
    demo_init();
    ray_upload();
    blns_set_pix_double(&blns, true);
    demo_music_scene(0);                       // the music (one tune: no scene list)
#ifdef SIMULATION
    lks_led_set(&lks, 1);
#else
    demo_meter_leds = true;                    // the LEDs show the load bar
#endif

#ifdef SIMULATION
    for (unsigned f = 0; f < 24u; f++) {
#else
    for (;;) {
#endif
        // head bob: the horizon sways up to 2 lines with the steps
        int bob = (int) (rsin(step) * 2 >> 14);
        (void) ray_frame(back, &me, bob, &left, &right);
        ray_sprites(back, &me, bob, t);
        if (!(lks_switches(&lks) & LKS_SW(0)))
            ray_minimap(back, &me);
        blns_gpu_wait(&blns);
        ray_animate(t++);
        if (t % PASS_FRAMES == 0u)             // a measurement pass (demo_perf.py)
            demo_pass_done();
        blns_set_frame(&blns, 0, back);
        pal_show();                            // with the frame flip, at a frame start
        back = (back == F0) ? F1 : F0;

        bool key = lks_key_pressed(&lks, 1) != 0;
        if (key && !key_prev) {
            around   = ANG_NR / 2u / TURN;
            pilot_on = false;
        }
        key_prev = key;

        int32_t  ox = me.x, oy = me.y;
        uint32_t sw = lks_switches(&lks);
        if (around) {
            me.a = (me.a + TURN) & ANG_MASK;
            around--;
        } else if (sw & (LKS_SW(3) | LKS_SW(2) | LKS_SW(1))) {
            if (sw & LKS_SW(2))
                me.a = (me.a - TURN) & ANG_MASK;
            if (sw & LKS_SW(1))
                me.a = (me.a + TURN) & ANG_MASK;
            if (sw & LKS_SW(3))
                move(SPEED);
            pilot_on = false;
        } else {
            autopilot();
#ifdef SIMULATION
            for (unsigned k = 0; k < 3u; k++)
                autopilot();
#endif
        }
        if (me.x != ox || me.y != oy)
            step += 24u;                       // a step every ~40 frames of walking
    }
#ifdef SIMULATION
    lks_led_set(&lks, LED_SIM_DONE);
    for (;;)
        wait_frames(1);
#endif
}

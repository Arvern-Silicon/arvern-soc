//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo_kit.c  (Tang Console 138K Belenos demos)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Shared layer of the Belenos demos (see demo_kit.h).
//----------------------------------------------------------------------------
#include "demo_kit.h"
#include "tang_board.h"
#include "tang_video.h"


// Refresh timer: the Belenos DPI driver takes a frame at the panel's own
// rate and does not use the refresh request, so the period only paces
// Belenos' refresh timer.
#ifdef SIMULATION
#define REFRESH_PERIOD   400000u
#else
#define REFRESH_PERIOD   (BOARD_HCLK_HZ / 60u)
#endif

blns_t   blns;
aclint_t aclint;
lks_t    lks;
uint32_t pal[256];
uint32_t scr_swap = BLNS_SWAP_X | BLNS_SWAP_Y;

static bool     key_prev;

//==========================================================
// LOAD METER
//==========================================================

demo_perf_t demo_perf[DEMO_SCENES_MAX];
volatile uint32_t trace_mark;
bool        demo_meter_leds;

static unsigned scene_cur;
static uint32_t t_resume;                     // end of the previous frame wait
static uint32_t frame_nr;
static uint32_t win_busy, win_total;          // LED bar window

static void meter_wait_begin(uint32_t *t_wait)
{
    *t_wait = aclint_mtime_lo(&aclint);
}

static void meter_wait_end(uint32_t t_wait)
{
    uint32_t now   = aclint_mtime_lo(&aclint);
    uint32_t busy  = t_wait - t_resume;
    uint32_t total = now - t_resume;
    demo_perf_t *p = &demo_perf[scene_cur];

    t_resume   = now;
    p->frames += 1u;
    p->busy   += busy;
    p->total  += total;

    win_busy  += busy;
    win_total += total;
    if (win_total >= BOARD_TIME_HZ / 8u) {              // 8 bar updates per second
        if (demo_meter_leds || (lks_switches(&lks) & LKS_SW(2))) {
            unsigned n = (win_busy * 8u + win_total / 2u) / win_total;
            lks_led_set(&lks, (n >= 8u) ? 0xFFu : ((1u << n) - 1u));
        }
        win_busy = win_total = 0;
    }
}

//==========================================================
// HDMI HOT-PLUG
//==========================================================

// The HDMI link and the monitor's hot-plug line: the board's hot-plug
// service (tang_video_poll: the driver's blns_dpi_hotplug_poll, its policy in
// belenos.h) restarts the link when a monitor appears or asks for it; KEY[1]
// held for HDMI_KEY_MS forces a restart (its press also skips the scene).
#define HDMI_KEY_MS          1000u

uint32_t              hdmi_restarts;
static uint32_t       hdmi_key_t0;
static bool           hdmi_key_q, hdmi_key_done;


static void hdmi_watch(void)
{
    if (!tang_video_hdmi())
        return;
    const uint32_t now = tang_video_ms();
    const bool     key = lks_key_pressed(&lks, 1) != 0;
    if (key && !hdmi_key_q) {
        hdmi_key_t0   = now;
        hdmi_key_done = false;
    }
    const bool key_long = key && !hdmi_key_done && (now - hdmi_key_t0) >= HDMI_KEY_MS;
    hdmi_key_q = key;
    if (key_long)
        hdmi_key_done = true;
    if (tang_video_poll(key_long) == BLNS_HP_RESTARTED)
        hdmi_restarts++;                // the driver placed the picture again
}

static void frame_wait(unsigned n)
{
    uint32_t t;

    meter_wait_begin(&t);
    if (n == 1u)
        blns_wait_frame(&blns);
    else
        blns_wait_frames(&blns, (uint16_t) n);
    meter_wait_end(t);
    trace_mark = ++frame_nr;
    hdmi_watch();
}

//==========================================================
// TIMING
//==========================================================

void wait_ms(uint32_t ms)
{
    const uint32_t t0 = aclint_mtime_lo(&aclint);

    while (aclint_mtime_lo(&aclint) - t0 < ms * (BOARD_TIME_HZ / 1000u))
        ;
}

uint32_t ms_now(void)
{
    return aclint_mtime_lo(&aclint) / (BOARD_TIME_HZ / 1000u);
}

void wait_frames(unsigned n)
{
    frame_wait(n);
}

void wait_fence_frame(uint32_t tag)
{
    uint32_t t;

    meter_wait_begin(&t);
    blns_gpu_wait_fence(&blns, tag);
    meter_wait_end(t);
    trace_mark = ++frame_nr;
    hdmi_watch();
}

//==========================================================
// PALETTE
//==========================================================

uint32_t rgb_lerp(uint32_t a, uint32_t b, unsigned t, unsigned t_max)
{
    uint32_t out = 0;

    for (unsigned sh = 0; sh < 24u; sh += 8u) {
        int ca = (int) ((a >> sh) & 0xFFu);
        int cb = (int) ((b >> sh) & 0xFFu);
        out |= (uint32_t) (ca + (cb - ca) * (int) t / (int) t_max) << sh;
    }
    return out;
}

void pal_blend(const uint32_t *a, const uint32_t *b, unsigned t, unsigned t_max)
{
    for (unsigned i = 0; i < 256u; i++)
        pal[i] = rgb_lerp(a[i], b[i], t, t_max);
}

void pal_show(void)
{
    blns_lut_present(&blns, pal, 256);          // the hidden bank, then selected
    frame_wait(1);                            // the refresh takes LUT_CFG at a frame start
}

//==========================================================
// SET-UP
//==========================================================

void demo_init(void)
{
    const aclint_config_t acfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    const tang_video_t    vid  = { .w = SCR_W, .h = SCR_H, .bpp = BLNS_BPP_8, .frame = F0 };

    aclint_init(&aclint, &acfg);
    lks_init(&lks, BOARD_LKS_BASE);
    tang_video_init(&blns, &aclint);

    // The board's display set-up (tang_video.h): the DPI driver scans the
    // frame in plain row order, 1:1 or scaled to the largest image of its
    // shape; on HDMI the frame at its own shape on a monitor that stretches the
    // signal to its EDID shape (else BOARD_HDMI_ASPECT), letterboxed without
    // BOARD_HDMI_STRETCH. The panel (or monitor) starts once the frames are clear
    scr_swap = tang_video_swap();
    tang_video_config(&vid);
    tang_video_picture();
    set_orientation(BLNS_SWAP_NONE);
    blns_set_frame(&blns, 1, 0);
    blns_select_frame(&blns, 0);

    fill(0, SCR_W, 2u * SCR_H, 0);
    blns_gpu_wait(&blns);
    blns_lut_load(&blns, 0, 0, pal, 256);
    blns_lut_load(&blns, 1, 0, pal, 256);
    blns_lut_select(&blns, true, 0);

    blns_refresh_period(&blns, REFRESH_PERIOD);
    tang_video_start();
    t_resume = aclint_mtime_lo(&aclint);
}

void set_orientation(uint32_t swap)
{
    blns_set_orientation(&blns, swap ^ SCR_SWAP);
}

//==========================================================
// DRAWING
//==========================================================

void fill(uint32_t addr, unsigned w, unsigned h, uint8_t colour)
{
    const blns_surf_t dst = { addr, 0, BLNS_SWAP_NONE };
    blns_gpu_fill(&blns, &dst, (uint16_t) w, (uint16_t) h, colour, BLNS_ROP_S);
}

void show_frame(uint32_t frame)
{
    blns_set_frame(&blns, 0, frame);
    frame_wait(1);
}

//==========================================================
// SCENES
//==========================================================

bool scene_check(void)
{
    bool key = lks_key_pressed(&lks, 1) != 0;
    bool hit = key && !key_prev;

    key_prev = key;
    while (!hit && (lks_switches(&lks) & LKS_SW(0))) {        // hold
        wait_frames(1);
        key  = lks_key_pressed(&lks, 1) != 0;
        hit  = key && !key_prev;
        key_prev = key;
    }
    return hit;
}

bool scene_frame(void)
{
    wait_frames(1);
    return scene_check();
}

// Called after every pass through the scenes: a breakpoint here halts the demo
// with demo_perf[] holding exactly one pass (software/tools/demo_perf.py)
__attribute__((noinline)) void demo_pass_done(void)
{
    __asm__ volatile ("" ::: "memory");
}

// A demo with music (demo_music.c) changes the tune with each scene
__attribute__((weak)) void demo_music_scene(unsigned n)
{
    (void) n;
}

void demo_run(void (*const *scene)(void), unsigned nr)
{
    for (;;) {
        for (unsigned n = 0; n < nr; n++) {
            scene_cur = (n < DEMO_SCENES_MAX) ? n : DEMO_SCENES_MAX - 1u;
            demo_music_scene(n);
            if (!(lks_switches(&lks) & LKS_SW(2)))
                lks_led_set(&lks, n + 1u);
            scene[n]();
        }
        demo_pass_done();
#ifdef SIMULATION
        lks_led_set(&lks, LED_SIM_DONE);
        for (;;)
            wait_frames(1);
#endif
    }
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo_kit.c  (DE0-Nano-SoC Belenos demos)
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
#include "ili9341.h"
#include "de0_board.h"

// Panel bus timing (DIF_CFG.CLK_DIV): 8080 write cycle of 80 ns
#define LT24_CLK_DIV     1u

// Refresh: 60 frames per second on the board. In simulation a frame takes
// 320 x 240 x 4 = 307200 cycles to send; the period leaves a gap after it so
// the LUT and the GPU are not shared with the refresh all the time.
#ifdef SIMULATION
#define REFRESH_PERIOD   400000u
#else
#define REFRESH_PERIOD   (BOARD_HCLK_HZ / 60u)
#endif

blns_t   blns;
aclint_t aclint;
lks_t    lks;
uint32_t pal[256];

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
    blns_lut_present(&blns, pal, 256);        // the hidden bank, then selected
    frame_wait(1);                            // the refresh takes LUT_CFG at a frame start
}

//==========================================================
// SET-UP
//==========================================================

void demo_init(void)
{
    const aclint_config_t acfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    const blns_config_t   bcfg = { .reg_base = BOARD_BLNS_BASE, .vram_base = BOARD_VRAM_BASE, .hclk_hz = BOARD_HCLK_HZ };
    const blns_dbi_config_t dbi = { .spi = false, .clk_div = LT24_CLK_DIV, .hw_window = false };

    aclint_init(&aclint, &acfg);
    lks_init(&lks, BOARD_LKS_BASE);
    blns_init(&blns, &bcfg);
    blns_dbi_config(&blns, &dbi);

    // The panel window is set once (portrait 240 x 320); the 320 x 240 frame
    // is scanned column by column into it (CL_SWAP)
    blns_set_size(&blns, SCR_W, SCR_H);
    const ili9341_config_t panel = {
        .delay_ms     = wait_ms,
        .madctl       = ILI9341_MADCTL_BGR,
        .win_width    = SCR_H,
        .win_height   = SCR_W,
        .clear        = true,
        .clear_rgb888 = 0x000000u
    };
    ili9341_init(&blns, &panel);

    blns_set_mode(&blns, BLNS_BPP_8);
    set_orientation(BLNS_SWAP_NONE);
    blns_gpu_enable(&blns, true);
    blns_set_frame(&blns, 0, F0);
    blns_set_frame(&blns, 1, 0);
    blns_select_frame(&blns, 0);

    fill(0, SCR_W, 2u * SCR_H, 0);
    blns_gpu_wait(&blns);
    blns_lut_load(&blns, 0, 0, pal, 256);
    blns_lut_load(&blns, 1, 0, pal, 256);
    blns_lut_select(&blns, true, 0);

    blns_refresh_period(&blns, REFRESH_PERIOD);
#ifdef DEMO_SCAN_SYNC
    // Tear control, opt-in (-DDEMO_SCAN_SYNC=<line>, e.g. 300): the LT24 has
    // no TE line to the FPGA (lcd_te_i is tied low), but its RD line and data
    // bus are wired, so each refresh can wait until the panel's own scan
    // (Get_Scanline, polled by the DBI driver) passes <line>. The frame goes
    // out in the panel's row order (the portrait window, CL_SWAP) in about
    // 6 ms, faster than the panel scans it (about 14 ms): started just before
    // the scan wraps, it stays ahead of it, without a tear. Not yet checked on
    // a board: when no frame starts within 100 ms (the panel does not answer),
    // the sync is turned off again and the refresh runs free as before.
    blns_dbi_sync(&blns, DEMO_SCAN_SYNC, true, false);
    {
        const uint16_t n0 = blns_frame_num(&blns);
        const uint32_t t0 = aclint_mtime_lo(&aclint);
        while (blns_frame_num(&blns) == n0 && aclint_mtime_lo(&aclint) - t0 < 100u * (BOARD_TIME_HZ / 1000u))
            ;
        if (blns_frame_num(&blns) == n0)
            blns_dbi_sync(&blns, 0u, false, false);
    }
#endif
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

void demo_run(void (*const *scene)(void), unsigned nr)
{
    for (;;) {
        for (unsigned n = 0; n < nr; n++) {
            scene_cur = (n < DEMO_SCENES_MAX) ? n : DEMO_SCENES_MAX - 1u;
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

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo_kit.h  (DE0-Nano-SoC Belenos demos)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Shared layer of the Belenos demos on the LT24: panel and controller set-up,
// the double-buffered palette, timing, GPU fills and the scene framework.
//
// Display: 320 x 240, 8 bpp, two frames back to back in the VRAM (F0 at pixel
// address 0, F1 at 76800), scanned column by column into the portrait LT24.
// Frame pointer 0 is the one the refresh shows (moved to flip or scroll);
// frame pointer 1 is set to pixel address 0 and otherwise unused.
//
// Palette: pal[] is the working copy. pal_show() writes it into the LUT bank
// the refresh is not showing and swaps the banks at the next frame start.
//
// Scenes: a demo runs a list of scenes; each calls scene_frame() once per
// frame (or scene_check() after a call that already waited a frame), which
// applies SW[0] (hold) and KEY[1] (skip: the scene must end).
//
// Load meter: every frame wait measures the time spent since the previous
// one (drawing, GPU waits included) against the frame wait itself. SW[2]
// shows the load on the LEDs as a bar (8 LEDs = the whole frame time) instead
// of the scene number; demo_perf[] keeps per-scene totals (read them in the
// debugger).
//
// Trace marker: every frame wait ends by storing the frame number into
// trace_mark, which the simulation's instruction trace (asphalt.log) notes as
// "# mark <n>" and can start and stop on (sim/rtl_sim/run/run -frames A:B).
//----------------------------------------------------------------------------
#ifndef DEMO_KIT_H
#define DEMO_KIT_H

#include <stdbool.h>
#include <stdint.h>
#include "belenos.h"
#include "ahb_aclint.h"
#include "led_key_sw.h"

#define SCR_W            320u
#define SCR_H            240u
#define FRAME_PX         (SCR_W * SCR_H)
#define F0               0u                   // pixel address of frame 0
#define F1               FRAME_PX             // pixel address of frame 1
#define VRAM_SPARE       (2u * FRAME_PX)      // byte address of the 10 KB after F1

// The LEDs show this once the simulation build has played every scene once
#define LED_SIM_DONE     0xA5u

#define DEMO_SCENES_MAX  8u

typedef struct {
    uint32_t frames;                          // frame waits
    uint32_t busy;                            // mtime ticks between frame waits
    uint32_t total;                           // busy + waiting (load = busy / total)
} demo_perf_t;

extern demo_perf_t demo_perf[DEMO_SCENES_MAX];
extern volatile uint32_t trace_mark;          // frame number, after each frame wait
extern bool        demo_meter_leds;           // load bar always on (no SW[2] needed)

// Frame counts: the board runs at 60 frames per second; the simulation build
// shortens every scene so a whole demo fits in a few dozen frames
#ifdef SIMULATION
#define FRAMES(n)        (((n) + 31u) / 32u)
#else
#define FRAMES(n)        (n)
#endif

extern blns_t   blns;
extern aclint_t aclint;
extern lks_t    lks;
extern uint32_t pal[256];

// The LT24 is portrait: the refresh scans every landscape frame column by
// column into its window (DISP_CFG CL swap)
#define SCR_SWAP         BLNS_SWAP_CL

// Set-up: both frames black, both LUT banks loaded with pal[] (set it first)
void     demo_init(void);
// Scan orientation (BLNS_SWAP_*) as seen on the panel
void     set_orientation(uint32_t swap);

// Timing
uint32_t ms_now(void);
void     wait_ms(uint32_t ms);
void     wait_frames(unsigned n);
// GPU-paced frames (BLNS_CFG GPU_CMD2): a frame's GPU work is queued behind
// WAIT_REFR_DONE and ends with a fence; the next frame waits for that fence
// instead of the frame end (load meter and trace marker as a frame wait)
void     wait_fence_frame(uint32_t tag);

// Palette
void     pal_show(void);
uint32_t rgb_lerp(uint32_t a, uint32_t b, unsigned t, unsigned t_max);
void     pal_blend(const uint32_t *a, const uint32_t *b, unsigned t, unsigned t_max);

// One GPU fill (absolute pixel address, the display width as pitch)
void     fill(uint32_t addr, unsigned w, unsigned h, uint8_t colour);
// Show frame base 'frame' from the next refresh on and wait for it
void     show_frame(uint32_t frame);

// Scenes
bool     scene_frame(void);
bool     scene_check(void);
// Play the scenes in order, LEDs = scene number, for ever (once in simulation)
void     demo_run(void (*const *scene)(void), unsigned nr);
// End of a pass through the scenes (a breakpoint target for measurements)
void     demo_pass_done(void);

#endif // DEMO_KIT_H

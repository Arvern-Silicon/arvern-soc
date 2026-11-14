//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K VRAM-window vs GPU test)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Can the CPU, storing to the VRAM window as fast as it can, starve the GPU?
// Times (mtime ticks) a full-frame GPU fill alone, an unrolled store loop
// alone, and both together; when the stores end, tvs_filled counts the frame
// words the fill has written so far. Then the same three, each started as a
// display refresh (column-by-column CL swap) begins: tvs_r*. Results in tvs_*
// (read them in the debugger).
//----------------------------------------------------------------------------
#include "demo_kit.h"

#define STORES           20000u                // word stores per loop run
#define STORE_AREA       (VRAM_SPARE / 4u)     // word address of the store target

volatile uint32_t tvs_fill, tvs_store, tvs_both, tvs_store_in_both, tvs_done, tvs_filled;
volatile uint32_t tvs_rfill, tvs_rstore, tvs_rsib, tvs_rfilled;

// Words of frame 0 already holding the fill colour (the fill runs in address order)
static uint32_t filled(uint8_t c)
{
    volatile uint32_t *v = (volatile uint32_t *) blns_vram8(&blns);
    uint32_t w = 0x01010101u * c, lo = 0, hi = FRAME_PX / 4u;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (v[mid] == w) lo = mid + 1u; else hi = mid;
    }
    return lo;
}

__attribute__((noinline)) static void store_loop(void)
{
    volatile uint32_t *v = (volatile uint32_t *) blns_vram8(&blns) + STORE_AREA;

    for (unsigned i = 0; i < STORES / 8u; i++) {
        v[0] = i; v[1] = i; v[2] = i; v[3] = i;
        v[4] = i; v[5] = i; v[6] = i; v[7] = i;
    }
}

// Return as the refresh starts sending a frame
static void refresh_start(void)
{
    while (blns.regs->STATUS & BLNS_STATUS_REFRESH_ACTIVE_Msk)
        ;
    while (!(blns.regs->STATUS & BLNS_STATUS_REFRESH_ACTIVE_Msk))
        ;
}

static void gpu_fill(uint8_t c)
{
    fill(F0, SCR_W, SCR_H, c);
}

int main(void)
{
    demo_init();
    for (;;) {
        uint32_t t = aclint_mtime_lo(&aclint);
        gpu_fill((uint8_t) tvs_done);
        blns_gpu_wait(&blns);
        tvs_fill = aclint_mtime_lo(&aclint) - t;

        t = aclint_mtime_lo(&aclint);
        store_loop();
        tvs_store = aclint_mtime_lo(&aclint) - t;

        t = aclint_mtime_lo(&aclint);
        gpu_fill((uint8_t) (tvs_done + 0x80u));
        store_loop();
        tvs_store_in_both = aclint_mtime_lo(&aclint) - t;
        tvs_filled = filled((uint8_t) (tvs_done + 0x80u));
        blns_gpu_wait(&blns);
        tvs_both = aclint_mtime_lo(&aclint) - t;

        refresh_start();
        t = aclint_mtime_lo(&aclint);
        gpu_fill((uint8_t) (tvs_done + 0x40u));
        blns_gpu_wait(&blns);
        tvs_rfill = aclint_mtime_lo(&aclint) - t;
        refresh_start();
        t = aclint_mtime_lo(&aclint);
        store_loop();
        tvs_rstore = aclint_mtime_lo(&aclint) - t;
        refresh_start();
        t = aclint_mtime_lo(&aclint);
        gpu_fill((uint8_t) (tvs_done + 0xC0u));
        store_loop();
        tvs_rsib = aclint_mtime_lo(&aclint) - t;
        tvs_rfilled = filled((uint8_t) (tvs_done + 0xC0u));
        blns_gpu_wait(&blns);
        tvs_done++;
        wait_ms(20);
    }
}

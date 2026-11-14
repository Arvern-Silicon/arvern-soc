//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_common.c  (DE0-Nano-SoC Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Shared helpers of the effects (see fx.h).
//----------------------------------------------------------------------------
#include "fx.h"

int16_t sin_q14[256];

// Sine table, built at start-up: a quarter wave by the Chebyshev recurrence
// sin((n + 1) d) = 2 cos(d) sin(n d) - sin((n - 1) d), in Q30, then mirrored
COLD void fx_sin_init(void)
{
    const int64_t two_cos = 2LL * 1073418433;            // 2 cos(2 pi / 256), Q30
    int64_t s0 = 0, s1 = 26350943;                     // sin(0), sin(2 pi / 256), Q30

    for (unsigned n = 0; n <= 64u; n++) {
        int16_t v = (int16_t) ((s0 + (1 << 15)) >> 16);  // Q30 to Q14, rounded
        sin_q14[n] = v;
        sin_q14[128u - n] = v;
        sin_q14[(128u + n) & 255u] = (int16_t) -v;
        sin_q14[(256u - n) & 255u] = (int16_t) -v;
        int64_t s2 = ((two_cos * s1) >> 30) - s0;
        s0 = s1;
        s1 = s2;
    }
}

static uint32_t fx_scratch[FX_SCRATCH_SIZE / 4u];

void *fx_mem(void)
{
    return fx_scratch;
}

static uint32_t rand_state = 0x2545F491u;

uint32_t fx_rand(void)
{
    uint32_t x = rand_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_state = x;
    return x;
}

void fx_clear(void)
{
    for (unsigned i = 0; i < 256u; i++)
        pal[i] = 0;
    pal_show();
    fill(F0, SCR_W, 2u * SCR_H, 0);
    blns_gpu_wait(&blns);
    show_frame(F0);
}

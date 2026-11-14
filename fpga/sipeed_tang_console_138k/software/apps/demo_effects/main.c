//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// demo_effects -- classic demo effects on Belenos, one after the other, in a
// loop. The LEDs show the effect number; KEY[1] skips to the next effect,
// SW[0] holds the current one. The effects draw at the 480 x 272 of the RGB
// panel, shown 1:1.
//----------------------------------------------------------------------------
#include "fx.h"
#include "music_data.h"

const mus_tune_t *const *const demo_tunes    = music_tunes;
const unsigned                 demo_tunes_nr = sizeof(music_tunes) / sizeof(music_tunes[0]);

static void (*const effect[])(void) = {
    fx_stars,
    fx_plasma,
    fx_torus,
    fx_fire,
    fx_scroll,
};

int main(void)
{
    fx_sin_init();
    demo_init();
    demo_run(effect, sizeof(effect) / sizeof(effect[0]));
}

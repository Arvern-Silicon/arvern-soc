//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo_music.c  (Tang Console 138K demo kit)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// A demo with music links this file and defines demo_tunes (one tune per
// scene, the last one for the scenes after it) and demo_tunes_nr: demo_run()
// then starts the music with the first scene and changes the tune with each
// scene. The simulation build stays silent.
//----------------------------------------------------------------------------
#include "demo_kit.h"
#include "music.h"
#include "music_i2s.h"

extern const mus_tune_t *const *const demo_tunes;
extern const unsigned                 demo_tunes_nr;

void demo_music_scene(unsigned n)
{
#ifdef SIMULATION
    (void) n;
#else
    static bool tried, ok;
    if (!tried) {
        tried = true;
        ok = music_init();
    }
    if (ok && demo_tunes_nr)
        mus_play(demo_tunes[(n < demo_tunes_nr) ? n : demo_tunes_nr - 1u]);
#endif
}

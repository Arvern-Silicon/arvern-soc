//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (DE0-Nano-SoC aRVern logo demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// demo_arvern_logo -- the aRVern logo on Belenos, one scene per controller
// feature, in a loop. The LEDs show the scene number; KEY[1] skips to the
// next scene, SW[0] holds the current one. The panel is the LT24 on GPIO_0.
//----------------------------------------------------------------------------
#include "gfx.h"
#include "scenes.h"

static void (*const scene[])(void) = {
    scene_paint,
    scene_power,
    scene_themes,
    scene_spin,
    scene_sprites,
    scene_credits,
};

int main(void)
{
    gfx_init();
    demo_run(scene, sizeof(scene) / sizeof(scene[0]));
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c      (DE0-Nano-SoC graphic demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// demo_ogfx430 -- Belenos graphic demo for the DE0-Nano-SoC, a port of the
// openGFX430 demo. It cycles through the 16, 8, 4, 2 and 1 bpp modes; the
// LEDs show the current colour depth. The panel is the LT24 on GPIO_0.
//----------------------------------------------------------------------------
#include "gfx_controller.h"
#include "demo.h"

//---------------------------------------------------//
// Graphic mode initialization                       //
//---------------------------------------------------//
static void gfx_mode_init(blns_mode_t gfx_mode, uint32_t refresh_period)
{
  init_gfx_ctrl(gfx_mode, refresh_period);

  // LUT configuration: hardware palette 0, no colour table
  blns_lut_select(&blns, false, 0);
  blns_hw_palette(&blns, 0, HW_LUT_BG_BLACK, HW_LUT_FG_WHITE);

  // Frame pointers
  frame_ptr(0, PIX_ADDR(0, 0));
  frame_ptr(1, PIX_ADDR(0, 0));

  blns_select_frame(&blns, 0);

  start_gfx_ctrl();
  mode_timer_start();
}

//---------------------------------------------------//
// Main                                              //
//---------------------------------------------------//
int main(void)
{
  LED_CTRL = 0x00;
  init_lcd();

  while (1) {

    LED_CTRL = 16;
    gfx_mode_init(GFX_16_BPP, REFR_62_FPS);
    demo_16bpp();

    LED_CTRL = 8;
    gfx_mode_init(GFX_8_BPP,  REFR_1000_FPS);
    demo_8bpp();

    LED_CTRL = 4;
    gfx_mode_init(GFX_4_BPP,  REFR_1000_FPS);
    demo_4bpp();

    LED_CTRL = 2;
    gfx_mode_init(GFX_2_BPP,  REFR_1000_FPS);
    demo_2bpp();

    LED_CTRL = 1;
    gfx_mode_init(GFX_1_BPP,  REFR_1000_FPS);
    demo_1bpp();
  }

  return 0;
}

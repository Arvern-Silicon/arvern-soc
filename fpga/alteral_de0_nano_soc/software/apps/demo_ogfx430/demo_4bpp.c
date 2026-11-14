//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo_4bpp.c  (DE0-Nano-SoC graphic demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 4 bpp: the 16 CGA colours, then random-colour boxes redrawn by the GPU at
// four speeds.
//----------------------------------------------------------------------------
#include "demo.h"
#include "gfx_controller.h"

//---------------------------------------------------//
// 8BPP DEMO
//---------------------------------------------------//
void demo_4bpp(void) {

  uint16_t      lfsr;
  uint16_t      lfsr_lsb;
  unsigned int  line, column;
  unsigned int  color        = 0;
  unsigned int  use_gpu      = 1;
  unsigned int  wait_sel     = 0;
  unsigned int  x_coord      = 0;
  unsigned int  y_coord      = 0;
  unsigned int  loop;

  const uint16_t offset_x    = 35;
  const uint16_t offset_y    = 95;
  const uint16_t char_width  =  7;

  // Screen introduction
  gpu_fill (PIX_ADDR(0,                                    0                    ), 320,          480,          0x0000, DST_SWAP_NONE); // Background

  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+3*char_width, offset_y+0*char_width), 1*char_width, 7*char_width, 0x000B, DST_SWAP_NONE); // 4
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+0*char_width, offset_y+4*char_width), 5*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+2*char_width, offset_y+1*char_width), 1*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+1*char_width, offset_y+2*char_width), 1*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+0*char_width, offset_y+3*char_width), 1*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);

  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+0*char_width, offset_y+0*char_width), 1*char_width, 6*char_width, 0x000B, DST_SWAP_NONE); // b
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+1*char_width, offset_y+6*char_width), 3*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+1*char_width, offset_y+4*char_width), 1*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+2*char_width, offset_y+3*char_width), 2*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+4*char_width, offset_y+4*char_width), 1*char_width, 2*char_width, 0x000B, DST_SWAP_NONE);

  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+0*char_width, offset_y+2*char_width), 1*char_width, 5*char_width, 0x000B, DST_SWAP_NONE); // p
  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+1*char_width, offset_y+2*char_width), 3*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+1*char_width, offset_y+4*char_width), 3*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+4*char_width, offset_y+3*char_width), 1*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);

  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+0*char_width, offset_y+2*char_width), 1*char_width, 5*char_width, 0x000B, DST_SWAP_NONE); // p
  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+1*char_width, offset_y+2*char_width), 3*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+1*char_width, offset_y+4*char_width), 3*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+4*char_width, offset_y+3*char_width), 1*char_width, 1*char_width, 0x000B, DST_SWAP_NONE);

  delay_ms(500);
  delay_ms(500);
  delay_ms(500);
  delay_ms(500);

  // Clear background
  gpu_fill (PIX_ADDR(0,                                    0                    ), 320,          240,          0x0000, DST_SWAP_NONE);

  // Wait for on-going GPU operation to be done before moving on
  gpu_wait_done();

  // Fill the screen with all possible colors
  color = 0x0000;
  for( line = 0; line <4; line = line + 1 ) {
    for( column = 0; column < 4; column = column + 1 ) {

      draw_block(PIX_ADDR(x_coord, y_coord), 77, 57, color, DST_SWAP_NONE, 0);
      if (color==15) { color= 0;}
      else           { color++; }
      x_coord += 80;
    }
    y_coord += 60;
    x_coord  =  0;
  }
  delay_ms(500);
  delay_ms(500);

  // Initialize LFSR for random color generation
  lfsr = 0xACEB;

  // Loop the demo
  loop = 0;
  while (loop <4) {

    // Select rotation & GPU use
    switch(loop & 0x0003) {
    case 0 : blns_set_orientation(&blns, DST_SWAP_CL);         wait_sel = 3;
             break;
    case 1 : blns_set_orientation(&blns, DST_SWAP_CL);         wait_sel = 2;
             break;
    case 2 : blns_set_orientation(&blns, DST_SWAP_CL);         wait_sel = 1;
             break;
    default: blns_set_orientation(&blns, DST_SWAP_CL);         wait_sel = 0;
             break;
    }
    loop++;
    mode_timer_start();

    // Move the starting point of the buffer refresh
    frame_pace(0);
    while (!mode_timer_expired()) {

      // Fill the screen with random colors
      x_coord           = 0;
      y_coord           = 0;
      for( line = 0; line <4; line = line + 1 ) {
        for( column = 0; column < 4; column = column + 1 ) {

          // Compute next color from LFSR
          lfsr_lsb = lfsr & 1;                           /* Get LSB (i.e., the output bit). */
          lfsr     >>= 1;                           /* Shift register */
          lfsr     ^= (-lfsr_lsb) & 0xB400u;           /* If the output bit is 1, apply toggle mask.
                                                    * The value has 1 at bits corresponding
                                                    * to taps, 0 elsewhere. */
          color    = lfsr & 0x000F;

          // Draw the box
          draw_block(PIX_ADDR(x_coord, y_coord), 77, 57, color, DST_SWAP_NONE, use_gpu);
          x_coord += 80;
        }
        y_coord += 60;
        x_coord  =  0;
      }

      // Wait according to config
      switch(wait_sel) {
      case 0 : frame_pace(2);
               break;
      case 1 : frame_pace(4);
               break;
      case 2 : frame_pace(7);
               break;
      default: frame_pace(10);
               break;
      }
    }
  }
};

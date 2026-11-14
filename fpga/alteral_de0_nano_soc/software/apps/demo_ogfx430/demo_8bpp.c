//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo_8bpp.c  (DE0-Nano-SoC graphic demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 8 bpp: the 256 RGB332 colours, then a diagonal scroll of the refresh frame
// pointer at four speeds (FRAME_CNT pacing).
//----------------------------------------------------------------------------
#include "demo.h"
#include "gfx_controller.h"

//---------------------------------------------------//
// 8BPP DEMO
//---------------------------------------------------//
void demo_8bpp(void) {

  unsigned int  line, column;
  unsigned int  color        = 0;
  unsigned int  use_gpu      = 0;
  unsigned int  wait_sel     = 0;
  uint16_t      x_coord      = 0;
  uint16_t      y_coord      = 0;
  unsigned int  loop;
  uint16_t      address      = 0;  // 16-bit: the scroll wraps inside the two 8 bpp frames

  const uint16_t offset_x    = 35;
  const uint16_t offset_y    = 95;
  const uint16_t char_width  =  7;

  // Screen introduction
  gpu_fill (PIX_ADDR(0,                                    0                    ), 320,          480,          0x0000, DST_SWAP_NONE); // Background

  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+1*char_width, offset_y+0*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE); // 8
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+1*char_width, offset_y+3*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+1*char_width, offset_y+6*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+0*char_width, offset_y+1*char_width), 1*char_width, 2*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+4*char_width, offset_y+1*char_width), 1*char_width, 2*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+0*char_width, offset_y+4*char_width), 1*char_width, 2*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+1*6*char_width+4*char_width, offset_y+4*char_width), 1*char_width, 2*char_width, 0x001C, DST_SWAP_NONE);

  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+0*char_width, offset_y+0*char_width), 1*char_width, 6*char_width, 0x001C, DST_SWAP_NONE); // b
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+1*char_width, offset_y+6*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+1*char_width, offset_y+4*char_width), 1*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+2*char_width, offset_y+3*char_width), 2*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+3*6*char_width+4*char_width, offset_y+4*char_width), 1*char_width, 2*char_width, 0x001C, DST_SWAP_NONE);

  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+0*char_width, offset_y+2*char_width), 1*char_width, 5*char_width, 0x001C, DST_SWAP_NONE); // p
  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+1*char_width, offset_y+2*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+1*char_width, offset_y+4*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+4*6*char_width+4*char_width, offset_y+3*char_width), 1*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);

  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+0*char_width, offset_y+2*char_width), 1*char_width, 5*char_width, 0x001C, DST_SWAP_NONE); // p
  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+1*char_width, offset_y+2*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+1*char_width, offset_y+4*char_width), 3*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);
  gpu_fill (PIX_ADDR(offset_x+5*6*char_width+4*char_width, offset_y+3*char_width), 1*char_width, 1*char_width, 0x001C, DST_SWAP_NONE);

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
  for( line = 0; line <29; line = line + 1 ) {
    for( column = 0; column < 20; column = column + 1 ) {

      draw_block(PIX_ADDR(x_coord, y_coord), 13, 13, color, DST_SWAP_NONE, use_gpu);
      if (color==255) { color= 0;}
      else            { color++; }
      x_coord += 16;
    }
    y_coord += 16;
    x_coord  =  0;
  }
  delay_ms(500);
  delay_ms(500);

  // Use the frame pointer 1 for refresh
  frame_ptr(1, 0x0000);
  blns_select_frame(&blns, 1);

  // Initialize coordinates for refresh
  x_coord      = 0;
  y_coord      = 0;

  // Loop the demo
  loop = 0;
  while (loop <4) {

    // Select rotation & GPU use
    switch(loop & 0x0003) {
    case 0 : blns_set_orientation(&blns, DST_SWAP_CL);  wait_sel = 3;
             break;
    case 1 : blns_set_orientation(&blns, DST_SWAP_CL);  wait_sel = 2;
             break;
    case 2 : blns_set_orientation(&blns, DST_SWAP_CL);  wait_sel = 1;
             break;
    default: blns_set_orientation(&blns, DST_SWAP_CL);  wait_sel = 0;
             break;
    }
    loop++;
    mode_timer_start();

    // Move the starting point of the buffer refresh
    frame_pace(0);
    while (!mode_timer_expired()) {

      frame_ptr(1, address);

      // Compute next address
      x_coord +=1;
      y_coord +=1;
      address = PIX_ADDR(x_coord, y_coord);

      // Wait according to config
      switch(wait_sel) {
      case 0 : break;
      case 1 : frame_pace(1);
                 break;
      case 2 : frame_pace(2);
               break;
      default: frame_pace(3);
                  break;
      }
    }
  }

  // Restore refresh configuration
  frame_ptr(0, 0x0000);
  blns_select_frame(&blns, 0);
};

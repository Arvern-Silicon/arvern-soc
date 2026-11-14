//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gfx_controller.h  (DE0-Nano-SoC graphic demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Demo-side view of Belenos: the openGFX430 demo names mapped onto the Belenos
// driver library (arvern-belenos/sw). The frame buffer is 320 x 240
// (landscape), scanned column by column (DISP_CFG.CL_SWAP) into the portrait
// LT24 panel.
//----------------------------------------------------------------------------
#ifndef GFX_CONTROLLER_H
#define GFX_CONTROLLER_H

#include <stdint.h>
#include "belenos.h"

//----------------------------------------------------------
// GLOBAL CONFIGURATION
//----------------------------------------------------------
#define SCREEN_WIDTH         320
#define SCREEN_HEIGHT        240

#define VRAM_WORDS           38400                  // 320 x 240 at 16 bpp

#define CPU_HZ               50000000u

// Convert pixel coordinates into a pixel address
#define PIX_ADDR(X, Y) ((((uint32_t)(Y)) * ((uint32_t)(SCREEN_WIDTH))) + ((uint32_t)(X)))

//----------------------------------------------------------
// BOARD
//----------------------------------------------------------
#define LED_CTRL          (*(volatile uint32_t *) 0x10040000u)

#define BLNS_REG_BASE     0x10050000u
#define BLNS_VRAM_BASE    0x82000000u


//----------------------------------------------------------
// BELENOS (the device handle)
//----------------------------------------------------------
// openGFX430's registers are set through the driver: DISP_CFG by
// blns_set_orientation, FRAME0/1_PTR by frame_ptr (below), FRAME_SEL by
// blns_select_frame, LUT_CFG by blns_hw_palette / blns_lut_select, the
// FRAME_CNT pacing by frame_pace
extern blns_t blns;


//----------------------------------------------------------
// FIELD VALUES (openGFX430 names)
//----------------------------------------------------------

// Colour depth
#define  GFX_1_BPP                 BLNS_BPP_1
#define  GFX_2_BPP                 BLNS_BPP_2
#define  GFX_4_BPP                 BLNS_BPP_4
#define  GFX_8_BPP                 BLNS_BPP_8
#define  GFX_16_BPP                BLNS_BPP_16

// DISP_CFG and the GPU / gate swap fields
#define  DST_SWAP_NONE             BLNS_SWAP_NONE
#define  DST_SWAP_CL               BLNS_SWAP_CL
#define  DST_SWAP_Y                BLNS_SWAP_Y
#define  DST_SWAP_Y_CL             (BLNS_SWAP_Y | BLNS_SWAP_CL)
#define  DST_SWAP_X                BLNS_SWAP_X
#define  DST_SWAP_X_CL             (BLNS_SWAP_X | BLNS_SWAP_CL)
#define  DST_SWAP_X_Y              (BLNS_SWAP_X | BLNS_SWAP_Y)
#define  DST_SWAP_X_Y_CL           (BLNS_SWAP_X | BLNS_SWAP_Y | BLNS_SWAP_CL)

// Refresh periods (hclk cycles)
#ifdef SIMULATION
#define  REFR_62_FPS               1u                               // continuous
#define  REFR_1000_FPS             1u
#else
#define  REFR_62_FPS               (CPU_HZ / 62u)
#define  REFR_1000_FPS             (CPU_HZ / 1000u)
#endif

// Hardware palettes (blns_hw_palette): background and foreground colours
#define  HW_LUT_BG_BLACK           0x0u
#define  HW_LUT_FG_WHITE           0xFu

// Software gate CFG (window walk; at 16 bpp mask mode writes the pixel)
#define  GATE_MSK_MODE             0x0000u
#define  GATE_WIN_MODE             0x0000u

// GPU configuration argument of gpu_fill / gpu_copy (openGFX430 layout):
// [2:0] destination swaps (DST_SWAP_*), [8:5] pixel operation, [11:9] source swaps
#define  SRC_SWAP_NONE             0x0000u
#define  SRC_SWAP_CL               0x0200u
#define  SRC_SWAP_Y                0x0400u
#define  SRC_SWAP_Y_CL             0x0600u
#define  SRC_SWAP_X                0x0800u
#define  SRC_SWAP_X_CL             0x0A00u
#define  SRC_SWAP_X_Y              0x0C00u
#define  SRC_SWAP_X_Y_CL           0x0E00u
#define  GPU_PXOP(op)              ((uint32_t)(op) << 5)
#define  GPU_PXOP_0                GPU_PXOP(BLNS_ROP_S)
#define  GPU_PXOP_1                GPU_PXOP(BLNS_ROP_NOT_S)

//----------------------------------------------------------
// FUNCTIONS
//----------------------------------------------------------

// Timing (ACLINT driver, BOARD_TIME_HZ)
void delay_ms          (uint32_t ms);
void mode_timer_start  (void);
int  mode_timer_expired(void);

// Panel and controller
void init_lcd          (void);
void init_gfx_ctrl     (blns_mode_t gfx_mode, uint32_t refresh_period);
void start_gfx_ctrl    (void);
// Frame pointer n at pixel address addr (blns_set_frame; the software gate
// walks from frame pointer 0)
void frame_ptr         (unsigned n, uint32_t addr);
// openGFX430's FRAME_CNT pacing, on the frame number: once the frames of the
// period before are done (n of 0: none), a period of n frames starts; the
// work between two calls takes its share of the period. Ends at once when no
// frames flow (the refresh stopped).
void frame_pace        (unsigned n);

// GPU (configuration word as above)
void gpu_fill             (uint32_t addr, uint16_t width, uint16_t length, uint16_t color, uint16_t configuration);
void gpu_copy             (uint32_t src_addr, uint32_t dst_addr, uint16_t width, uint16_t length, uint16_t configuration);
void gpu_copy_transparent (uint32_t src_addr, uint32_t dst_addr, uint16_t width, uint16_t length, uint16_t trans_color, uint16_t configuration);
void gpu_wait_done        (void);

// Software gate: the openGFX430 gate-0 window walk at 16 bpp, done by the CPU
// through the VRAM window (the DE0 build has no Belenos gate). cfg holds the
// DST_SWAP_* bits; the walk starts at addr (pixel address in frame 0), runs of
// 'width' pixels, a pixel per gate0_data() / gate0_read().
void     gate0_cfg   (uint16_t cfg);
void     gate0_width (uint16_t width);
void     gate0_addr  (uint32_t addr);
void     gate0_data  (uint16_t color);
uint16_t gate0_read  (void);

#endif

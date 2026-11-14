//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gfx_controller.c  (DE0-Nano-SoC graphic demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Demo support on the Belenos driver library: timing, LT24 bring-up, per-mode
// controller set-up and the openGFX430-style GPU helpers.
//----------------------------------------------------------------------------
#include "gfx_controller.h"
#include "ili9341.h"
#include "ahb_aclint.h"
#include "de0_board.h"

// Panel bus timing (see DIF_CFG.CLK_DIV)
#define LT24_CLK_DIV       1u                  // 8080: 80 ns write cycle

// ILI9341 memory access control: BGR order, portrait (the frame buffer is
// scanned column by column, DISP_CFG.CL_SWAP)
#define LCD_MADCTL         ILI9341_MADCTL_BGR

// Each demo step runs for MODE_TIME_MS before moving to the next one
#define MODE_TIME_MS       6000u

blns_t blns;

static uint32_t refresh_period;
static uint32_t mode_start;
static uint32_t mode_ticks;

//==========================================================
// TIMING
//==========================================================

static aclint_t aclint;

static void timer_init(void)
{
    const aclint_config_t cfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                  .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    aclint_init(&aclint, &cfg);
    mode_ticks = (uint32_t) aclint_ms_to_ticks(&aclint, MODE_TIME_MS);
}

void delay_ms(uint32_t ms)
{
    aclint_delay_ms(&aclint, ms);
}

void mode_timer_start(void)
{
    mode_start = aclint_mtime_lo(&aclint);
}

int mode_timer_expired(void)
{
    return (aclint_mtime_lo(&aclint) - mode_start) >= mode_ticks;
}

static uint16_t pace_t0, pace_n;                             // the period: its first frame, its length

void frame_pace(unsigned n)
{
    while (pace_n && blns_frames_flowing(&blns) && (uint16_t) (blns_frame_num(&blns) - pace_t0) < pace_n)
        ;
    pace_t0 = blns_frame_num(&blns);
    pace_n  = (uint16_t) n;
}

//==========================================================
// PANEL
//==========================================================

void init_lcd(void)
{
    timer_init();

    const blns_config_t cfg = { .reg_base = BLNS_REG_BASE, .vram_base = BLNS_VRAM_BASE, .hclk_hz = CPU_HZ };
    blns_init(&blns, &cfg);

    // The refresh window is set once at init (CASET 0-239, PASET 0-319):
    // HW_WINDOW_EN stays off with CL_SWAP
    const blns_dbi_config_t dbi = { .spi = false, .clk_div = LT24_CLK_DIV, .hw_window = false };
    blns_dbi_config(&blns, &dbi);

    blns_set_size(&blns, SCREEN_WIDTH, SCREEN_HEIGHT);         // pixel count of the clear
    const ili9341_config_t panel = {
        .delay_ms     = delay_ms,
        .madctl       = LCD_MADCTL,
        .win_width    = SCREEN_HEIGHT,                         // portrait window
        .win_height   = SCREEN_WIDTH,
        .clear        = true,
        .clear_rgb888 = 0x000000u
    };
    ili9341_init(&blns, &panel);
}

//==========================================================
// CONTROLLER
//==========================================================

void init_gfx_ctrl(blns_mode_t gfx_mode, uint32_t period)
{
    // Stop the refresh and let the frame in progress finish
    blns_refresh_stop(&blns);
    while (blns_refresh_running(&blns)) ;

    // Video mode; re-enabling the GPU flushes it
    blns_gpu_enable(&blns, false);
    blns_set_mode(&blns, gfx_mode);
    blns_gpu_enable(&blns, true);

    blns_set_size(&blns, SCREEN_WIDTH, SCREEN_HEIGHT);
    blns_set_orientation(&blns, BLNS_SWAP_CL);

    // Clear the frame buffer
    volatile uint32_t *vram = blns_vram32(&blns);
    for (uint32_t i = 0; i < VRAM_WORDS; i++)
        vram[i] = 0;

    refresh_period = period;
}

void start_gfx_ctrl(void)
{
    blns_refresh_period(&blns, refresh_period);
}

//==========================================================
// GPU
//==========================================================

// openGFX430 configuration word: [2:0] destination swaps, [8:5] pixel
// operation, [11:9] source swaps
static blns_rop_t cfg_rop(uint16_t cfg)  { return (blns_rop_t)((cfg >> 5) & 0xFu); }
static uint8_t    cfg_dst(uint16_t cfg)  { return (uint8_t)(cfg & 0x7u); }
static uint8_t    cfg_src(uint16_t cfg)  { return (uint8_t)((cfg >> 9) & 0x7u); }

void gpu_fill(uint32_t addr, uint16_t width, uint16_t length, uint16_t color, uint16_t configuration)
{
    const blns_surf_t dst = { addr, 0, cfg_dst(configuration) };
    blns_gpu_fill(&blns, &dst, width, length, color, cfg_rop(configuration));
}

void gpu_copy(uint32_t src_addr, uint32_t dst_addr, uint16_t width, uint16_t length, uint16_t configuration)
{
    const blns_surf_t src = { src_addr, 0, cfg_src(configuration) };
    const blns_surf_t dst = { dst_addr, 0, cfg_dst(configuration) };
    blns_gpu_copy(&blns, &src, &dst, width, length, cfg_rop(configuration));
}

void gpu_copy_transparent(uint32_t src_addr, uint32_t dst_addr, uint16_t width, uint16_t length, uint16_t trans_color, uint16_t configuration)
{
    const blns_surf_t src = { src_addr, 0, cfg_src(configuration) };
    const blns_surf_t dst = { dst_addr, 0, cfg_dst(configuration) };
    blns_gpu_copy_transp(&blns, &src, &dst, width, length, cfg_rop(configuration), trans_color);
}

void gpu_wait_done(void)
{
    blns_gpu_wait(&blns);
}

//==========================================================
// SOFTWARE GATE
//==========================================================

static uint16_t sg_cfg;
static uint16_t sg_width = 1u;
static uint16_t sg_col;
static uint32_t sg_addr;                                     // current pixel
static uint32_t sg_line;                                     // first pixel of the run

void gate0_cfg(uint16_t cfg)     { sg_cfg = cfg; }
void gate0_width(uint16_t width) { sg_width = width ? width : 1u; sg_col = 0; }

static uint32_t sg_frame0;                                   // frame pointer 0 (frame_ptr)

void frame_ptr(unsigned n, uint32_t addr)
{
    blns_set_frame(&blns, n, addr);
    if (n == 0u)
        sg_frame0 = addr;
}

void gate0_addr(uint32_t addr)
{
    sg_addr = sg_frame0 + addr;
    sg_line = sg_addr;
    sg_col  = 0;
}

static void sg_step(void)
{
    const bool     cl    = (sg_cfg & BLNS_SWAP_CL) != 0u;
    const int32_t  pitch = SCREEN_WIDTH;
    const int32_t  inner = cl ? ((sg_cfg & BLNS_SWAP_Y) ? -pitch : pitch) : ((sg_cfg & BLNS_SWAP_X) ? -1 : 1);
    const int32_t  outer = cl ? ((sg_cfg & BLNS_SWAP_X) ? -1 : 1) : ((sg_cfg & BLNS_SWAP_Y) ? -pitch : pitch);

    if (++sg_col == sg_width) {
        sg_col   = 0;
        sg_line += (uint32_t) outer;
        sg_addr  = sg_line;
    } else
        sg_addr += (uint32_t) inner;
}

void gate0_data(uint16_t color)
{
    blns_vram16(&blns)[sg_addr] = color;
    sg_step();
}

uint16_t gate0_read(void)
{
    uint16_t color = blns_vram16(&blns)[sg_addr];

    sg_step();
    return color;
}

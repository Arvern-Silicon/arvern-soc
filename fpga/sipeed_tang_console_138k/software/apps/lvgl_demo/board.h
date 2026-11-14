/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      board.h (lvgl_demo)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang Console 138K side of the LVGL demo: Belenos at 16 bpp on the RGB
 * panel or HDMI, time, the USB gamepads, the TTY, traps.
 *
 * VRAM (board_blns.caps.vram_words words: 272 KB on this board): the 480 x 272
 * RGB565 frame at 0 (255 KB), shown on the panel 1:1 (turned by half a turn:
 * the panel is mounted upside down) or scaled into the HDMI picture; the rest
 * after it (VRAM_SPARE on: 17 KB here) holds the GPU flush's two draw buffers
 * (the default; free with LVGL_FLUSH_CPU or LVGL_FLUSH_DIRECT) and, in its last
 * 512 bytes, the cursor's shape. A second frame does not fit.
 *----------------------------------------------------------------------------*/
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "belenos.h"

#define FB_W            480u
#define FB_H            272u
#define FB_PIXEL        0u                              /* frame's pixel address  */
#define VRAM_SPARE      ((FB_PIXEL + FB_W * FB_H) * 2u) /* byte address after it  */

#define PAD_UP          0x01u
#define PAD_DOWN        0x02u
#define PAD_LEFT        0x04u
#define PAD_RIGHT       0x08u
#define PAD_A           0x10u
#define PAD_B           0x20u
#define PAD_START       0x40u
#define PAD_SELECT      0x80u

extern blns_t board_blns;

void     board_init(void);              /* Belenos 16 bpp, the display, USB, timer */
bool     board_hdmi(void);              /* the HDMI bitstream                      */
uint32_t board_ticks(void);             /* machine timer, BOARD_MTIME_HZ (real)    */
uint32_t board_ticks_hz(void);
uint32_t board_ms(void);                /* milliseconds (real time, both builds)   */
void     board_poll(void);              /* USB and the HDMI hot plug               */
uint32_t board_pad(void);               /* PAD_* held on any gamepad               */
void     board_leds(uint32_t v);
void     board_printf(const char *fmt, ...);
void     board_stack_paint(void);
uint32_t board_stack_used(void);        /* bytes of stack ever used                */
uint32_t board_stack_size(void);

#endif /* BOARD_H */

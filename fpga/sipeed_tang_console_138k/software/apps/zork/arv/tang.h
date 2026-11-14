/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      tang.h (zork)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang platform layer of the Zork port: what its parts share. The
 * screen is a 1 bit per pixel frame at VRAM 0: 640 x 400 on HDMI (80 x 25
 * characters of 8 x 16), 480 x 272 on the panel (60 x 17).
 *----------------------------------------------------------------------------*/
#ifndef TANG_H
#define TANG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern uint8_t *const arv_vram;
extern unsigned arv_frame_w, arv_frame_h;      /* the frame's size, in pixels */

int      arv_printf(const char *fmt, ...);
void     arv_putc(char c);
void     arv_puts(const char *s);
uint32_t arv_mtime(void);
uint32_t arv_time_hz(void);
void     arv_board_start(void);
void     arv_board_poll(void);                 /* while waiting for a key */
void     arv_halt(void) __attribute__((noreturn));
int      arv_chdir(const char *path);

#define PAD_UP      0x001u
#define PAD_DOWN    0x002u
#define PAD_LEFT    0x004u
#define PAD_RIGHT   0x008u
#define PAD_A       0x010u
#define PAD_B       0x020u
#define PAD_X       0x040u
#define PAD_Y       0x080u
#define PAD_START   0x100u
#define PAD_SELECT  0x200u
extern volatile uint32_t arv_pad_inject;
void     arv_input_poll(void);
uint32_t arv_pad(void);
bool     arv_key_event(uint8_t *usage, uint8_t *mods, bool *down);

/* console.c */
void con_init(void);
void con_write(const char *s, size_t n);
void con_status(const char *text);             /* the top line, inverted; NULL: none */
int  con_read_line(char *buf, int size);       /* keyboard or gamepad; the length */
int  con_menu(const char *const *items, int n); /* the item chosen */
void con_clear(void);

void arv_stack_paint(void);
void arv_mem_report(const char *where);
char *arv_heap_brk(void);
void arv_irq_handler(void (*h)(void));

#endif

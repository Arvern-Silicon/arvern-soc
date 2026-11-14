/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      board.c (lvgl_demo)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang Console 138K side of the LVGL demo (see board.h):
 *   display   Belenos at 16 bpp: the panel 1:1, or HDMI (720 x 480, the
 *             480 x 272 frame scaled to the monitor's shape from its EDID); the
 *             board's display set-up and hot-plug handling (tang_video.h)
 *   time      the ACLINT machine timer; board_ms() counts real milliseconds in
 *             both builds (the drivers' delays run 500 times faster in the
 *             simulation, BOARD_TIME_HZ, but LVGL's animations and the frame
 *             rate readout keep the board's time)
 *   controls  USB gamepads on both USB-A ports
 *   TTY       the board's TTY register (the simulation log)
 *   traps     reported on the TTY ("lvgl_demo: trap ..."), then a halt
 *----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <string.h>
#include "lvgl.h"
#include "tang_board.h"
#include "led_key_sw.h"
#include "ahb_aclint.h"
#include "usb_hid.h"
#include "tang_video.h"
#include "board.h"

_Static_assert(FB_W == BOARD_LCD_W && FB_H == BOARD_LCD_H, "the frame is the panel's size");

blns_t           board_blns;
static aclint_t  aclint;
static lks_t     lks;
static usb_hid_t usb[2];

/*------------------------------- TTY ----------------------------------------*/

void board_printf(const char *fmt, ...)
{
    char    buf[120];
    va_list ap;
    va_start(ap, fmt);
    lv_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (const char *p = buf; *p; p++)
        lks_tty_putc(&lks, *p);
}

void board_leds(uint32_t v) { lks_led_set(&lks, v); }

/*------------------------------- time ---------------------------------------*/

static uint32_t ms_count, ms_tick_q;

uint32_t board_ticks(void)    { return aclint_mtime_lo(&aclint); }
uint32_t board_ticks_hz(void) { return BOARD_MTIME_HZ; }

/* Real milliseconds, carried across the 32-bit timer's wrap (761 s) */
uint32_t board_ms(void)
{
    const uint32_t per = BOARD_MTIME_HZ / 1000u;
    const uint32_t ms  = (aclint_mtime_lo(&aclint) - ms_tick_q) / per;
    ms_tick_q += ms * per;
    ms_count  += ms;
    return ms_count;
}

/*------------------------------- HDMI ---------------------------------------*/

bool board_hdmi(void) { return tang_video_hdmi(); }

/*------------------------------- start --------------------------------------*/

void board_init(void)
{
    const aclint_config_t tcfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    const tang_video_t    vid  = { .w = FB_W, .h = FB_H, .bpp = BLNS_BPP_16, .frame = FB_PIXEL };

    lks_init(&lks, BOARD_LKS_BASE);
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    aclint_init(&aclint, &tcfg);
    ms_tick_q = aclint_mtime_lo(&aclint);
    tang_video_init(&board_blns, &aclint);
    tang_video_config(&vid);
    blns_select_frame(&board_blns, 0);
    {                                           /* a black frame until LVGL's first one */
        const blns_surf_t fb = { FB_PIXEL, 0, BLNS_SWAP_NONE };
        blns_gpu_fill(&board_blns, &fb, FB_W, FB_H, 0u, BLNS_ROP_S);
        blns_gpu_wait(&board_blns);
    }
    tang_video_picture();                       /* on HDMI: the frame at its own shape on the monitor */
    tang_video_start();
}

/*------------------------------- controls -----------------------------------*/

void board_poll(void)
{
    for (int i = 0; i < 2; i++)
        (void) usb_hid_service(&usb[i]);
    (void) tang_video_watch(&lks);              /* hot plug, KEY[1] restarts the link */
}

uint32_t board_pad(void)
{
    static const struct { uint32_t usb, pad; } map[] = {
        { USB_BTN_UP, PAD_UP }, { USB_BTN_DOWN, PAD_DOWN }, { USB_BTN_LEFT, PAD_LEFT },
        { USB_BTN_RIGHT, PAD_RIGHT }, { USB_BTN_A, PAD_A }, { USB_BTN_B, PAD_B },
        { USB_BTN_START, PAD_START }, { USB_BTN_SELECT, PAD_SELECT },
    };
    uint32_t b = 0, p = 0;
    for (int i = 0; i < 2; i++)
        if (usb_hid_type(&usb[i]) == USB_HID_GAMEPAD)
            b |= usb_hid_buttons(&usb[i]);
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (b & map[i].usb)
            p |= map[i].pad;
    return p;
}

/*------------------------------- stack --------------------------------------*/

extern uint32_t _stack_start[], _stack_end[];
#define STACK_PAINT 0x5AA5C33Cu

/* Paint the stack below the current frame (called first thing in main) */
__attribute__((noinline)) void board_stack_paint(void)
{
    volatile uint32_t *p = _stack_start;
    uint32_t *sp = (uint32_t *) __builtin_frame_address(0) - 64;
    while ((uint32_t *) p < sp)
        *p++ = STACK_PAINT;
}

uint32_t board_stack_used(void)
{
    const uint32_t *p = _stack_start;
    while (p < _stack_end && *p == STACK_PAINT)
        p++;
    return (uint32_t) ((uintptr_t) _stack_end - (uintptr_t) p);
}

uint32_t board_stack_size(void) { return (uint32_t) ((uintptr_t) _stack_end - (uintptr_t) _stack_start); }

/*------------------------------- traps --------------------------------------*/

/* Every trap is fatal (no interrupts are enabled; the core does not split
 * misaligned accesses): reported on the TTY, then a halt */
uint32_t arv_trap(uint32_t *x, uint32_t cause, uint32_t pc, uint32_t val)
{
    (void) x;
    board_leds(0xEEu);
    board_printf("lvgl_demo: trap cause %u at pc %x (value %x)\n", (unsigned) cause, (unsigned) pc,
                 (unsigned) val);
    for (;;)
        ;
}

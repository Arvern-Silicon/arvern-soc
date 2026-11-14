/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      board.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang Console 138K side of the Prince of Persia port:
 *   display   Belenos at 8 bpp, the 320 x 200 frames (tang.h) scaled to the
 *             panel or the HDMI picture (16:10, square pixels), the colours
 *             through the colour table; the board's display set-up and
 *             HDMI hot-plug handling (tang_video.h)
 *   time      the ACLINT machine timer
 *   controls  USB gamepads and keyboards on both USB-A ports
 *   TTY       the board's TTY register (the simulation log, the debugger)
 *   card      FatFs on the microSD slot; the program's folder /APPS/PRINCE
 *             is its current directory
 *----------------------------------------------------------------------------*/
#include <stdint.h>
#include <string.h>
#include "ff.h"
#include "tang_board.h"
#include "led_key_sw.h"
#include "belenos.h"
#include "ahb_aclint.h"
#include "usb_hid.h"
#include "tang_video.h"
#include "tang.h"

uint8_t *const arv_vram = (uint8_t *) BOARD_VRAM_BASE;
blns_t   arv_blns;
volatile uint32_t arv_pad_inject;

static aclint_t  aclint;
static lks_t     lks;
static usb_hid_t usb[2];
static FATFS     fs;

void arv_putc(char c)          { lks_tty_putc(&lks, c); }
void arv_puts(const char *s)   { while (*s) arv_putc(*s++); }
uint32_t arv_mtime(void)       { return aclint_mtime_lo(&aclint); }
uint32_t arv_time_hz(void)     { return BOARD_TIME_HZ; }

int arv_chdir(const char *path)
{
    if (f_mount(&fs, "", 1) != FR_OK)
        return -1;
    return f_chdir(path) == FR_OK ? 0 : -1;
}

/*------------------------------- HDMI ---------------------------------------*/

#ifdef SIMULATION
volatile uint32_t trace_mark;                   /* the bench's frame marker (-frames A:B) */
#endif

/* Between frames: the HDMI hot plug; in the simulation build every 50th
 * frame and each level on the TTY */
void arv_board_poll(void)
{
#ifdef SIMULATION
    extern uint16_t current_level;
    static uint32_t presents;
    static int level_q = -2;
    trace_mark = presents + 1u;
    if ((int) (int16_t) current_level != level_q)
        arv_printf("prince: level %d at present %u\n", (int) (int16_t) current_level, (unsigned) presents);
    level_q = (int16_t) current_level;
    if (++presents % 50u == 0u)
        arv_printf("prince: present %u at %u ms\n", (unsigned) presents,
                   (unsigned) (aclint_mtime_lo(&aclint) / (BOARD_MTIME_HZ / 1000u)));
#endif
    (void) tang_video_watch(&lks);              /* hot plug, KEY[1] restarts the link; a waiting palette */
}

void arv_halt(void)
{
    for (;;)
        ;
}

/*------------------------------- start --------------------------------------*/

void arv_board_start(void)
{
    const aclint_config_t tcfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    const tang_video_t    vid  = { .w = FRAME_W, .h = FRAME_H, .bpp = BLNS_BPP_8, .frame = VRAM_FRAME };

    lks_init(&lks, BOARD_LKS_BASE);
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    aclint_init(&aclint, &tcfg);
    tang_video_init(&arv_blns, &aclint);
    memset(arv_vram + VRAM_FRAME, 0, FRAME_BYTES);
#ifndef ARV_PRESENT_COPY
    memset(arv_vram + VRAM_FRAME1, 0, FRAME_BYTES);
#endif
    tang_video_config(&vid);
#ifndef ARV_PRESENT_COPY
    blns_set_frame(&arv_blns, 1, VRAM_FRAME1);    /* shown in turn with frame 0 (psdl_tang.c) */
#endif
    blns_lut_select(&arv_blns, true, 0);
    tang_video_picture();
    tang_video_start();
#ifdef SIMULATION
#ifdef ARV_PRESENT_COPY
    arv_puts("prince: display: copies into one displayed frame (ARV_PRESENT_COPY)\n");
#else
    arv_puts("prince: display: flips between two displayed frames\n");
#endif
#endif
}

/* Tear-free: the whole table into the colour table bank not shown, selected
 * at a frame start (tang_video_palette; a second change of the same frame
 * goes at the next poll, with the next present) */
void arv_palette_set(unsigned first, unsigned n, const uint32_t *rgb)
{
    static uint32_t shown[256];
    if (first >= 256u || n > 256u - first)
        return;
    memcpy(&shown[first], rgb, n * sizeof(*rgb));
    tang_video_palette(shown, 256u);
}

/*------------------------------- controls -----------------------------------*/

void arv_input_poll(void)
{
    for (int i = 0; i < 2; i++)
        (void) usb_hid_service(&usb[i]);
}

uint32_t arv_pad(void)
{
    static const struct { uint32_t usb, pad; } map[] = {
        { USB_BTN_UP, PAD_UP }, { USB_BTN_DOWN, PAD_DOWN }, { USB_BTN_LEFT, PAD_LEFT },
        { USB_BTN_RIGHT, PAD_RIGHT }, { USB_BTN_A, PAD_A }, { USB_BTN_B, PAD_B },
        { USB_BTN_X, PAD_X }, { USB_BTN_Y, PAD_Y }, { USB_BTN_START, PAD_START },
        { USB_BTN_SELECT, PAD_SELECT },
    };
    uint32_t b = 0, p = arv_pad_inject;
    for (int i = 0; i < 2; i++)
        if (usb_hid_type(&usb[i]) == USB_HID_GAMEPAD)
            b |= usb_hid_buttons(&usb[i]);
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (b & map[i].usb)
            p |= map[i].pad;
    return p;
}

bool arv_key_event(uint8_t *usage, uint8_t *mods, bool *down)
{
    usb_hid_key_t ev;
    for (int i = 0; i < 2; i++)
        if (usb_hid_type(&usb[i]) == USB_HID_KEYBOARD && usb_hid_key_event(&usb[i], &ev)) {
            *usage = ev.key;
            *mods = ev.mods;
            *down = ev.down;
            return true;
        }
    return false;
}

uint8_t arv_key_mods(void)
{
    uint8_t m = 0;
    for (int i = 0; i < 2; i++)
        if (usb_hid_type(&usb[i]) == USB_HID_KEYBOARD)
            m |= (uint8_t) usb_hid_modifiers(&usb[i]);
    return m;
}

/*------------------------------- entry --------------------------------------*/

#define APP_DIR "/APPS/PRINCE"

extern int g_argc;
extern char **g_argv;
void pop_main(void);

int main(void)
{
    static char *argv[] = { "prince", 0 };
    arv_stack_paint();
    arv_board_start();
    arv_puts("prince: start\n");
    if (arv_chdir(APP_DIR) != 0) {
        arv_puts("prince: no " APP_DIR " on the card\n");
        for (;;)
            ;
    }
    g_argc = 1;
    g_argv = argv;
    pop_main();
    return 0;
}

/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      board.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang Console 138K side of the Keen 4 port:
 *   display   Belenos at 4 bpp, the 320 x 200 frame at VRAM 0 scaled to the
 *             panel or the HDMI picture (16:10, square pixels), the colours
 *             through the colour table; the board's display set-up and
 *             HDMI hot-plug handling (tang_video.h)
 *   time      the ACLINT machine timer
 *   controls  USB gamepads and keyboards on both USB-A ports
 *   TTY       the board's TTY register (the simulation log, the debugger)
 *   card      FatFs on the microSD slot; the program's folder /APPS/KEEN4
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

/* Between frames: the HDMI hot plug; in the simulation build every tenth
 * frame on the TTY, with the machine time */
void arv_board_poll(void)
{
#ifdef SIMULATION
    static uint32_t presents;

    if (++presents % 10u == 0u)
        arv_printf("keen4: present %u at %u ms\n", (unsigned) presents,
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
    const tang_video_t    vid  = { .w = FRAME_W, .h = FRAME_H, .bpp = BLNS_BPP_4, .frame = VRAM_FRAME * 2u };

    lks_init(&lks, BOARD_LKS_BASE);
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    aclint_init(&aclint, &tcfg);
    tang_video_init(&arv_blns, &aclint);
    memset(arv_vram, 0, FRAME_W * FRAME_H / 2u);
    tang_video_config(&vid);
    blns_lut_select(&arv_blns, true, 0);
    tang_video_picture();
    tang_video_start();
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

#define APP_DIR "/APPS/KEEN4"

int keen_main(int argc, char *argv[]);

int main(void)
{
    static char *argv[] = { "keen4", 0 };
    arv_stack_paint();
    arv_board_start();
    arv_puts("keen4: start\n");
    if (arv_chdir(APP_DIR) != 0) {
        arv_puts("keen4: no " APP_DIR " on the card\n");
        for (;;)
            ;
    }
    return keen_main((int) (sizeof(argv) / sizeof(argv[0])) - 1, argv);
}

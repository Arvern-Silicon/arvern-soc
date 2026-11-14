/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      in_tang.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Omnispeak's input back-end (IN_Backend): the USB gamepads and keyboards of
 * both USB-A ports, seen as the PC keyboard the game reads by default.
 *
 *   gamepad   d-pad: the arrows; A: Ctrl (jump, and select in the menus);
 *             B: Alt (pogo); X, Y: Space (fire); Start: Esc (the menu);
 *             Select: Enter (the status window)
 *   keyboard  its keys as the PC's (scan codes from the HID usages)
 *----------------------------------------------------------------------------*/
#include "id_in.h"
#include "tang.h"

/* PC scan codes of HID usages 0x00-0x52 (0: not used by the game) */
static const uint8_t hid_sc[0x53] = {
    0, 0, 0, 0,
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,    /* a-l */
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,    /* m-x */
    0x15, 0x2C,                                                                /* y z */
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,                /* 1-0 */
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0, 0x27, 0x28,   /* Enter .. ' */
    0x29, 0x33, 0x34, 0x35, 0x3A,                                              /* ` , . / Caps */
    0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,    /* F1-F12 */
    0, 0, 0x45, 0x52, 0x47, 0x49, 0x53, 0x4F, 0x51,                            /* .. Ins Home PgUp Del End PgDn */
    0x4D, 0x4B, 0x50, 0x48,                                                    /* Right Left Down Up */
};

static const struct { uint32_t bit; uint8_t sc; } pad_keys[] = {
    { PAD_UP, IN_SC_UpArrow }, { PAD_DOWN, IN_SC_DownArrow },
    { PAD_LEFT, IN_SC_LeftArrow }, { PAD_RIGHT, IN_SC_RightArrow },
    { PAD_A, IN_SC_Control }, { PAD_B, IN_SC_Alt }, { PAD_X | PAD_Y, IN_SC_Space },
    { PAD_START, IN_SC_Escape }, { PAD_SELECT, IN_SC_Enter },
};

static uint32_t pad_q;
static uint8_t  mods_q;

static void key(uint8_t sc, bool down)
{
    if (!sc)
        return;
    if (down)
        IN_HandleKeyDown((IN_ScanCode) sc, false);
    else
        IN_HandleKeyUp((IN_ScanCode) sc, false);
}

static void TANG_PumpEvents(void)
{
    static const uint8_t mod_sc[8] = { 0x1D, 0x2A, 0x38, 0, 0x1D, 0x36, 0x38, 0 };
    uint32_t pad, chg;
    uint8_t usage, mods;
    bool down;
    SD_Tang_Poll();
    arv_input_poll();
    pad = arv_pad();
    chg = pad ^ pad_q;
    for (unsigned i = 0; i < sizeof(pad_keys) / sizeof(pad_keys[0]); i++)
        if (chg & pad_keys[i].bit)
            key(pad_keys[i].sc, (pad & pad_keys[i].bit) != 0);
    pad_q = pad;
    while (arv_key_event(&usage, &mods, &down))
        if (usage < sizeof(hid_sc))
            key(hid_sc[usage], down);
    mods = arv_key_mods();
    for (unsigned i = 0; i < 8; i++)
        if ((mods ^ mods_q) & (1u << i))
            key(mod_sc[i], (mods >> i) & 1u);
    mods_q = mods;
}

static void TANG_WaitKey(void) { TANG_PumpEvents(); }
static void TANG_Startup(bool disableJoysticks) { (void) disableJoysticks; }
static bool TANG_JoyStart(int joystick) { (void) joystick; return false; }
static void TANG_JoyStop(int joystick) { (void) joystick; }
static bool TANG_JoyPresent(int joystick) { (void) joystick; return false; }
static void TANG_JoyGetAbs(int joystick, int *x, int *y) { (void) joystick; if (x) *x = 0; if (y) *y = 0; }
static uint16_t TANG_JoyGetButtons(int joystick) { (void) joystick; return 0; }
static const char *TANG_JoyGetName(int joystick) { (void) joystick; return 0; }

static IN_Backend in_tang_backend = {
    .startup = TANG_Startup,
    .shutdown = 0,
    .pumpEvents = TANG_PumpEvents,
    .waitKey = TANG_WaitKey,
    .joyStart = TANG_JoyStart,
    .joyStop = TANG_JoyStop,
    .joyPresent = TANG_JoyPresent,
    .joyGetAbs = TANG_JoyGetAbs,
    .joyGetButtons = TANG_JoyGetButtons,
    .joyGetName = TANG_JoyGetName,
    .joyGetButtonName = 0,
    .startTextInput = 0,
    .stopTextInput = 0,
    .joyAxisMin = -1000,
    .joyAxisMax = 1000,
    .supportsTextEvents = false,
};

IN_Backend *IN_Impl_GetBackend(void)
{
    return &in_tang_backend;
}

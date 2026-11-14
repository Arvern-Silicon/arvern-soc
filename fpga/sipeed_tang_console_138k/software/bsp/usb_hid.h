/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      usb_hid.h
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The USB-A ports of the Tang Console 138K (apb_usb_host, one per port: J15
 * at BOARD_USB0_BASE, J16 at BOARD_USB1_BASE, PLIC sources
 * BOARD_PLIC_SRC_USB0 / _USB1): a low-speed gamepad, keyboard or mouse,
 * enumerated by the IP and decoded by its HID helper (arvern-ips
 * apb_usb_host/sw: src/apb_usb_host.c and hid/apb_usb_host_hid.c, which the
 * application builds with).
 *
 * usb_hid_init binds a port and enables it (a port already enabled, by a
 * previous program, keeps its device). Reports reach the decoder through
 * usb_hid_service, called either from the port's interrupt handler (after
 * usb_hid_irq_enable) or from the application's polling loop (once a frame
 * is enough for the gamepad and the keys held; key events and mouse movement
 * collected between two calls are the ones of the latest report only, so an
 * application that needs every keystroke uses the interrupt).
 *----------------------------------------------------------------------------*/
#ifndef USB_HID_H
#define USB_HID_H

#include <stdbool.h>
#include <stdint.h>
#include "apb_usb_host.h"
#include "apb_usb_host_hid.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_HID_NONE     = 0,              /* no device in RUN                         */
    USB_HID_KEYBOARD = 1,              /* boot keyboard                            */
    USB_HID_MOUSE    = 2,              /* boot mouse                               */
    USB_HID_GAMEPAD  = 3               /* any other HID device (gamepad layouts)   */
} usb_hid_type_t;

/* Gamepad buttons */
#define USB_BTN_LEFT        APB_USB_HOST_HID_PAD_LEFT
#define USB_BTN_RIGHT       APB_USB_HOST_HID_PAD_RIGHT
#define USB_BTN_UP          APB_USB_HOST_HID_PAD_UP
#define USB_BTN_DOWN        APB_USB_HOST_HID_PAD_DOWN
#define USB_BTN_A           APB_USB_HOST_HID_PAD_A
#define USB_BTN_B           APB_USB_HOST_HID_PAD_B
#define USB_BTN_X           APB_USB_HOST_HID_PAD_X
#define USB_BTN_Y           APB_USB_HOST_HID_PAD_Y
#define USB_BTN_SELECT      APB_USB_HOST_HID_PAD_SELECT
#define USB_BTN_START       APB_USB_HOST_HID_PAD_START
#define USB_BTN_L           APB_USB_HOST_HID_PAD_L
#define USB_BTN_R           APB_USB_HOST_HID_PAD_R

/* Keyboard modifiers */
#define USB_KEY_LCTRL       APB_USB_HOST_HID_MOD_LCTRL
#define USB_KEY_LSHIFT      APB_USB_HOST_HID_MOD_LSHIFT
#define USB_KEY_LALT        APB_USB_HOST_HID_MOD_LALT
#define USB_KEY_LGUI        APB_USB_HOST_HID_MOD_LGUI
#define USB_KEY_RCTRL       APB_USB_HOST_HID_MOD_RCTRL
#define USB_KEY_RSHIFT      APB_USB_HOST_HID_MOD_RSHIFT
#define USB_KEY_RALT        APB_USB_HOST_HID_MOD_RALT
#define USB_KEY_RGUI        APB_USB_HOST_HID_MOD_RGUI

/* Mouse buttons */
#define USB_MOUSE_LEFT      APB_USB_HOST_HID_MOUSE_LEFT
#define USB_MOUSE_RIGHT     APB_USB_HOST_HID_MOUSE_RIGHT
#define USB_MOUSE_MIDDLE    APB_USB_HOST_HID_MOUSE_MIDDLE

#define USB_HID_KEYS        APB_USB_HOST_HID_KEYS       /* key slots of usb_hid_keys */

typedef apb_usb_host_hid_key_t usb_hid_key_t;          /* key, mods, down */

typedef struct {
    apb_usb_host_t     host;
    apb_usb_host_hid_t hid;
} usb_hid_t;

/* Bind the port at base and enable it; -1 when no apb_usb_host answers there */
static inline int usb_hid_init(usb_hid_t *dev, uintptr_t base)
{
    const apb_usb_host_config_t cfg = { .base = base };
    if (apb_usb_host_init(&dev->host, &cfg) != APB_USB_HOST_OK) {
        apb_usb_host_hid_init(&dev->hid, 0);
        return -1;
    }
    apb_usb_host_hid_init(&dev->hid, &dev->host);
    apb_usb_host_irq_disable(&dev->host, APB_USB_HOST_IRQ_ALL);
    if (!apb_usb_host_enabled(&dev->host))
        apb_usb_host_enable(&dev->host, APB_USB_HOST_OPT_DEFAULT);
    return 0;
}

/* The decoder's entry (interrupt handler or polling loop): the IRQ_PEND
 * value found (APB_USB_HOST_IRQ_*) */
static inline uint32_t usb_hid_service(usb_hid_t *dev) { return apb_usb_host_hid_service(&dev->hid); }

/* The port's interrupt: the causes the decoder consumes */
static inline void usb_hid_irq_enable(usb_hid_t *dev)
{
    if (dev->hid.host)
        apb_usb_host_irq_enable(&dev->host, APB_USB_HOST_HID_IRQ_MASK);
}
static inline void usb_hid_irq_disable(usb_hid_t *dev)
{
    if (dev->hid.host)
        apb_usb_host_irq_disable(&dev->host, APB_USB_HOST_IRQ_ALL);
}

static inline usb_hid_type_t usb_hid_type(const usb_hid_t *dev)
{
    switch (apb_usb_host_hid_kind(&dev->hid)) {
    case APB_USB_HOST_HID_KEYBOARD: return USB_HID_KEYBOARD;
    case APB_USB_HOST_HID_MOUSE:    return USB_HID_MOUSE;
    case APB_USB_HOST_HID_GAMEPAD:
    case APB_USB_HOST_HID_UNKNOWN:  return USB_HID_GAMEPAD;
    default:                        return USB_HID_NONE;
    }
}

/* Gamepad: USB_BTN_* of the latest report */
static inline uint32_t usb_hid_buttons(const usb_hid_t *dev) { return apb_usb_host_hid_pad_buttons(&dev->hid); }

/* Keyboard: the modifiers and the keys held (0 = none) in keys[0..5]; the
 * press / release events in order */
static inline uint32_t usb_hid_modifiers(const usb_hid_t *dev)
{
    uint8_t m;
    apb_usb_host_hid_kbd_state(&dev->hid, &m, 0);
    return m;
}
static inline void     usb_hid_keys(const usb_hid_t *dev, uint8_t keys[USB_HID_KEYS])
{ apb_usb_host_hid_kbd_state(&dev->hid, 0, keys); }
static inline bool     usb_hid_key_event(usb_hid_t *dev, usb_hid_key_t *ev)
{ return apb_usb_host_hid_key_pop(&dev->hid, ev); }

/* Mouse: the buttons, and the movement since the previous call (X and Y
 * together, each saturated to 16 bits) */
static inline uint32_t usb_hid_mouse_buttons(const usb_hid_t *dev) { return apb_usb_host_hid_mouse_buttons(&dev->hid); }
static inline void     usb_hid_mouse_move(usb_hid_t *dev, int16_t *dx, int16_t *dy)
{
    int32_t x, y;
    apb_usb_host_hid_mouse_take(&dev->hid, &x, &y, 0);
    *dx = (int16_t) (x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    *dy = (int16_t) (y > 32767 ? 32767 : y < -32768 ? -32768 : y);
}

#ifdef __cplusplus
}
#endif

#endif /* USB_HID_H */

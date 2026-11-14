//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K USB host test)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// tst_usb_hid -- the two USB host ports (apb_usb_host): USB-A J15 (PLIC
// source 29) and J16 (PLIC source 30), reports decoded by the IP's HID
// helper from the ports' interrupts.
//
// Simulation (-DSIMULATION): the bench's device models play a boot keyboard
// on J15 and the SNES gamepad (0079:0011) on J16 (src/tst_usb_hid.v); the
// LED register paces them:
//   0x10  both ports enabled, interrupts on: attach the devices
//   0x11  both READY, DEV_ID 0x00011209 (J15) / 0x00110079 (J16), J15
//         DEV_IF.BOOT: keyboard report left shift + "a", pad report right + A
//   0x12  one key press event "a" with left shift; the pad shows RIGHT and A
//         only: all keys released, pad idle
//   0x13  the key release event; the pad idle: detach the keyboard
//   0xA5  DETACH on J15, J15 DETACHED, J16 still in RUN: pass
// 0xE0 | code on the first failure of a J15 check, 0xD0 | code for J16
// (codes below); the 'tst' block holds the ports' STATUS and the IRQ_PEND
// causes seen, for a debugger.
//
// Board: LED[0] lit while a device is in RUN on J15, LED[1] on J16; LED
// register bits [7:2] count the reports decoded on both ports.
//----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "usb_hid.h"
#include "ahb_plic.h"
#include "ahb_aclint.h"
#include "arvern_irq.h"

#define LED_PASS       0xA5u

enum {
    FAIL_INIT = 1,         // driver bind, CTRL.EN / IRQ_EN read-back, or PLIC set-up
    FAIL_ENUM,             // no READY in time, or IRQ_PEND.FAIL
    FAIL_DEVICE,           // DEV_ID, DEV_IF.BOOT or the decoded kind
    FAIL_PRESS,            // key press event or pad state
    FAIL_RELEASE,          // key release event or idle pad
    FAIL_DETACH,           // no DETACH, J15 not DETACHED, or J16 not in RUN
    FAIL_UNEXPECTED        // a source outside the test was claimed
};

static lks_t     lks;
static aclint_t  aclint;
static usb_hid_t usb[2];
static plic_t    plic;
static unsigned  ctx;

// Debugger view (magic 'USBT')
volatile struct {
    uint32_t magic;
    uint32_t step;                          // the LED value of the step
    uint32_t pend[2];                       // IRQ_PEND causes seen per port
    uint32_t status[2];                     // STATUS at the last cause (or at a FAIL)
    uint32_t unexpected;
} tst = { 0x54425355u, 0u, { 0u, 0u }, { 0u, 0u }, 0u };

//-----------------------------------------------------------------------------
// Interrupt handling
//-----------------------------------------------------------------------------
static void on_source(uint32_t src, void *arg)
{
    (void)arg;
    if ((src == BOARD_PLIC_SRC_USB0) || (src == BOARD_PLIC_SRC_USB1)) {
        unsigned p   = (src == BOARD_PLIC_SRC_USB1) ? 1u : 0u;
        uint32_t evt = usb_hid_service(&usb[p]);
        tst.pend[p]   = tst.pend[p] | evt;
        tst.status[p] = apb_usb_host_status(&usb[p].host);
    } else {
        tst.unexpected = tst.unexpected + 1u;
        (void)plic_disable(&plic, ctx, src);
    }
}

static void on_external(uint32_t cause)
{
    (void)cause;
    while (plic_dispatch(&plic, ctx, on_source, 0) > 0)
        ;
}

static void fail(unsigned port, unsigned code)
{
    arv_irq_global_disable();
    lks_led_set(&lks, (port ? 0xD0u : 0xE0u) | code);
    for (;;)
        arv_wfi();
}

static void setup(void)
{
    const plic_config_t   pcfg = { .base        = BOARD_PLIC_BASE,
                                   .num_sources = BOARD_PLIC_SOURCES,
                                   .num_harts   = BOARD_PLIC_HARTS,
                                   .prio_bits   = BOARD_PLIC_PRIO_BITS,
                                   .su_mode     = BOARD_PLIC_SU_MODE };
    const aclint_config_t tcfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true,  .mtime_hz = BOARD_MTIME_HZ };

    lks_init(&lks, BOARD_LKS_BASE);
    lks_led_set(&lks, 0x00u);
    if (aclint_init(&aclint, &tcfg) != ACLINT_OK)                 fail(0u, FAIL_INIT);
    if (plic_init(&plic, &pcfg) != PLIC_OK ||
        plic_context(&plic, 0u, false, &ctx) != PLIC_OK ||
        plic_set_priority(&plic, BOARD_PLIC_SRC_USB0, 1u) != PLIC_OK ||
        plic_set_priority(&plic, BOARD_PLIC_SRC_USB1, 1u) != PLIC_OK ||
        plic_enable(&plic, ctx, BOARD_PLIC_SRC_USB0) != PLIC_OK ||
        plic_enable(&plic, ctx, BOARD_PLIC_SRC_USB1) != PLIC_OK ||
        plic_set_threshold(&plic, ctx, 0u) != PLIC_OK)
        fail(0u, FAIL_INIT);

    for (unsigned p = 0u; p < 2u; p++) {
        if (usb_hid_init(&usb[p], p ? BOARD_USB1_BASE : BOARD_USB0_BASE) != 0) fail(p, FAIL_INIT);
        usb_hid_irq_enable(&usb[p]);
        if (!apb_usb_host_enabled(&usb[p].host) ||
            usb[p].host.regs->IRQ_EN != APB_USB_HOST_HID_IRQ_MASK)  fail(p, FAIL_INIT);
    }

    arv_irq_attach(IRQ_M_EXT, on_external);
    arv_irq_enable(IRQ_M_EXT);
    arv_irq_global_enable();
}

#ifdef SIMULATION
//-----------------------------------------------------------------------------
// Simulation: the bench's device models
//-----------------------------------------------------------------------------
// Bounds in bus time: MTIME ticks at BOARD_MTIME_HZ in simulation too (the
// firmware delays of BOARD_TIME_HZ are the scaled ones), and the USB timing
// is real. One enumeration is about 0.26 s plus the 100 ms debounce.
#define MS(n)          ((n) * (BOARD_MTIME_HZ / 1000u))
#define ENUM_TIMEOUT   MS(1500u)
#define STEP_TIMEOUT   MS(500u)

static uint32_t t_step;

static void step(uint32_t led)
{
    tst.step = led;
    t_step   = aclint_mtime_lo(&aclint);
    lks_led_set(&lks, led);
}

static bool expired(uint32_t ticks) { return (uint32_t) (aclint_mtime_lo(&aclint) - t_step) >= ticks; }

static void check_no_fail(void)
{
    for (unsigned p = 0u; p < 2u; p++)
        if (tst.pend[p] & APB_USB_HOST_IRQ_FAIL)
            fail(p, FAIL_ENUM);
}

static void run(void)
{
    usb_hid_key_t ev;
    const uint32_t right_a = USB_BTN_RIGHT | USB_BTN_A;

    // Both ports enabled: the devices are attached
    step(0x10u);
    for (;;) {
        check_no_fail();
        bool r0 = (tst.pend[0] & APB_USB_HOST_IRQ_READY) && usb_hid_type(&usb[0]) != USB_HID_NONE;
        bool r1 = (tst.pend[1] & APB_USB_HOST_IRQ_READY) && usb_hid_type(&usb[1]) != USB_HID_NONE;
        if (r0 && r1)
            break;
        if (expired(ENUM_TIMEOUT))
            fail(r0 ? 1u : 0u, FAIL_ENUM);
    }
    if (usb[0].host.regs->DEV_ID != 0x00011209u ||
        !(usb[0].host.regs->DEV_IF & APB_USB_HOST_DEV_IF_BOOT_Msk) ||
        apb_usb_host_hid_kind(&usb[0].hid) != APB_USB_HOST_HID_KEYBOARD) fail(0u, FAIL_DEVICE);
    if (usb[1].host.regs->DEV_ID != 0x00110079u ||
        apb_usb_host_hid_kind(&usb[1].hid) != APB_USB_HOST_HID_GAMEPAD)  fail(1u, FAIL_DEVICE);

    // Left shift + "a" on J15, right + A on J16
    step(0x11u);
    bool pressed = false;
    while (!pressed || usb_hid_buttons(&usb[1]) != right_a) {
        check_no_fail();
        if (!pressed && usb_hid_key_event(&usb[0], &ev)) {
            if (ev.key != 0x04u || ev.mods != USB_KEY_LSHIFT || !ev.down) fail(0u, FAIL_PRESS);
            pressed = true;
        }
        if (expired(STEP_TIMEOUT))
            fail(pressed ? 1u : 0u, FAIL_PRESS);
    }
    if (usb_hid_key_event(&usb[0], &ev))                            fail(0u, FAIL_PRESS);
    if (usb_hid_modifiers(&usb[0]) != USB_KEY_LSHIFT)               fail(0u, FAIL_PRESS);

    // Keys released, pad idle
    step(0x12u);
    bool released = false;
    while (!released || usb_hid_buttons(&usb[1]) != 0u) {
        check_no_fail();
        if (!released && usb_hid_key_event(&usb[0], &ev)) {
            if (ev.key != 0x04u || ev.down)                         fail(0u, FAIL_RELEASE);
            released = true;
        }
        if (usb_hid_buttons(&usb[1]) & ~right_a)                    fail(1u, FAIL_RELEASE);
        if (expired(STEP_TIMEOUT))
            fail(released ? 1u : 0u, FAIL_RELEASE);
    }
    if (usb_hid_key_event(&usb[0], &ev))                            fail(0u, FAIL_RELEASE);

    // The keyboard is removed
    step(0x13u);
    while (!(tst.pend[0] & APB_USB_HOST_IRQ_DETACH) ||
           apb_usb_host_state(&usb[0].host) != APB_USB_HOST_STATE_DETACHED) {
        if (expired(STEP_TIMEOUT))                                  fail(0u, FAIL_DETACH);
    }
    if (usb_hid_type(&usb[0]) != USB_HID_NONE)                      fail(0u, FAIL_DETACH);
    if (!apb_usb_host_running(&usb[1].host) ||
        usb_hid_type(&usb[1]) != USB_HID_GAMEPAD ||
        (tst.pend[1] & (APB_USB_HOST_IRQ_DETACH | APB_USB_HOST_IRQ_FAIL))) fail(1u, FAIL_DETACH);
    if (tst.unexpected != 0u)                                       fail(0u, FAIL_UNEXPECTED);
    lks_led_set(&lks, LED_PASS);
    for (;;)
        arv_wfi();
}

#else
//-----------------------------------------------------------------------------
// Board: which ports have a device, and the report count
//-----------------------------------------------------------------------------
static void run(void)
{
    for (;;) {
        uint32_t n    = usb[0].hid.reports + usb[1].hid.reports;
        uint32_t leds = ((n & 0x3Fu) << 2) |
                        ((usb_hid_type(&usb[1]) != USB_HID_NONE) ? 2u : 0u) |
                        ((usb_hid_type(&usb[0]) != USB_HID_NONE) ? 1u : 0u);
        lks_led_set(&lks, leds);
    }
}
#endif

int main(void)
{
    arv_irq_init();
    setup();
    run();
    return 0;
}

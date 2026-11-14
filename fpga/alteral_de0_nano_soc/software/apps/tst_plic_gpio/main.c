//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (DE0-Nano-SoC PLIC / GPIO test)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// PLIC and GPIO test. GPIO pins 0-7 (GPIO_1[7:0]) are inputs with
// rising-edge interrupts, pins 8-15 outputs. Every PLIC source has a non-zero
// priority and is enabled on context 0 (mip.MEIP), threshold 0. The M-mode
// external interrupt handler claims through plic_dispatch() until nothing is
// pending; a GPIO source has its IRQ_PEND bit cleared before the completion.
//
// After each GPIO interrupt, pins 15:8 show {count[3:0], pin[3:0]}: the
// number of GPIO interrupts taken so far and the pin just serviced.
//
// PHASES (the LEDs show 0x10 | phase while the test waits for the inputs)
//   1  count      one rising edge on each of pins 0..7: one interrupt each
//   2  priority   pin 5 (priority 6) and pin 2 (priority 2) both pending with
//                 MEIE masked; after unmasking pin 5 must be claimed first
//   3  threshold  pin 1 at priority 2, threshold 3: the edge pends in the PLIC
//                 but does not interrupt (LEDs 0x14 once that is checked);
//                 threshold 0 then lets it through
//   5  KEY        KEY[1] press through PLIC source 27
//   6  Arduino    the Arduino GPIO drives each of its 16 pins high in turn;
//                 its input path sees the pad, so each pin raises a rising
//                 edge through PLIC source 32+n (no external stimulus)
//
// VERDICT on the LEDs: 0xA5 pass, 0xE0 | step on the first failure.
//----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "de0_board.h"
#include "led_key_sw.h"
#include "ahb_plic.h"
#include "apb_gpio.h"
#include "arvern_irq.h"

// Pins
#define IN_PINS        0x00FFu
#define OUT_PINS       0xFF00u

// Phase pins and priorities
#define PRIO_LOW_PIN   2u
#define PRIO_HIGH_PIN  5u
#define THR_PIN        1u
#define THR_PRIO       2u

// LED codes
#define LED_WAIT(p)    (0x10u | (p))
#define LED_THR_HELD   0x14u
#define LED_PASS       0xA5u
#define LED_FAIL(s)    (0xE0u | (s))

// Failure steps
enum {
    FAIL_INIT = 1,         // driver bind or PLIC set-up read-back
    FAIL_COUNT_WAIT,       // phase 1: edges did not all interrupt
    FAIL_COUNT_CHECK,      // phase 1: per-pin count wrong
    FAIL_PRIO_WAIT,        // phase 2: both sources never pending
    FAIL_PRIO_ORDER,       // phase 2: claim order wrong
    FAIL_THR_WAIT,         // phase 3: source never pending
    FAIL_THR_LEAK,         // phase 3: interrupt taken below the threshold
    FAIL_THR_RELEASE,      // phase 3: no interrupt after lowering the threshold
    FAIL_KEY_WAIT,         // phase 5: KEY interrupt not taken
    FAIL_UNEXPECTED,       // a source outside the test was claimed
    FAIL_FINAL,            // final counts or pending state wrong
    FAIL_ARD               // phase 6: Arduino GPIO pin did not interrupt once
};

// Wait bounds (busy-loop iterations). The board waits for the inputs for ever.
#ifdef SIMULATION
#define WAIT_LOOPS     40000u
#define HOLD_LOOPS     300u
#else
#define WAIT_LOOPS     0u
#define HOLD_LOOPS     200000u
#endif

#define LOG_MAX        32u

static lks_t    lks;
static apb_gpio_t   gpio;
static apb_gpio_t   gpio_ard;
static plic_t   plic;
static unsigned ctx;

static volatile uint32_t src_count[BOARD_PLIC_SOURCES + 1u];
static volatile uint32_t claim_log[LOG_MAX];
static volatile uint32_t n_claims;
static volatile uint32_t gpio_total;
static volatile uint32_t ard_total;
static volatile uint32_t unexpected;

//-----------------------------------------------------------------------------
// Interrupt handling
//-----------------------------------------------------------------------------
static void on_source(uint32_t src, void *arg)
{
    (void)arg;
    if (n_claims < LOG_MAX)
        claim_log[n_claims] = src;
    n_claims = n_claims + 1u;
    src_count[src] = src_count[src] + 1u;

    if (src >= BOARD_PLIC_SRC_GPIO(0) && src <= BOARD_PLIC_SRC_GPIO(BOARD_GPIO_NR - 1u)) {
        unsigned pin = src - BOARD_PLIC_SRC_GPIO(0);
        apb_gpio_irq_clear(&gpio, 1u << pin);
        gpio_total = gpio_total + 1u;
        apb_gpio_write(&gpio, ((gpio_total & 0xFu) << 12) | (pin << 8));
    } else if (src >= BOARD_PLIC_SRC_GPIO_ARD(0) &&
               src <= BOARD_PLIC_SRC_GPIO_ARD(BOARD_GPIO_ARD_NR - 1u)) {
        apb_gpio_irq_clear(&gpio_ard, 1u << (src - BOARD_PLIC_SRC_GPIO_ARD(0)));
        ard_total = ard_total + 1u;
    } else if (src == BOARD_PLIC_SRC_KEY) {
        lks_irq_clear(&lks, LKS_KEY_ALL);
    } else if (src == BOARD_PLIC_SRC_SW) {
        lks_irq_clear(&lks, LKS_SW_ALL);
    } else {
        // Nothing to clear it at: disabled before the completion, the source
        // stays in service and cannot interrupt again
        unexpected = unexpected + 1u;
        (void)plic_disable(&plic, ctx, src);
    }
}

static void on_external(uint32_t cause)
{
    (void)cause;
    while (plic_dispatch(&plic, ctx, on_source, 0) > 0)
        ;
}

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
static void fail(unsigned step)
{
    arv_irq_global_disable();
    lks_led_set(&lks, LED_FAIL(step));
    for (;;)
        arv_wfi();
}

typedef bool (*cond_t)(void);

static bool wait_for(cond_t cond)
{
#if WAIT_LOOPS == 0u
    while (!cond())
        ;
    return true;
#else
    for (uint32_t n = 0u; n < WAIT_LOOPS; n++)
        if (cond())
            return true;
    return false;
#endif
}

static void hold(void)
{
    for (volatile uint32_t n = 0u; n < HOLD_LOOPS; n++)
        ;
}

static bool count_done(void)   { return gpio_total >= 8u; }
static bool prio_pending(void) { return plic_is_pending(&plic, BOARD_PLIC_SRC_GPIO(PRIO_LOW_PIN)) &&
                                        plic_is_pending(&plic, BOARD_PLIC_SRC_GPIO(PRIO_HIGH_PIN)); }
static uint32_t prio_base;    // claims before phase 2 (both pins also interrupted in phase 1)
static bool prio_done(void)    { return n_claims >= prio_base + 2u; }
static bool thr_pending(void)  { return plic_is_pending(&plic, BOARD_PLIC_SRC_GPIO(THR_PIN)); }
static uint32_t thr_base;     // count of the threshold source before phase 3 (it also interrupted in phase 1)
static bool thr_done(void)     { return src_count[BOARD_PLIC_SRC_GPIO(THR_PIN)] != thr_base; }
static bool key_done(void)     { return src_count[BOARD_PLIC_SRC_KEY] != 0u; }
static unsigned ard_pin;      // Arduino pin driven in phase 6
static bool ard_done(void)     { return src_count[BOARD_PLIC_SRC_GPIO_ARD(ard_pin)] != 0u; }

//-----------------------------------------------------------------------------
// Set-up
//-----------------------------------------------------------------------------
static void setup(void)
{
    const apb_gpio_config_t gcfg = { .base = BOARD_GPIO_BASE, .pin_nr = BOARD_GPIO_NR };
    const apb_gpio_config_t acfg = { .base = BOARD_GPIO_ARD_BASE, .pin_nr = BOARD_GPIO_ARD_NR };
    const plic_config_t pcfg = { .base        = BOARD_PLIC_BASE,
                                 .num_sources = BOARD_PLIC_SOURCES,
                                 .num_harts   = BOARD_PLIC_HARTS,
                                 .prio_bits   = BOARD_PLIC_PRIO_BITS,
                                 .su_mode     = BOARD_PLIC_SU_MODE };

    lks_init(&lks, BOARD_LKS_BASE);
    lks_led_set(&lks, 0x00u);

    if (apb_gpio_init(&gpio, &gcfg) != APB_GPIO_OK ||
        apb_gpio_init(&gpio_ard, &acfg) != APB_GPIO_OK ||
        plic_init(&plic, &pcfg) != PLIC_OK ||
        plic_context(&plic, 0u, false, &ctx) != PLIC_OK)
        fail(FAIL_INIT);

    // GPIO: 15:8 outputs at 0, 7:0 inputs with rising-edge interrupts
    apb_gpio_write(&gpio, 0u);
    if (apb_gpio_set_dir(&gpio, OUT_PINS, APB_GPIO_DIR_OUT) != APB_GPIO_OK ||
        apb_gpio_set_dir(&gpio, IN_PINS,  APB_GPIO_DIR_IN)  != APB_GPIO_OK)
        fail(FAIL_INIT);
    if (apb_gpio_input_enable(&gpio, IN_PINS) != APB_GPIO_OK)
        fail(FAIL_INIT);
    for (unsigned pin = 0u; pin < 8u; pin++)
        if (apb_gpio_irq_config(&gpio, pin, APB_GPIO_IRQ_RISE) != APB_GPIO_OK)
            fail(FAIL_INIT);
    apb_gpio_irq_clear(&gpio, gpio.pin_mask);
    if (apb_gpio_irq_enable(&gpio, IN_PINS) != APB_GPIO_OK)
        fail(FAIL_INIT);

    // Arduino GPIO: every pin an output at 0, sampled, rising-edge interrupt
    apb_gpio_write(&gpio_ard, 0u);
    if (apb_gpio_set_dir(&gpio_ard, gpio_ard.pin_mask, APB_GPIO_DIR_OUT) != APB_GPIO_OK ||
        apb_gpio_input_enable(&gpio_ard, gpio_ard.pin_mask) != APB_GPIO_OK)
        fail(FAIL_INIT);
    for (unsigned pin = 0u; pin < BOARD_GPIO_ARD_NR; pin++)
        if (apb_gpio_irq_config(&gpio_ard, pin, APB_GPIO_IRQ_RISE) != APB_GPIO_OK)
            fail(FAIL_INIT);
    apb_gpio_irq_clear(&gpio_ard, gpio_ard.pin_mask);
    if (apb_gpio_irq_enable(&gpio_ard, gpio_ard.pin_mask) != APB_GPIO_OK)
        fail(FAIL_INIT);

    // PLIC: every source at priority 1 and enabled on context 0, threshold 0
    for (unsigned src = 1u; src <= BOARD_PLIC_SOURCES; src++) {
        if (plic_set_priority(&plic, src, 1u) != PLIC_OK ||
            plic_enable(&plic, ctx, src)      != PLIC_OK ||
            plic_get_priority(&plic, src)     != 1u      ||
            !plic_is_enabled(&plic, ctx, src))
            fail(FAIL_INIT);
    }
    if (plic_set_threshold(&plic, ctx, 0u) != PLIC_OK)
        fail(FAIL_INIT);

    // Core: M-mode external interrupt
    arv_irq_attach(IRQ_M_EXT, on_external);
    arv_irq_enable(IRQ_M_EXT);
    arv_irq_global_enable();
}

//-----------------------------------------------------------------------------
// Phases
//-----------------------------------------------------------------------------
static void phase_count(void)
{
    lks_led_set(&lks, LED_WAIT(1u));
    if (!wait_for(count_done))
        fail(FAIL_COUNT_WAIT);
    for (unsigned pin = 0u; pin < 8u; pin++)
        if (src_count[BOARD_PLIC_SRC_GPIO(pin)] != 1u)
            fail(FAIL_COUNT_CHECK);
}

static void phase_priority(void)
{
    const uint32_t lo = BOARD_PLIC_SRC_GPIO(PRIO_LOW_PIN);
    const uint32_t hi = BOARD_PLIC_SRC_GPIO(PRIO_HIGH_PIN);
    uint32_t base;

    // The higher priority goes to the higher ID: ties go to the lower ID
    arv_irq_disable(IRQ_M_EXT);
    (void)plic_set_priority(&plic, lo, 2u);
    (void)plic_set_priority(&plic, hi, 6u);
    base      = n_claims;
    prio_base = base;

    lks_led_set(&lks, LED_WAIT(2u));
    if (!wait_for(prio_pending))
        fail(FAIL_PRIO_WAIT);
    arv_irq_enable(IRQ_M_EXT);
    if (!wait_for(prio_done))
        fail(FAIL_PRIO_ORDER);
    if (n_claims != base + 2u || claim_log[base] != hi || claim_log[base + 1u] != lo)
        fail(FAIL_PRIO_ORDER);

    (void)plic_set_priority(&plic, lo, 1u);
    (void)plic_set_priority(&plic, hi, 1u);
}

static void phase_threshold(void)
{
    const uint32_t src = BOARD_PLIC_SRC_GPIO(THR_PIN);
    uint32_t total;

    // A source notifies only with a priority strictly above the threshold
    (void)plic_set_priority(&plic, src, THR_PRIO);
    (void)plic_set_threshold(&plic, ctx, THR_PRIO + 1u);
    total    = gpio_total;
    thr_base = src_count[src];

    lks_led_set(&lks, LED_WAIT(3u));
    if (!wait_for(thr_pending))
        fail(FAIL_THR_WAIT);
    hold();
    if (gpio_total != total || src_count[src] != thr_base || !thr_pending())
        fail(FAIL_THR_LEAK);

    lks_led_set(&lks, LED_THR_HELD);
    (void)plic_set_threshold(&plic, ctx, 0u);
    if (!wait_for(thr_done) || src_count[src] != thr_base + 1u)
        fail(FAIL_THR_RELEASE);

    (void)plic_set_priority(&plic, src, 1u);
}

static void phase_key(void)
{
    lks_irq_config(&lks, LKS_KEY(1), LKS_EDGE_FALLING);
    lks_led_set(&lks, LED_WAIT(5u));
    if (!wait_for(key_done))
        fail(FAIL_KEY_WAIT);
    lks_irq_disable(&lks, LKS_KEY(1));
}

static void phase_arduino(void)
{
    lks_led_set(&lks, LED_WAIT(6u));
    for (ard_pin = 0u; ard_pin < BOARD_GPIO_ARD_NR; ard_pin++) {
        apb_gpio_set(&gpio_ard, 1u << ard_pin);
        if (!wait_for(ard_done) || src_count[BOARD_PLIC_SRC_GPIO_ARD(ard_pin)] != 1u)
            fail(FAIL_ARD);
    }
    apb_gpio_write(&gpio_ard, 0u);                       // falling edges: no interrupt
    hold();
}

static void final_check(void)
{
    if (unexpected != 0u)
        fail(FAIL_UNEXPECTED);
    if (gpio_total != 11u || src_count[BOARD_PLIC_SRC_KEY] != 1u ||
        src_count[BOARD_PLIC_SRC_SW] != 0u ||
        plic_pending_word(&plic, 0u) != 0u || apb_gpio_irq_pending(&gpio) != 0u)
        fail(FAIL_FINAL);
    if (ard_total != BOARD_GPIO_ARD_NR || plic_pending_word(&plic, 1u) != 0u ||
        apb_gpio_irq_pending(&gpio_ard) != 0u)
        fail(FAIL_FINAL);
}

//=============================================================================
// main
//=============================================================================
int main(void)
{
    arv_irq_init();
    setup();

    phase_count();
    phase_priority();
    phase_threshold();
    phase_key();
    phase_arduino();
    final_check();

    // Pins 0-7 keep interrupting and the count keeps showing on pins 15:8
    lks_led_set(&lks, LED_PASS);
    for (;;)
        arv_wfi();

    return 0;
}

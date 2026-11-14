/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      led_key_sw.h
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Driver of the DE0-Nano-SoC LED / key / switch peripheral (apb_led_key_sw):
 * 8 LEDs, 4 switches and 2 keys, debounced, with per-input edge interrupts.
 * The KEY inputs raise the board's KEY interrupt, the switches the SW one
 * (de0_board.h). KEY[0] is the board reset.
 *----------------------------------------------------------------------------*/
#ifndef LED_KEY_SW_H
#define LED_KEY_SW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile uint32_t LED_CTRL;        /* 0x00: LED[7:0]                           */
    volatile uint32_t TTY_DATA;        /* 0x04: character output (simulation log)  */
             uint32_t RESERVED[6];     /* 0x08 .. 0x1C                             */
    volatile const uint32_t VAL;       /* 0x20: debounced inputs                   */
    volatile uint32_t IRQ_EN;          /* 0x24: interrupt enable per input         */
    volatile uint32_t IRQ_EDGE;        /* 0x28: 1 rising, 0 falling                */
    volatile uint32_t IRQ_BOTH;        /* 0x2C: 1 both edges (overrides IRQ_EDGE)  */
    volatile uint32_t IRQ_VAL;         /* 0x30: pending, write 1 to clear          */
} LKS_TypeDef;

_Static_assert(offsetof(LKS_TypeDef, VAL)     == 0x20, "LKS VAL offset");
_Static_assert(offsetof(LKS_TypeDef, IRQ_VAL) == 0x30, "LKS IRQ_VAL offset");

/* Input bits (VAL, IRQ_*): switches [3:0], keys [5:4]; keys read 0 when pressed */
#define LKS_SW(n)        (1u << (n))
#define LKS_KEY(n)       (1u << (4u + (n)))
#define LKS_SW_ALL       0x0Fu
#define LKS_KEY_ALL      0x30u

typedef enum {
    LKS_EDGE_FALLING = 0,              /* a key press, a switch moved down         */
    LKS_EDGE_RISING  = 1,              /* a key release, a switch moved up         */
    LKS_EDGE_BOTH    = 2
} lks_edge_t;

typedef struct {
    LKS_TypeDef *regs;
} lks_t;

static inline void lks_init(lks_t *dev, uintptr_t base)
{
    dev->regs = (LKS_TypeDef *) base;
    dev->regs->IRQ_EN  = 0u;
    dev->regs->IRQ_VAL = LKS_SW_ALL | LKS_KEY_ALL;
}

static inline void     lks_led_set(lks_t *dev, uint32_t leds) { dev->regs->LED_CTRL = leds & 0xFFu; }
static inline uint32_t lks_led_get(const lks_t *dev)          { return dev->regs->LED_CTRL & 0xFFu; }
static inline uint32_t lks_inputs(const lks_t *dev)           { return dev->regs->VAL & (LKS_SW_ALL | LKS_KEY_ALL); }
static inline uint32_t lks_switches(const lks_t *dev)         { return dev->regs->VAL & LKS_SW_ALL; }
static inline int      lks_key_pressed(const lks_t *dev, unsigned n)
{
    return (dev->regs->VAL & LKS_KEY(n)) == 0u;
}

/* Interrupt on the given edge of the inputs in 'mask' (other inputs unchanged) */
void     lks_irq_config(lks_t *dev, uint32_t mask, lks_edge_t edge);
void     lks_irq_disable(lks_t *dev, uint32_t mask);
static inline uint32_t lks_irq_pending(const lks_t *dev)      { return dev->regs->IRQ_VAL; }
static inline void     lks_irq_clear(lks_t *dev, uint32_t mask) { dev->regs->IRQ_VAL = mask; }

/* One character to the TTY register (shown in the simulation log) */
static inline void lks_tty_putc(lks_t *dev, char c)           { dev->regs->TTY_DATA = (uint8_t) c; }

#ifdef __cplusplus
}
#endif

#endif /* LED_KEY_SW_H */

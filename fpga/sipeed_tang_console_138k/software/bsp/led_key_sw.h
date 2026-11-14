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
 * Driver of the Tang Console 138K board I/O peripheral (apb_board_io):
 * 8 LEDs, 4 switches and 2 keys, debounced, with per-input edge interrupts.
 * The KEY inputs raise the board's KEY interrupt, the switches the SW one
 * (tang_board.h). KEY[0] is the board reset. The USB host ports have their
 * own driver (usb_hid.h, over apb_usb_host); the RGB panel is driven by
 * Belenos (built-in DPI driver, blns_dpi_*).
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
    volatile const uint32_t AUDIO_CTRL;/* 0x08: [0] PA_EN, speaker amplifier on    */
    volatile const uint32_t SD_STATUS; /* 0x0C: [0] SD_DET, microSD detect pin     */
    volatile uint32_t AUDIO_OUT;       /* 0x10: [1:0] the sound output             */
             uint32_t RESERVED[3];     /* 0x14 .. 0x1C                             */
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

/* Sound output: the I2S master's stream goes to one of them, the others stay
 * silent. From reset: HDMI in the HDMI build, the Pmod I2S2 (on PMOD1, whose
 * pins then leave the GPIO) in the panel build. The onboard speaker amplifier
 * (MAX98357A) is on only while it is the output, and needs about 7 ms after
 * being selected before it plays (lks_speaker_on reads its state) */
typedef enum {
    LKS_AUDIO_HDMI    = 0,
    LKS_AUDIO_SPEAKER = 1,
    LKS_AUDIO_PMOD    = 2,
    LKS_AUDIO_NONE    = 3
} lks_audio_t;
#define LKS_AUDIO_PA_EN  1u
static inline void        lks_audio_output(lks_t *dev, lks_audio_t out) { dev->regs->AUDIO_OUT = (uint32_t) out & 3u; }
static inline lks_audio_t lks_audio_output_get(const lks_t *dev)       { return (lks_audio_t) (dev->regs->AUDIO_OUT & 3u); }
static inline int         lks_speaker_on(const lks_t *dev)             { return (dev->regs->AUDIO_CTRL & LKS_AUDIO_PA_EN) != 0u; }

/* microSD slot card detect: the debounced pin level, 0 with a card in the
 * slot (its detect switch closes to ground through 1K), 1 with the slot empty
 * (the FPGA pull-up) and from reset until the first debounce */
#define LKS_SD_DET       1u
static inline int  lks_sd_det_level(lks_t *dev)               { return (dev->regs->SD_STATUS & LKS_SD_DET) != 0u; }

/* One character to the TTY register (shown in the simulation log) */
static inline void lks_tty_putc(lks_t *dev, char c)           { dev->regs->TTY_DATA = (uint8_t) c; }

#ifdef __cplusplus
}
#endif

#endif /* LED_KEY_SW_H */

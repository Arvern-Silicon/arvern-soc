//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      audio.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The menu's sound (see audio.h): the TX_LOW interrupt renders the free
// FIFO words with the synthesizer, so the stream needs nothing from the main
// loop and plays on through card reads and frame waits.
//----------------------------------------------------------------------------
#include "tang_board.h"
#include "demo_kit.h"
#include "apb_i2s.h"
#include "ahb_plic.h"
#include "audio.h"

#define TX_THR       8u                     // refill at or below 8 words
#define FIFO_MAX     16u
#define MEIE         (1u << 11)

volatile audio_status_t audio = { .magic = 0x4F445541u };

static apb_i2s_t  i2s;
static plic_t     plic;
static unsigned   ctx;
static uint32_t   mtvec_boot;

// Each rendered sample goes out twice: the stream at 2 x SYNTH_FS
static void feed(void)
{
    uint32_t buf[FIFO_MAX / 2u];
    unsigned n = (i2s.tx_depth - apb_i2s_tx_level(&i2s)) / 2u;

    if (n > FIFO_MAX / 2u)
        n = FIFO_MAX / 2u;
    synth_render(buf, n);
    for (unsigned i = 0; i < n; i++) {
        i2s.regs->TXDATA = buf[i];
        i2s.regs->TXDATA = buf[i];
    }
    audio.samples += n;
    if (apb_i2s_irq_pending(&i2s) & APB_I2S_IRQ_TX_UNDER) {
        apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_TX_UNDER);
        audio.underruns++;
    }
}

static void __attribute__((interrupt("machine"), aligned(4))) audio_isr(void)
{
    uint32_t id, cause;
    __asm__ volatile ("csrr %0, mcause" : "=r" (cause));
    if (!(cause >> 31))                     // an exception: the menu has none to handle
        for (;;)
            ;
    while ((id = plic_claim(&plic, ctx)) != 0u) {
        if (id == BOARD_PLIC_SRC_I2S)
            feed();
        (void) plic_complete(&plic, ctx, id);
    }
    audio.irqs++;
}

bool audio_init(void)
{
    const apb_i2s_config_t cfg = {
        .clocks     = { .mdiv = SYNTH_MDIV / 2u, .ratio = SYNTH_RATIO },
        .format     = APB_I2S_FMT_I2S,
        .mclk       = true,                 // the Pmod I2S2 needs it
        .rx_dly     = 1u,
        .tx_packing = APB_I2S_STEREO,
        .rx_packing = APB_I2S_STEREO
    };
    const plic_config_t     pcfg = { .base = BOARD_PLIC_BASE, .num_sources = BOARD_PLIC_SOURCES,
                                     .num_harts = BOARD_PLIC_HARTS, .prio_bits = BOARD_PLIC_PRIO_BITS,
                                     .su_mode = BOARD_PLIC_SU_MODE };
    static const uint32_t silence[FIFO_MAX];

    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (i2s.tx_depth == 0u) ||
        (plic_init(&plic, &pcfg) != PLIC_OK) ||
        (plic_context(&plic, 0u, false, &ctx) != PLIC_OK))
        return false;
    // a restart of the core alone may find the stream running
    apb_i2s_irq_enable(&i2s, 0u);
    apb_i2s_stop(&i2s);
    if ((apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK) || (apb_i2s_set_thresholds(&i2s, TX_THR, 1u) != APB_I2S_OK))
        return false;
    apb_i2s_flush(&i2s, true, true);

    __asm__ volatile ("csrr %0, mtvec" : "=r" (mtvec_boot));
    __asm__ volatile ("csrw mtvec, %0" :: "r" (&audio_isr));
    for (unsigned s = 1; s <= BOARD_PLIC_SOURCES; s++)     // only the I2S master
        (void) plic_disable(&plic, ctx, s);
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 1u);
    (void) plic_set_threshold(&plic, ctx, 0u);
    (void) plic_enable(&plic, ctx, BOARD_PLIC_SRC_I2S);
    __asm__ volatile ("csrs mie, %0" :: "r" (MEIE));

    (void) apb_i2s_write(&i2s, silence, FIFO_MAX);
    (void) apb_i2s_start(&i2s, true, false);
    apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_TX_UNDER);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
    audio.on = 1u;
    __asm__ volatile ("csrsi mstatus, 8");
    return true;
}

void audio_stop(void)
{
    static const uint32_t silence[4];
    uint32_t id;

    __asm__ volatile ("csrci mstatus, 8");
    if (!audio.on)
        return;
    apb_i2s_irq_enable(&i2s, 0u);
    while ((id = plic_claim(&plic, ctx)) != 0u)
        (void) plic_complete(&plic, ctx, id);
    (void) plic_disable(&plic, ctx, BOARD_PLIC_SRC_I2S);
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 0u);
    __asm__ volatile ("csrc mie, %0" :: "r" (MEIE));
    __asm__ volatile ("csrw mtvec, %0" :: "r" (mtvec_boot));

    // the FIFO played out, a few frames of silence, then the engine stopped
    (void) apb_i2s_write(&i2s, silence, 4u);
    while (apb_i2s_tx_level(&i2s) != 0u)
        ;
    apb_i2s_stop(&i2s);
    apb_i2s_flush(&i2s, true, true);
    apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_STICKY);
    i2s.regs->THRESH = 0u;
    audio.on = 0u;
}

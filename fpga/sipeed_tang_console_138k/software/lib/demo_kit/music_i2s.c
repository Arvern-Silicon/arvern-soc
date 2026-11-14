//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      music_i2s.c  (Tang Console 138K demo kit)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The demos' music on the board: the I2S master (apb_i2s) streams the
// synthesizer (music.c) to the sound output the boot menu selected (board
// I/O AUDIO_OUT: HDMI, the onboard speaker or a Pmod I2S2), each rendered
// sample sent twice (MUS_FS = hclk / 1536, 22.04 kHz; the stream at 44.08
// kHz, an HDMI audio rate). Its TX_LOW interrupt (PLIC source 17) renders
// the free FIFO words, so the music needs nothing from the demo's loop.
// music_init() takes mtvec and the machine external interrupt (the demos
// use no other interrupt); without an I2S master the demo runs silent.
//----------------------------------------------------------------------------
#include "tang_board.h"
#include "apb_i2s.h"
#include "ahb_plic.h"
#include "music.h"
#include "music_i2s.h"

#define MDIV         2u                     // MCLK = hclk / MDIV
#define RATIO        384u                   // MCLK / LRCK
#define TX_THR       8u                     // refill at or below 8 words
#define FIFO_MAX     16u
#define MEIE         (1u << 11)

_Static_assert(BOARD_HCLK_HZ / (2u * MDIV * RATIO) == MUS_FS, "MUS_FS does not match the board clock");

static apb_i2s_t i2s;
static plic_t    plic;
static unsigned  ctx;
static bool      on;

static void feed(void)
{
    uint32_t buf[FIFO_MAX / 2u];
    unsigned n = (i2s.tx_depth - apb_i2s_tx_level(&i2s)) / 2u;

    if (n > FIFO_MAX / 2u)
        n = FIFO_MAX / 2u;
    mus_render(buf, n);
    for (unsigned i = 0; i < n; i++) {
        i2s.regs->TXDATA = buf[i];
        i2s.regs->TXDATA = buf[i];
    }
    if (apb_i2s_irq_pending(&i2s) & APB_I2S_IRQ_TX_UNDER)
        apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_TX_UNDER);
}

static void __attribute__((interrupt("machine"), aligned(4))) music_isr(void)
{
    uint32_t id, cause;
    __asm__ volatile ("csrr %0, mcause" : "=r" (cause));
    if (!(cause >> 31))                     // an exception: the demos have none to handle
        for (;;)
            ;
    while ((id = plic_claim(&plic, ctx)) != 0u) {
        if (id == BOARD_PLIC_SRC_I2S)
            feed();
        (void) plic_complete(&plic, ctx, id);
    }
}

bool music_init(void)
{
    const apb_i2s_config_t cfg = {
        .clocks     = { .mdiv = MDIV / 2u, .ratio = RATIO },
        .format     = APB_I2S_FMT_I2S,
        .mclk       = true,                 // the Pmod I2S2 needs it
        .rx_dly     = 1u,
        .tx_packing = APB_I2S_STEREO,
        .rx_packing = APB_I2S_STEREO
    };
    const plic_config_t pcfg = { .base = BOARD_PLIC_BASE, .num_sources = BOARD_PLIC_SOURCES,
                                 .num_harts = BOARD_PLIC_HARTS, .prio_bits = BOARD_PLIC_PRIO_BITS,
                                 .su_mode = BOARD_PLIC_SU_MODE };
    static const uint32_t silence[FIFO_MAX];

    if (on)
        return true;
    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (i2s.tx_depth == 0u) ||
        (plic_init(&plic, &pcfg) != PLIC_OK) || (plic_context(&plic, 0u, false, &ctx) != PLIC_OK))
        return false;
    apb_i2s_irq_enable(&i2s, 0u);           // a restart of the core alone may find the stream running
    apb_i2s_stop(&i2s);
    if ((apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK) || (apb_i2s_set_thresholds(&i2s, TX_THR, 1u) != APB_I2S_OK))
        return false;
    apb_i2s_flush(&i2s, true, true);

    __asm__ volatile ("csrw mtvec, %0" :: "r" (&music_isr));
    for (unsigned s = 1; s <= BOARD_PLIC_SOURCES; s++)
        (void) plic_disable(&plic, ctx, s);
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 1u);
    (void) plic_set_threshold(&plic, ctx, 0u);
    (void) plic_enable(&plic, ctx, BOARD_PLIC_SRC_I2S);
    __asm__ volatile ("csrs mie, %0" :: "r" (MEIE));

    (void) apb_i2s_write(&i2s, silence, FIFO_MAX);
    (void) apb_i2s_start(&i2s, true, false);
    apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_TX_UNDER);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
    on = true;
    __asm__ volatile ("csrsi mstatus, 8");
    return true;
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K I2S test)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// tst_i2s -- the I2S master (apb_i2s at 0x10042000, PLIC source 17) and the
// sound outputs the board I/O AUDIO_OUT selects: the onboard MAX98357A
// amplifier, a Pmod I2S2 on PMOD1 and the HDMI audio input (Belenos).
//
// Simulation (-DSIMULATION): the LED register paces the stimulus
// (src/tst_i2s.v), which decodes the amplifier pins and the HDMI input and
// loops the Pmod's D/A SDIN (GPIO 14) back to its A/D SDOUT (GPIO 15):
//   0x11  speaker: PA_EN on, NWORDS stereo words sent in I2S format; the
//         stimulus checks them on I2S_DOUT and that PMOD1 stays undriven;
//         TX_LOW reaches PLIC source 17
//   0x12  Pmod: PA_EN off, the words sent and received through the external
//         loopback; the stimulus checks the amplifier pins idle and
//         MCLK / BCLK / LRCK on the Pmod
//   0x13  HDMI: the words sent; the stimulus checks them on the HDMI input,
//         the amplifier and the Pmod idle
//   0x14  none: a stream sent; the stimulus checks every output idle
// VERDICT on the LEDs: 0xA5 pass, 0xE0 | step on the first failure.
//
// Board: a 1 kHz tone, 2 s on the speaker (LED register 0x01), 2 s on the
// Pmod I2S2 line out (0x02), 2 s on HDMI (0x03), repeated.
//----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "apb_i2s.h"

#define LED_PASS       0xA5u
#define LED_FAIL       0xE0u
#define NWORDS         12u

static lks_t      lks;
static apb_i2s_t  i2s;

static void fail(unsigned step)
{
    lks_led_set(&lks, LED_FAIL | step);
    for (;;)
        ;
}

// Clocks: MDIV 1, RATIO 256 (66.1 kHz) in simulation; 44.08 kHz (RATIO 384,
// every device's range) on the board. I2S format, MCLK for the Pmod.
static void setup(void)
{
    apb_i2s_config_t cfg = {
        .clocks     = { .mdiv = 1u, .ratio = 256u },
        .format     = APB_I2S_FMT_I2S,
        .mclk       = true,
        .rx_dly     = 1u,
        .tx_packing = APB_I2S_STEREO,
        .rx_packing = APB_I2S_STEREO
    };
#ifndef SIMULATION
    cfg.clocks.ratio = 384u;
#endif
    if (apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK)
        fail(0u);
}

#ifdef SIMULATION

// Word i of the test stream (both halves non-zero; the stimulus has the same)
static uint32_t pattern(unsigned i)
{
    return (((0x8765u - 0x0203u * i) & 0xFFFFu) << 16) | ((0x1234u + 0x0101u * i) & 0xFFFFu);
}

static bool plic_pending(unsigned src)
{
    volatile const uint32_t *pend = (volatile const uint32_t *) (BOARD_PLIC_BASE + 0x1000u);
    return ((pend[src >> 5] >> (src & 31u)) & 1u) != 0u;
}

// The NWORDS test words through the transmitter, played out
static void send(void)
{
    for (unsigned i = 0u; i < NWORDS; i++) {
        uint32_t w = pattern(i);
        (void)apb_i2s_write(&i2s, &w, 1u);
    }
    apb_i2s_start(&i2s, true, false);
    while (apb_i2s_tx_level(&i2s) != 0u)
        ;
    apb_i2s_stop(&i2s);
}

int main(void)
{
    uint32_t got[NWORDS + 8u];
    unsigned i, n, first;

    lks_init(&lks, BOARD_LKS_BASE);
    if (apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK)
        fail(0u);
    setup();

    // Speaker
    lks_audio_output(&lks, LKS_AUDIO_SPEAKER);        // each step: the output, then the LED value
    lks_led_set(&lks, 0x11u);
    if (!lks_speaker_on(&lks) || (lks_audio_output_get(&lks) != LKS_AUDIO_SPEAKER))
        fail(1u);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
    if (!plic_pending(BOARD_PLIC_SRC_I2S))                 // empty FIFO, TX_THR 0
        fail(1u);
    for (i = 0u; i < NWORDS; i++) {
        uint32_t w = pattern(i);
        if (apb_i2s_write(&i2s, &w, 1u) != 1u)
            fail(1u);
    }
    apb_i2s_start(&i2s, true, false);
    while (apb_i2s_tx_level(&i2s) != 0u)
        ;
    apb_i2s_stop(&i2s);
    apb_i2s_irq_enable(&i2s, 0u);

    // Pmod I2S2, external loopback
    lks_audio_output(&lks, LKS_AUDIO_PMOD);
    lks_led_set(&lks, 0x12u);
    if (lks_speaker_on(&lks))
        fail(2u);
    for (i = 0u; i < NWORDS; i++) {
        uint32_t w = pattern(i);
        (void)apb_i2s_write(&i2s, &w, 1u);
    }
    apb_i2s_start(&i2s, true, true);
    for (n = 0u; n < NWORDS + 8u; )
        n += apb_i2s_read(&i2s, &got[n], NWORDS + 8u - n);
    apb_i2s_stop(&i2s);
    for (first = 0u; (first < 8u) && (got[first] == 0u); first++)
        ;
    for (i = 0u; i < NWORDS; i++)
        if ((first + i >= NWORDS + 8u) || (got[first + i] != pattern(i)))
            fail(2u);

    // HDMI
    apb_i2s_flush(&i2s, true, true);
    lks_audio_output(&lks, LKS_AUDIO_HDMI);
    lks_led_set(&lks, 0x13u);
    send();

    // None: every output idle
    lks_audio_output(&lks, LKS_AUDIO_NONE);
    lks_led_set(&lks, 0x14u);
    if (lks_speaker_on(&lks) || (lks_audio_output_get(&lks) != LKS_AUDIO_NONE))
        fail(4u);
    send();
    lks_led_set(&lks, LED_PASS);
    for (;;)
        ;
}

#else

// Two seconds of a 1 kHz square tone at quarter scale, one stereo frame per
// word: the FIFO paces the loop
static void tone(void)
{
    const unsigned fs = BOARD_HCLK_HZ / 768u, half = fs / 2000u;
    unsigned k;
    apb_i2s_start(&i2s, true, false);
    for (k = 0u; k < 2u * fs; ) {
        int16_t  s = ((k / half) & 1u) ? 8192 : -8192;
        uint32_t w = ((uint32_t) (uint16_t) s << 16) | (uint16_t) s;
        k += apb_i2s_write(&i2s, &w, 1u);
    }
    apb_i2s_stop(&i2s);
}

int main(void)
{
    lks_init(&lks, BOARD_LKS_BASE);
    if (apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK)
        fail(0u);
    setup();
    for (;;) {
        lks_led_set(&lks, 0x01u);
        lks_audio_output(&lks, LKS_AUDIO_SPEAKER);
        tone();
        lks_led_set(&lks, 0x02u);
        lks_audio_output(&lks, LKS_AUDIO_PMOD);
        tone();
        if (BOARD_HDMI()) {
            lks_led_set(&lks, 0x03u);
            lks_audio_output(&lks, LKS_AUDIO_HDMI);
            tone();
        }
    }
}

#endif

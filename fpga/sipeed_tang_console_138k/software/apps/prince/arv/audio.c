/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      audio.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The sound output the boot menu selected, fed by picosdl's mixer (the
 * game's audio callback: the digitised sounds and the music): 22.04 kHz,
 * each frame sent twice per 44.08 kHz I2S frame, made in the I2S TX_LOW
 * interrupt (PLIC). The simulator build (ARV_SOFT_GFX) has no output: the
 * mixer runs from the frame loop instead (arv_audio_frame), so its work is
 * done and counted all the same.
 *----------------------------------------------------------------------------*/
#include <stdint.h>
#include "SDL2/SDL.h"
#include "psdl_internal.h"
#include "tang.h"

static volatile int paused = 1;
static int opened;
static int master_volume;

static inline void irq_off(void) { __asm__ volatile ("csrc mstatus, 8" ::: "memory"); }
static inline void irq_on(void)  { __asm__ volatile ("csrs mstatus, 8" ::: "memory"); }

#ifdef ARV_SOFT_GFX

void arv_audio_open(unsigned freq) { (void) freq; opened = 1; master_volume = PSDL_VOLUME_UNITY; }
void arv_audio_lock(void) {}
void arv_audio_unlock(void) {}

void arv_audio_frame(unsigned frames)
{
    Sint16 buf[2 * 64];
    if (!opened || paused)
        return;
    while (frames) {
        const unsigned n = frames > 64u ? 64u : frames;
        psdl_audio_render(buf, (int) n);
        frames -= n;
    }
}

#else

#include "tang_board.h"
#include "apb_i2s.h"
#include "ahb_plic.h"

#define RATIO  384u                             /* MCLK / LRCK: 44.08 kHz */
#define TX_THR 8u

static apb_i2s_t i2s;
static plic_t    plic;
static unsigned  plic_ctx;
static int       running;

static void feed(void)
{
    Sint16 buf[2 * 32];
    unsigned n = (i2s.tx_depth - apb_i2s_tx_level(&i2s)) / 2u;
    while (n) {
        const unsigned k = n > 32u ? 32u : n;
        if (paused) {
            for (unsigned i = 0; i < 2u * k; i++)
                buf[i] = 0;
        } else {
            psdl_audio_render(buf, (int) k);
        }
        for (unsigned i = 0; i < k; i++) {
            const int32_t v = ((((int32_t) buf[2 * i] + buf[2 * i + 1]) >> 1) * master_volume) / PSDL_VOLUME_UNITY;
            const uint32_t w = (uint32_t) (uint16_t) v << 16 | (uint16_t) v;
            i2s.regs->TXDATA = w;
            i2s.regs->TXDATA = w;
        }
        n -= k;
    }
}

static void isr(void)
{
    uint32_t id;
    while ((id = plic_claim(&plic, plic_ctx)) != 0u) {
        if (id == BOARD_PLIC_SRC_I2S)
            feed();
        (void) plic_complete(&plic, plic_ctx, id);
    }
}

void arv_audio_open(unsigned freq)
{
    const apb_i2s_config_t cfg = {
        .clocks     = { .mdiv = 1u, .ratio = RATIO },
        .format     = APB_I2S_FMT_I2S,
        .mclk       = true,
        .rx_dly     = 1u,
        .tx_packing = APB_I2S_STEREO,
        .rx_packing = APB_I2S_STEREO
    };
    const plic_config_t pcfg = { .base = BOARD_PLIC_BASE, .num_sources = BOARD_PLIC_SOURCES,
                                 .num_harts = BOARD_PLIC_HARTS, .prio_bits = BOARD_PLIC_PRIO_BITS,
                                 .su_mode = BOARD_PLIC_SU_MODE };
    (void) freq;
    if (running)
        return;
    master_volume = PSDL_VOLUME_UNITY;
    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK) ||
        (apb_i2s_set_thresholds(&i2s, TX_THR, 1u) != APB_I2S_OK) ||
        (plic_init(&plic, &pcfg) != PLIC_OK) || (plic_context(&plic, 0u, false, &plic_ctx) != PLIC_OK)) {
        arv_puts("prince: no I2S, no sound\n");
        return;
    }
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 1u);
    (void) plic_set_threshold(&plic, plic_ctx, 0u);
    (void) plic_enable(&plic, plic_ctx, BOARD_PLIC_SRC_I2S);
    arv_irq_handler(isr);
    __asm__ volatile ("csrs mie, %0" :: "r" (1u << 11));          /* MEIE */
    running = 1;
    opened = 1;
    irq_on();
    apb_i2s_start(&i2s, true, false);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
}

void arv_audio_lock(void)   { if (running) irq_off(); }
void arv_audio_unlock(void) { if (running) irq_on(); }
void arv_audio_frame(unsigned frames) { (void) frames; }

#endif

void arv_audio_pause(int pause_on) { paused = pause_on; }

/* picosdl's master volume (the keyboard's volume keys): applied to the
 * mixer's output by psdl_audio_render's caller, here at unity by default */
void PSDL_SetMasterVolume(int volume)
{
    master_volume = volume < 0 ? 0 : volume > PSDL_VOLUME_UNITY ? PSDL_VOLUME_UNITY : volume;
}
int PSDL_GetMasterVolume(void) { return master_volume; }

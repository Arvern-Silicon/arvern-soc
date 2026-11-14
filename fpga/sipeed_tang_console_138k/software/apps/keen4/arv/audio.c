/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      audio.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Sound of the Keen 4 port, on the sound output the boot menu selected (HDMI,
 * the onboard speaker or a Pmod I2S2): 22.04 kHz, each sample sent twice per
 * 44.08 kHz I2S frame.
 *
 *   effects  the game's PC speaker sounds: the tone it programs (a timer
 *            divisor, 1193182 / divisor Hz) as a softened square wave
 *   music    the AdLib songs rendered offline (MUSnn.ADP, IMA ADPCM at
 *            22.04 kHz, tools/render_music.c), streamed from the card through
 *            a 4 KB ring in the VRAM that the main loop keeps filled
 *   clock    the I2S TX_LOW interrupt (PLIC) also runs the game's timer
 *            service (SDL_t0Service, 140 times a second; 560 with music),
 *            as the PC's timer interrupt did; the simulation build keeps the
 *            faster clock of the machine timer (sd_tang.c)
 *----------------------------------------------------------------------------*/
#include <stdint.h>
#include "ff.h"
#include "tang_board.h"
#include "apb_i2s.h"
#include "ahb_plic.h"
#include "tang.h"

#define RATIO      384u                         /* MCLK / LRCK: 44.08 kHz             */
#define FS_OUT     (BOARD_HCLK_HZ / (4u * RATIO))   /* 22.04 kHz: each sample sent twice */
#define TX_THR     8u
#define PIT_HZ     1193182u
#define TONE_LEVEL 5000                         /* the square's amplitude             */
#define MUS_BYTES  VRAM_SOUND_SIZE              /* the music ring, a power of two     */
#define MUS_CHUNK  1024u
#define MUS_HEADER 16u
#define MUS_VOLUME 160                          /* of 256                             */

void SDL_t0Service(void);

static apb_i2s_t i2s;
static plic_t    plic;
static unsigned  plic_ctx;
static bool      running;

static volatile uint32_t tone_step;             /* Q32 phase step, 0: silent          */
static uint32_t  tone_phase;
static int32_t   tone_lp;                       /* one-pole low pass of the square    */
static volatile uint32_t t0_rate = 140u;        /* timer services per second          */
#ifndef SIMULATION
static uint32_t  t0_acc;
#endif
uint32_t         arv_audio_isr;

/* Music */
static uint8_t           *mring;
static volatile uint32_t  m_wr, m_rd;           /* bytes written / nibbles decoded    */
static volatile uint32_t  m_loop = ~0u;
static volatile bool      m_on, m_paused;
static FIL                m_file;
static bool               m_open;
static int32_t            m_pred, m_idx;
uint32_t                  arv_music_starved;

static const int16_t ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767 };
static const int8_t ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static inline void irq_off(void) { __asm__ volatile ("csrc mstatus, 8" ::: "memory"); }
static inline void irq_on(void)  { __asm__ volatile ("csrs mstatus, 8" ::: "memory"); }

static inline int32_t ima_decode(unsigned code)
{
    int32_t step = ima_step[m_idx], d = step >> 3;
    if (code & 4) d += step;
    if (code & 2) d += step >> 1;
    if (code & 1) d += step >> 2;
    m_pred += (code & 8) ? -d : d;
    if (m_pred > 32767) m_pred = 32767;
    if (m_pred < -32768) m_pred = -32768;
    m_idx += ima_index[code];
    if (m_idx < 0) m_idx = 0;
    if (m_idx > 88) m_idx = 88;
    return m_pred;
}

/* One sample per two free FIFO words; the game's timer service on time */
static void feed(void)
{
    unsigned n = (i2s.tx_depth - apb_i2s_tx_level(&i2s)) / 2u;
    uint32_t r = m_rd, avail = (m_on && !m_paused) ? m_wr * 2u : r;
    arv_audio_isr++;
    while (n--) {
        int32_t v = 0, sq;
        if (r < avail) {
            if (r == m_loop) { m_pred = 0; m_idx = 0; }
            const unsigned b = mring[(r >> 1) & (MUS_BYTES - 1u)];
            v = (ima_decode((r & 1u) ? (b >> 4) : (b & 15u)) * MUS_VOLUME) >> 8;
            r++;
        } else if (m_on && !m_paused) {
            arv_music_starved++;
        }
        tone_phase += tone_step;
        sq = tone_step ? ((tone_phase & 0x80000000u) ? TONE_LEVEL : -TONE_LEVEL) : 0;
        tone_lp += (sq - tone_lp) >> 1;
        v += tone_lp;
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        const uint32_t w = (uint32_t) (uint16_t) v << 16 | (uint16_t) v;
        i2s.regs->TXDATA = w;
        i2s.regs->TXDATA = w;
#ifndef SIMULATION
        t0_acc += t0_rate;
        if (t0_acc >= FS_OUT) {
            t0_acc -= FS_OUT;
            SDL_t0Service();
        }
#endif
    }
    m_rd = r;
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

void arv_irq_handler(void (*h)(void));

void arv_audio_start(void)
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
    if (running)
        return;
    mring = arv_vram + VRAM_SOUND;
    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK) ||
        (apb_i2s_set_thresholds(&i2s, TX_THR, 1u) != APB_I2S_OK) ||
        (plic_init(&plic, &pcfg) != PLIC_OK) || (plic_context(&plic, 0u, false, &plic_ctx) != PLIC_OK)) {
        arv_puts("keen4: no I2S, no sound\n");
        return;
    }
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 1u);
    (void) plic_set_threshold(&plic, plic_ctx, 0u);
    (void) plic_enable(&plic, plic_ctx, BOARD_PLIC_SRC_I2S);
    arv_irq_handler(isr);
    __asm__ volatile ("csrs mie, %0" :: "r" (1u << 11));          /* MEIE */
    running = true;
    irq_on();
    apb_i2s_start(&i2s, true, false);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
}

/* The game's timer service: on the I2S interrupt once the sound runs (the
 * simulation build: on the machine timer, sd_tang.c) */
bool arv_audio_clock(void)
{
#ifdef SIMULATION
    return false;
#else
    return running;
#endif
}

void arv_audio_rate(uint32_t hz) { t0_rate = hz; }

void arv_audio_tone(unsigned divisor)
{
    tone_step = divisor ? (PIT_HZ / divisor) * (0xFFFFFFFFu / FS_OUT) : 0u;
}

void arv_audio_lock(void)   { if (running) irq_off(); }
void arv_audio_unlock(void) { if (running) irq_on(); }

/*------------------------------- music ------------------------------------*/

void arv_music_stop(void)
{
    m_on = false;
    if (m_open)
        f_close(&m_file);
    m_open = false;
}

/* Song n (MUSnn.ADP: 'KADP', rate, samples, reserved, then IMA ADPCM, low
 * nibble first); no file: silence */
void arv_music_start(int song)
{
    char name[] = "MUS00.ADP";
    arv_music_stop();
    if (!running || song < 0 || song > 99)
        return;
    name[3] = (char) ('0' + song / 10);
    name[4] = (char) ('0' + song % 10);
    if (f_open(&m_file, name, FA_READ) != FR_OK)
        return;
    m_open = true;
    if (f_lseek(&m_file, MUS_HEADER) != FR_OK) {
        arv_music_stop();
        return;
    }
    irq_off();
    m_wr = 0;
    m_rd = 0;
    m_loop = ~0u;
    m_pred = 0;
    m_idx = 0;
    irq_on();
    m_paused = false;
    m_on = true;
}

bool arv_music_playing(void) { return m_on && !m_paused; }

/* The game's music off / on (a pause, the menu): the song stops where it is */
void arv_music_pause(bool pause) { m_paused = pause; }

/* The main loop's part: the music ring kept filled */
void arv_audio_pump(void)
{
    UINT n, m;
    while (m_on && MUS_BYTES - (m_wr - (m_rd >> 1)) >= MUS_CHUNK) {
        uint8_t *dst = mring + (m_wr & (MUS_BYTES - 1u));
        if (f_read(&m_file, dst, MUS_CHUNK, &n) != FR_OK) {
            arv_music_stop();
            return;
        }
        if (n < MUS_CHUNK) {                    /* the end: on from the start */
            m_loop = (m_wr + n) * 2u;
            if (f_lseek(&m_file, MUS_HEADER) != FR_OK || f_read(&m_file, dst + n, MUS_CHUNK - n, &m) != FR_OK) {
                arv_music_stop();
                return;
            }
            n += m;
        }
        m_wr += n;
        if (n < MUS_CHUNK)
            return;
    }
}

/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      sd_tang.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Omnispeak's sound back-end (SD_Backend) and the game clock. The game's
 * timer interrupt (timer 0 of the PC, 140 Hz, 560 Hz with AdLib music) is a
 * catch-up on the machine timer: SD_Tang_Poll, called whenever the game asks
 * the time or reads the controls, runs the timer service once per period
 * passed. The PC speaker's tone goes to the board's sound output (audio.c).
 *----------------------------------------------------------------------------*/
#include "id_sd.h"
#include "tang.h"

#define PIT_BASE 1192030u                       /* the original's timer constant */

void SDL_t0Service(void);

static uint32_t rate = 140u;                    /* timer service per second          */
static uint32_t last, period;
static bool     busy, started;

void SD_Tang_Poll(void)
{
    uint32_t now;
    if (busy)
        return;
    busy = true;
    arv_audio_pump();
    if (arv_audio_clock()) {                    /* the I2S interrupt runs the service */
        busy = false;
        return;
    }
    now = arv_mtime();
    if (!started) {
        started = true;
        last = now;
        period = arv_time_hz() / rate;
    }
    while (now - last >= period) {
        last += period;
        SDL_t0Service();
    }
    busy = false;
}

static void TANG_SetTimer0(int16_t divisor)
{
    if (divisor > 0) {
        rate = PIT_BASE / (uint16_t) divisor;
        period = arv_time_hz() / rate;
        arv_audio_rate(rate);
    }
}

static void TANG_Startup(void)  { arv_audio_start(); }
static void TANG_Shutdown(void) {}
static void TANG_Lock(void)     { arv_audio_lock(); }
static void TANG_Unlock(void)   { arv_audio_unlock(); }
static void TANG_AlOut(uint8_t reg, uint8_t val) { (void) reg; (void) val; }
static void TANG_PCSpkOn(bool on, int divisor)   { arv_audio_tone(on ? (unsigned) divisor : 0u); }

static void TANG_WaitTick(void)
{
    const uint32_t t0 = SD_GetTimeCount();
    while (SD_GetTimeCount() == t0)
        ;
}

/* AdLib "present": the music menu works, the songs come from the card (the
 * effects stay the PC speaker's, id_sd.c) */
static unsigned int TANG_Detect(void) { return SD_CARD_PC_SPEAKER | SD_CARD_OPL2; }
static void TANG_SetOPL3(bool on) { (void) on; }

static SD_Backend sd_tang_backend = {
    .startup = TANG_Startup,
    .shutdown = TANG_Shutdown,
    .lock = TANG_Lock,
    .unlock = TANG_Unlock,
    .alOut = TANG_AlOut,
    .pcSpkOn = TANG_PCSpkOn,
    .setTimer0 = TANG_SetTimer0,
    .waitTick = TANG_WaitTick,
    .detect = TANG_Detect,
    .setOPL3 = TANG_SetOPL3,
};

SD_Backend *SD_Impl_GetBackend(void)
{
    return &sd_tang_backend;
}

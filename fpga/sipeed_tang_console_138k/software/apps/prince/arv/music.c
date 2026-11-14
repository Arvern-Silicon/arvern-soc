/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      music.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The game's music (midi.c's entry points): the MIDI tunes rendered offline
 * through the OPL emulation (MUSnn.ADP, nn the game's sound number: a 16-byte
 * header then IMA ADPCM at the mixer's rate, low nibble first), streamed from
 * the card through a ring in the VRAM that the main loop keeps filled
 * (arv_music_pump, from the input poll); the mixer's callback adds it in.
 * No file: the tune is silent and ends at once.
 *----------------------------------------------------------------------------*/
#include <string.h>
#include "ff.h"
#include "common.h"

#define MUS_BYTES  VRAM_MUSIC_SIZE
#define MUS_CHUNK  1024u
#define MUS_HEADER 16u

int arv_sound_number(const sound_buffer_type *buffer);
extern short midi_playing;                      /* seg009.c */
void init_digi(void);

static uint8_t           *ring;
static volatile uint32_t  m_wr, m_rd;          /* bytes written / nibbles decoded */
static volatile uint32_t  m_end = ~0u;         /* nibbles in the tune             */
static FIL                m_file;
static bool               m_open;
static int32_t            m_pred, m_idx;

static const int16_t ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767 };
static const int8_t ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

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

void stop_midi(void)
{
    SDL_LockAudio();
    midi_playing = 0;
    SDL_UnlockAudio();
    if (m_open)
        f_close(&m_file);
    m_open = false;
}

void midi_reclaim(void) {}

void play_midi_sound(const sound_buffer_type *buffer)
{
    char name[] = "MUS00.ADP";
    uint8_t hdr[MUS_HEADER];
    UINT n;
    const int nr = arv_sound_number(buffer);
    stop_midi();
    if (nr < 0 || nr > 99)
        return;
    name[3] = (char) ('0' + nr / 10);
    name[4] = (char) ('0' + nr % 10);
    if (f_open(&m_file, name, FA_READ) != FR_OK)
        return;
    m_open = true;
    if (f_read(&m_file, hdr, MUS_HEADER, &n) != FR_OK || n != MUS_HEADER || memcmp(hdr, "KADP", 4) != 0) {
        stop_midi();
        return;
    }
    ring = arv_vram + VRAM_MUSIC;
    SDL_LockAudio();
    m_wr = 0;
    m_rd = 0;
    m_end = (uint32_t) (hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t) hdr[11] << 24);
    m_pred = 0;
    m_idx = 0;
    SDL_UnlockAudio();
    arv_music_pump();
    SDL_LockAudio();
    midi_playing = 1;
    SDL_UnlockAudio();
    init_digi();
    SDL_PauseAudio(0);
}

/* The mixer's part: the tune added to the stream (stereo int16) */
uint32_t arv_music_starved, arv_music_played;

void midi_callback(void *userdata, Uint8 *stream, int len)
{
    Sint16 *s = (Sint16 *) stream;
    int frames = len / 4;
    uint32_t r = m_rd;
    (void) userdata;
    while (frames--) {
        if (r >= m_end) {
            midi_playing = 0;
            break;
        }
        if (r >= m_wr * 2u) {                   /* starved: the card is late */
            arv_music_starved++;
            break;
        }
        arv_music_played++;
        const unsigned b = ring[(r >> 1) & (MUS_BYTES - 1u)];
        int32_t v = ima_decode((r & 1u) ? (b >> 4) : (b & 15u)) >> 1;
        r++;
        for (int c = 0; c < 2; c++, s++) {
            int32_t m = *s + v;
            *s = (Sint16) (m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
    m_rd = r;
}

/* The main loop's part: the ring kept filled (the simulator build, which
 * has no audio interrupt, also runs the mixer here, on the clock) */
void arv_music_pump(void)
{
    UINT n;
#ifdef ARV_SOFT_GFX
    {
        extern void arv_audio_frame(unsigned frames);
        static uint32_t t_q;
        const uint32_t now = arv_mtime(), per = arv_time_hz() / 22050u;
        if (per && now - t_q >= 64u * per) {
            arv_audio_frame((now - t_q) / per);
            t_q = now;
        }
    }
#endif
    while (m_open && MUS_BYTES - (m_wr - (m_rd >> 1)) >= MUS_CHUNK) {
        uint8_t *dst = ring + (m_wr & (MUS_BYTES - 1u));
        if (f_read(&m_file, dst, MUS_CHUNK, &n) != FR_OK || n == 0) {
            f_close(&m_file);
            m_open = false;
            return;
        }
        m_wr += n;
        if (n < MUS_CHUNK) {
            f_close(&m_file);
            m_open = false;
            return;
        }
    }
}

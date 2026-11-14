//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// audio_player -- plays the WAV files of the microSD card's /MEDIA/MUSIC
// folder in name order, in a loop, on the sound output the boot menu selected
// (board I/O AUDIO_OUT: HDMI, the onboard speaker or a Pmod I2S2 on PMOD1).
//
// Files (codec.c, flac.c): RIFF / WAVE PCM (also WAVE_FORMAT_EXTENSIBLE with
// a PCM sub-format), 8 or 16 bits, or IMA ADPCM (format 0x11); QOA (.QOA);
// FLAC (.FLA, .FLAC), 8 to 24 bits; MP3 (.MP3, mp3.c), MPEG-1 Layer III;
// mono or stereo, any rate up to 96 kHz;
// tracker modules mixed at 44.1 kHz (trk.c): ProTracker (.MOD, mod.c), 4 to
// 8 channels, Scream Tracker 3 (.S3M, s3m.c), up to 16 channels, and
// FastTracker 2 (.XM, xm.c), up to 32 channels.
// A WAV's LIST INFO, a FLAC's Vorbis comment, a module's title give the
// title and artist. Other files are ignored.
//
// A module is loaded whole, into the memory nothing else uses while it
// plays: the FLAC decoder's buffers, the program memory after the program
// image (below the boot menu's launch routine, its last 256 bytes), and the
// VRAM's second frame (the screen draws in the first).
//
// Playback: the I2S master runs at a fixed 44.08 kHz (MCLK / LRCK ratio 384,
// the Pmod's D/A needs MCLK). Its TX_LOW interrupt (PLIC source 17, FIFO at
// or below TX_THR words) refills the 16-word FIFO from a ring of source
// frames, resampling with a Q16 phase step (file rate / output rate) and
// linear interpolation (a rate within 0.1 % of the output's, 44.1 kHz,
// copied as it is: 0.8 cent low); the main loop reads the file into the ring
// (FatFs on the SD layer of apb_spi) and polls the key.
//
// Screen (screen.c, on Belenos): the title (the file's LIST INFO name and
// artist, else its file name), a spectrum of the stream (spectrum.c: the
// interrupt keeps its last CAP_N output samples, a 512-point FFT 30 times a
// second) and the progress, time, CPU load and sound output. The load is the
// share of time in the interrupt, in the sample conversion and in the
// spectrum and drawing (not in card reads or refresh waits).
//
// CONTROLS : KEY[1] pressed briefly = next track; held 3/4 s = the next
// sound output (HDMI, speaker, Pmod I2S2; no HDMI in the panel build)
// (KEY[0] = reset). A USB gamepad or keyboard (either port), as in the boot
// menu: right / left (arrows) next / previous track (the start of the track
// after its first 3 s), L / R (- / =) volume, A or Start (Space, Enter)
// pause, X (F3) sound output. The volume (0 to 8, 3 dB a step, 8 the samples
// as they are) starts at the boot menu's (/APPS/LAST.TXT), else at 8.
// LED register: [7:4] track number, [3] the ring ran dry during this track,
// [2:1] the sound output (0 HDMI, 1 speaker, 2 Pmod I2S2), [0] toggles every
// 1/4 s of playback; [1:0] blink in turn on an error (no card, no folder, no
// WAV file), retried every 2 s.
// The status block 'player' (symbol) holds the counters for a debugger.
//
// Simulation (-DSIMULATION): RATIO 256 (65.1 kHz) and the long press sixteen
// times shorter; src/audio_player.v puts a small FAT image on the card.
//----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "apb_i2s.h"
#include "ahb_plic.h"
#include "ff.h"
#include "demo_kit.h"
#include "screen.h"
#include "spectrum.h"
#include "codec.h"
#include "flac.h"
#include "mod.h"
#include "s3m.h"
#include "xm.h"
#include "mp3.h"
#include "usb_hid.h"

#ifdef SIMULATION
#define RATIO        256u
#define PRESS_SCALE  16u
#else
#define RATIO        384u
#define PRESS_SCALE  1u
#endif
#define FS_OUT       (BOARD_HCLK_HZ / (2u * RATIO))
#define LONG_PRESS   (FS_OUT * 3u / (4u * PRESS_SCALE))  // output samples
#define TX_THR       4u                     // FIFO refill threshold, words: 12 frames an interrupt, 90 us to answer
#define RING_N       8192u                  // source frames (32 KB)
#define CHUNK        4096u                  // bytes per file read
#define MAX_TRACKS   64u
#define MUSIC_DIR    "/MEDIA/MUSIC"
#define CAP_N        1024u                  // output samples kept for the spectrum
#define DRAW_EVERY   (FS_OUT / 30u)         // output samples between frames

//----------------------------------------------------------------------------
// Status block, read by a debugger
//----------------------------------------------------------------------------

typedef struct {
    uint32_t magic;                         // 'APLY'
    uint32_t state;                         // 0 mount, 1 scan, 2 play, 3 error
    uint32_t error;                         // FatFs FRESULT, or 100 + reason
    uint32_t tracks, track;
    uint32_t rate, channels, bits;          // the current file
    uint32_t played;                        // output samples since reset
    uint32_t starved;                       // output samples the ring could not feed
    uint32_t read_max;                      // worst file read, in output samples
    uint32_t files_played;
    uint32_t underruns;                     // I2S FIFO underruns (the interrupt late)
    uint32_t dec_ticks, dec_frames;         // this track: mtime ticks decoding, frames decoded
    uint32_t isr_ticks, fft_ticks, draw_ticks;  // since reset: the interrupt, the spectrum, the drawing
} player_t;

volatile player_t player = { .magic = 0x594C5041u };

//----------------------------------------------------------------------------
// Devices
//----------------------------------------------------------------------------

static apb_i2s_t  i2s;
static plic_t     plic;
static unsigned   plic_ctx;

static void say(const char *s)
{
    while (*s)
        lks_tty_putc(&lks, *s++);
}

static void say_num(uint32_t v)
{
    char d[11];
    unsigned n = 0u;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        lks_tty_putc(&lks, d[--n]);
}

static inline void irq_off(void) { __asm__ volatile ("csrc mstatus, 8"); }
static inline void irq_on(void)  { __asm__ volatile ("csrs mstatus, 8"); }

//----------------------------------------------------------------------------
// Ring of source frames ([15:0] left, [31:16] right, the I2S word layout):
// the main loop writes at wr, the interrupt reads at rd (free-running counts)
//----------------------------------------------------------------------------

static volatile uint32_t ring[RING_N];
static volatile uint32_t wr, rd;
static volatile bool     active;            // a track is feeding the ring
static uint32_t          phase, step;       // Q16 position between rd and rd + 1
static int16_t           cap[CAP_N];        // the output's last samples (left + right) / 2
static volatile uint32_t cap_w;
static volatile uint32_t busy;              // mtime ticks of work (see the header)
static volatile bool     paused;            // the interrupt sends silence, the ring waits
static volatile int32_t  gain = 256;        // the volume, Q8 (8: the samples as they are)
static unsigned          volume = 8u;       // 0 .. 8
static const int16_t     vol_gain[9] = { 0, 23, 32, 45, 64, 91, 128, 181, 256 };

static inline unsigned ring_level(void) { return wr - rd; }

// Fill the transmit FIFO, one resampled frame per free word (the shared
// state read once: the ring's frames up to wr are complete once wr is read)
static void feed(void)
{
    unsigned n = i2s.tx_depth - apb_i2s_tx_level(&i2s);
    const uint32_t w = wr;
    __asm__ volatile ("" ::: "memory");
    const uint32_t *rg = (const uint32_t *) ring;
    volatile uint32_t *tx = &i2s.regs->TXDATA;
    uint32_t r = rd, ph = phase, cw = cap_w;
    const uint32_t st = step;
    const int32_t g = gain;
    const bool pz = paused, act = active;
    player.played += n;
    if (st == 0x10000u && !pz && w - r >= n) {              // the source's own rate: copies
        for (; n; n--, r++) {
            const uint32_t a = rg[r % RING_N];
            const int32_t  l = (int16_t) a, rr = (int16_t) (a >> 16);
            cap[cw++ % CAP_N] = (int16_t) ((l + rr) >> 1);
            *tx = ((uint32_t) (uint16_t) ((rr * g) >> 8) << 16) | (uint16_t) ((l * g) >> 8);
        }
    }
    while (n--) {
        uint32_t out = 0u;
        int32_t  c = 0;
        if (!pz && w - r >= 2u) {
            const uint32_t a = rg[r % RING_N], b = rg[(r + 1u) % RING_N];
            const int32_t  f = (int32_t) (ph >> 1);              // Q15
            const int32_t  al = (int16_t) a, bl = (int16_t) b;
            const int32_t  ar = (int16_t) (a >> 16), br = (int16_t) (b >> 16);
            const int32_t  l  = al + (((bl - al) * f) >> 15);
            const int32_t  rr = ar + (((br - ar) * f) >> 15);
            c   = (l + rr) >> 1;                                // the spectrum before the volume
            out = ((uint32_t) (uint16_t) ((rr * g) >> 8) << 16) | (uint16_t) ((l * g) >> 8);
            ph += st;
            r  += ph >> 16;
            ph &= 0xFFFFu;
        } else if (!pz && act) {
            player.starved++;
        }
        cap[cw++ % CAP_N] = (int16_t) c;
        *tx = out;
    }
    rd    = r;
    phase = ph;
    cap_w = cw;
    if (apb_i2s_irq_pending(&i2s) & APB_I2S_IRQ_TX_UNDER) {
        apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_TX_UNDER);
        player.underruns++;
    }
}

void __attribute__((interrupt("machine"), aligned(4))) trap_handler(void)
{
    const uint32_t t0 = aclint_mtime_lo(&aclint);
    uint32_t id;
    while ((id = plic_claim(&plic, plic_ctx)) != 0u) {
        if (id == BOARD_PLIC_SRC_I2S)
            feed();
        (void) plic_complete(&plic, plic_ctx, id);
    }
    const uint32_t dt = aclint_mtime_lo(&aclint) - t0;
    busy += dt;
    player.isr_ticks += dt;
}

//----------------------------------------------------------------------------
// Output: the stream's destination (board I/O AUDIO_OUT), switched with the
// I2S master stopped
//----------------------------------------------------------------------------

static lks_audio_t aout;

static void set_output(lks_audio_t out)
{
    static const char *const name[4] = { "HDMI", "speaker", "Pmod I2S2 (PMOD1)", "none" };
    static const char *const shown[4] = { "HDMI", "Spkr", "Pmod", "-   " };
    aout = out;
    apb_i2s_irq_enable(&i2s, 0u);
    apb_i2s_stop(&i2s);
    apb_i2s_flush(&i2s, true, false);
    lks_audio_output(&lks, out);
    apb_i2s_start(&i2s, true, false);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
    say("output: ");
    say(name[out & 3u]);
    say("\n");
    screen_sound(shown[out & 3u], volume);
}

// HDMI, speaker, Pmod I2S2, in turn (the panel build has no HDMI)
static void next_output(void)
{
    set_output((aout == LKS_AUDIO_SPEAKER) ? LKS_AUDIO_PMOD
             : (aout == LKS_AUDIO_PMOD && BOARD_HDMI()) ? LKS_AUDIO_HDMI : LKS_AUDIO_SPEAKER);
}

//----------------------------------------------------------------------------
// Key and LEDs, polled by the main loop
//----------------------------------------------------------------------------

typedef enum { KEY_NONE, KEY_SHORT, KEY_LONG } press_t;

static uint32_t key_t0;
static bool     key_down, key_long;

static press_t key_poll(void)
{
    bool down = lks_key_pressed(&lks, 1u);
    press_t k = KEY_NONE;
    if (down && !key_down) {
        key_t0   = player.played;
        key_long = false;
    } else if (down && !key_long && (player.played - key_t0 >= LONG_PRESS)) {
        key_long = true;
        k = KEY_LONG;
    } else if (!down && key_down && !key_long) {
        k = KEY_SHORT;
    }
    key_down = down;
    return k;
}

static void leds(bool starved)
{
    uint32_t v = ((player.track & 15u) << 4) | (starved ? 8u : 0u) | ((aout & 3u) << 1) |
                 ((player.played / (FS_OUT / 4u)) & 1u);
    lks_led_set(&lks, v);
}

// Error: the reason on the TTY and the screen, [1:0] blinking in turn for
// 2 s (the output switch still works)
static void error_wait(const char *what, uint32_t code)
{
    uint32_t t0 = player.played;
    screen_message(what, "retrying...");
    player.state = 3u;
    player.error = code;
    say("audio_player: ");
    say(what);
    say(" (");
    say_num(code);
    say(")\n");
    while (player.played - t0 < 2u * FS_OUT) {
        uint32_t ph = (player.played / (FS_OUT / 4u)) & 1u;
        lks_led_set(&lks, ph ? 2u : 1u);
        if (key_poll() == KEY_LONG)
            next_output();
    }
}

//----------------------------------------------------------------------------
// Playlist and WAV files
//----------------------------------------------------------------------------

static FATFS    fs;
static FIL      file;
static char     names[MAX_TRACKS][13];
static uint8_t  buf[CHUNK + CODEC_UNIT_MAX] __attribute__((aligned(4)));
static uint32_t frames_total;               // the track's frames
static char     title[48], artist[48];      // the file's LIST INFO INAM / IART

// The file name's extension, any case: ".wav" -> 'w', ".qoa" -> 'q',
// ".fla" or ".flac" -> 'f', ".mp3" -> 'p', ".mod" -> 'm', ".s3m" -> 's', ".xm" -> 'x', else 0
static char kind(const char *n)
{
    size_t l = strlen(n);
    char   e[6] = { 0 };
    if ((n[0] == '.') || (l <= 4u))
        return 0;
    const char *d = strrchr(n, '.');
    if (!d || (strlen(d) > 5u))
        return 0;
    for (unsigned i = 0; d[i]; i++)
        e[i] = (char) (d[i] | 0x20);
    return !strcmp(e, ".wav") ? 'w' : !strcmp(e, ".qoa") ? 'q' : (!strcmp(e, ".fla") || !strcmp(e, ".flac")) ? 'f'
         : !strcmp(e, ".mp3") ? 'p' : !strcmp(e, ".mod") ? 'm' : !strcmp(e, ".s3m") ? 's' : !strcmp(e, ".xm") ? 'x' : 0;
}

static FRESULT scan(void)
{
    DIR     dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, MUSIC_DIR);
    unsigned n = 0u, i, j;
    if (fr != FR_OK)
        return fr;
    while ((n < MAX_TRACKS) && (f_readdir(&dir, &fno) == FR_OK) && fno.fname[0]) {
        if (!(fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) && kind(fno.fname)) {
            for (i = n; (i > 0u) && (strcmp(names[i - 1u], fno.fname) > 0); i--)
                memcpy(names[i], names[i - 1u], 13u);
            for (j = 0u; j < 13u; j++)
                names[i][j] = fno.fname[j];
            n++;
        }
    }
    (void) f_closedir(&dir);
    player.tracks = n;
    return FR_OK;
}

static uint32_t le16(const uint8_t *p) { return p[0] | ((uint32_t) p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

// The sub-chunks of a LIST INFO chunk up to 'end': the title and artist
static void info(FSIZE_t end)
{
    uint8_t h[8];
    UINT    got;
    while (f_tell(&file) + 8u <= end) {
        if ((f_read(&file, h, 8u, &got) != FR_OK) || (got != 8u))
            return;
        const uint32_t size = le32(h + 4);
        const FSIZE_t  next = f_tell(&file) + size + (size & 1u);
        char *const dst = !memcmp(h, "INAM", 4u) ? title : !memcmp(h, "IART", 4u) ? artist : 0;
        if (dst) {
            const UINT n = (size < sizeof(title) - 1u) ? size : sizeof(title) - 1u;
            if ((f_read(&file, dst, n, &got) != FR_OK))
                return;
            dst[got] = 0;
            for (int i = (int) got - 1; i >= 0 && ((unsigned char) dst[i] <= ' '); i--)
                dst[i] = 0;
        }
        if ((next > end) || (f_lseek(&file, next) != FR_OK))
            return;
    }
}

// Walk the RIFF chunks to "data": format checked, the file left at the first
// sample; the data size, or 0 when the file cannot be played
static uint32_t wav_open(void)
{
    uint8_t  h[40];
    UINT     got;
    unsigned k;
    bool     fmt_ok = false;
    if ((f_read(&file, h, 12u, &got) != FR_OK) || (got != 12u) ||
        memcmp(h, "RIFF", 4u) || memcmp(h + 8, "WAVE", 4u))
        return 0u;
    for (k = 0u; k < 32u; k++) {
        uint32_t size;
        if ((f_read(&file, h, 8u, &got) != FR_OK) || (got != 8u))
            return 0u;
        size = le32(h + 4);
        if (!memcmp(h, "data", 4u)) {
            if (!fmt_ok)
                return 0u;
            frames_total = (codec.fmt == CODEC_ADPCM)
                ? (size / codec.block) * codec_adpcm_frames(codec.block) + codec_adpcm_frames(size % codec.block)
                : size / ((player.bits / 8u) * player.channels);
            return size;
        }
        if (!memcmp(h, "LIST", 4u) && (size >= 4u)) {
            const FSIZE_t next = f_tell(&file) + size + (size & 1u);
            if ((f_read(&file, h, 4u, &got) == FR_OK) && (got == 4u) && !memcmp(h, "INFO", 4u))
                info(next);
            if (f_lseek(&file, next) != FR_OK)
                return 0u;
        } else if (!memcmp(h, "fmt ", 4u) && (size >= 16u)) {
            FSIZE_t next = f_tell(&file) + size + (size & 1u);
            uint32_t tag;
            if ((f_read(&file, h, (size < 40u) ? size : 40u, &got) != FR_OK) || (got < 16u))
                return 0u;
            tag = le16(h);
            if ((tag == 0xFFFEu) && (got >= 26u))
                tag = le16(h + 24);
            player.channels = le16(h + 2);
            player.rate     = le32(h + 4);
            player.bits     = le16(h + 14);
            codec.fmt       = (tag == 0x11u) ? CODEC_ADPCM : CODEC_PCM;
            codec.channels  = player.channels;
            codec.bits      = player.bits;
            codec.block     = le16(h + 12);
            fmt_ok = ((player.channels == 1u) || (player.channels == 2u)) &&
                     (player.rate >= 1000u) && (player.rate <= 96000u) &&
                     (((tag == 1u) && ((player.bits == 8u) || (player.bits == 16u))) ||
                      ((tag == 0x11u) && (player.bits == 4u) &&
                       (codec.block > 4u * player.channels) && (codec.block <= CODEC_UNIT_MAX)));
            if (f_lseek(&file, next) != FR_OK)
                return 0u;
        } else if (f_lseek(&file, f_tell(&file) + size + (size & 1u)) != FR_OK) {
            return 0u;
        }
    }
    return 0u;
}

// A QOA file: its header and first frame header checked, the file left at
// the first frame; the bytes from there, or 0 when it cannot be played
static uint32_t qoa_open(void)
{
    uint8_t h[16];
    UINT    got;
    if ((f_read(&file, h, 16u, &got) != FR_OK) || (got != 16u) || memcmp(h, "qoaf", 4u))
        return 0u;
    frames_total    = ((uint32_t) h[4] << 24) | ((uint32_t) h[5] << 16) | ((uint32_t) h[6] << 8) | h[7];
    player.channels = h[8];
    player.rate     = ((uint32_t) h[9] << 16) | ((uint32_t) h[10] << 8) | h[11];
    player.bits     = 16u;
    codec.fmt       = CODEC_QOA;
    codec.channels  = player.channels;
    if (!frames_total || ((player.channels != 1u) && (player.channels != 2u)) ||
        (player.rate < 1000u) || (player.rate > 96000u) || (f_lseek(&file, 8u) != FR_OK))
        return 0u;
    return (uint32_t) (f_size(&file) - 8u);
}

// FLAC: the stream's bytes from the file; a skip by a seek
static unsigned flac_read(uint8_t *d, unsigned n)
{
    UINT got;
    return (f_read(&file, d, n, &got) == FR_OK) ? got : 0u;
}

static int flac_skip(uint32_t n)
{
    return f_lseek(&file, f_tell(&file) + n) != FR_OK;
}

static unsigned flac_block;                 // the stream's largest block

static uint32_t flac_open_file(void)
{
    flac_info_t in;
    if (flac_open(flac_read, flac_skip, buf, sizeof(buf), &in, title, artist, sizeof(title)))
        return 0u;
    player.rate     = in.rate;
    player.channels = in.channels;
    player.bits     = in.bps;
    codec.fmt       = CODEC_FLAC;
    codec.channels  = in.channels;
    frames_total    = in.frames;
    flac_block      = in.block_max;
    return ((in.rate >= 1000u) && (in.rate <= 96000u)) ? 1u : 0u;
}

// MP3: its working memory in the FLAC decoder's buffers
static uint32_t mp3_open_file(void)
{
    mp3_info_t in;
    uint32_t   fsz;
    void *mem = flac_buffers(&fsz);
    if (fsz < MP3_MEM || mp3_open(flac_read, flac_skip, (uint32_t) f_size(&file), mem, &in, title, artist, sizeof(title)))
        return 0u;
    player.rate     = in.rate;
    player.channels = in.channels;
    player.bits     = 16u;
    codec.fmt       = CODEC_MP3;
    codec.channels  = 2u;
    frames_total    = in.frames;
    return 1u;
}

// A module: loaded whole into the free memory areas, fastest first
extern uint8_t __rom_end[];
#define ROM_TOP     (0x20000000u + 160u * 1024u - 256u)
static char mod_format[8];

static unsigned mod_read(uint8_t *d, unsigned n)
{
    UINT got;
    return (f_read(&file, d, n, &got) == FR_OK) ? got : 0u;
}

static int mod_seek(uint32_t at)
{
    return f_lseek(&file, at) != FR_OK;
}

// A module (k: 'm' MOD, 's' S3M, 'x' XM): loaded and started in the mixer
static uint32_t mod_open(char k)
{
    trk_area_t a[3];
    trk_info_t in;
    uint32_t   fsz;
    a[0].base = flac_buffers(&fsz);
    a[0].size = fsz;
    a[1].base = (uint8_t *) (((uintptr_t) __rom_end + 3u) & ~(uintptr_t) 3u);
    a[1].size = ROM_TOP - (uint32_t) (uintptr_t) a[1].base;
    a[2].base = (uint8_t *) (uintptr_t) (blns_vram8(&blns) + FRAME_PX);
    a[2].size = FRAME_PX;
    const uint32_t fs = (uint32_t) f_size(&file);
    const int e = (k == 's') ? s3m_load(mod_read, mod_seek, fs, a, 3u, &in)
                : (k == 'x') ? xm_load(mod_read, fs, a, 3u, &in)
                             : mod_load(mod_read, fs, a, 3u, &in);
    if (e)
        return 0u;
    player.rate     = TRK_RATE;
    player.channels = 2u;
    player.bits     = 16u;
    codec.fmt       = CODEC_MOD;
    codec.channels  = 2u;
    frames_total    = in.frames;
    memcpy(title, in.title, sizeof(in.title));
    for (int i = (int) strlen(title) - 1; i >= 0 && title[i] == ' '; i--)
        title[i] = 0;
    const unsigned l = (k == 'x') ? 2u : 3u;
    memcpy(mod_format, (k == 's') ? "S3M" : (k == 'x') ? "XM" : "MOD", l);
    mod_format[l] = (char) ((in.channels >= 10u) ? '0' + in.channels / 10u : '0' + in.channels);
    mod_format[l + 1u] = (char) ((in.channels >= 10u) ? '0' + in.channels % 10u : 0);
    mod_format[l + 2u] = 0;
    return 1u;
}

//----------------------------------------------------------------------------
// Gamepad and keyboard (the boot menu's mapping)
//----------------------------------------------------------------------------

static usb_hid_t usb[2];
static uint32_t  pad_q;
static unsigned  dev_q = ~0u;

#define PAD_NEXT    USB_BTN_RIGHT
#define PAD_PREV    USB_BTN_LEFT
#define PAD_VOL_DN  USB_BTN_L
#define PAD_VOL_UP  USB_BTN_R
#define PAD_PAUSE   (USB_BTN_A | USB_BTN_START)
#define PAD_OUTPUT  USB_BTN_X

// The buttons pressed since the last call; the help line follows the devices
static uint32_t pad_press(void)
{
    uint32_t b = 0;
    unsigned dev = 0;
    for (unsigned i = 0; i < 2u; i++) {
        (void) usb_hid_service(&usb[i]);
        const usb_hid_type_t t = usb_hid_type(&usb[i]);
        if (t == USB_HID_GAMEPAD) {
            dev |= 1u;
            b |= usb_hid_buttons(&usb[i]);
        } else if (t == USB_HID_KEYBOARD) {
            uint8_t k[USB_HID_KEYS];
            dev |= 2u;
            usb_hid_keys(&usb[i], k);
            for (unsigned j = 0; j < USB_HID_KEYS; j++) {
                if (k[j] == 0x4Fu) b |= PAD_NEXT;                       // right arrow
                if (k[j] == 0x50u) b |= PAD_PREV;                       // left arrow
                if (k[j] == 0x28u || k[j] == 0x58u || k[j] == 0x2Cu)   // Enter, keypad Enter, Space
                    b |= USB_BTN_A;
                if (k[j] == 0x3Cu) b |= PAD_OUTPUT;                     // F3
                if (k[j] == 0x2Du || k[j] == 0x56u) b |= PAD_VOL_DN;    // -, keypad -
                if (k[j] == 0x2Eu || k[j] == 0x57u) b |= PAD_VOL_UP;    // =, keypad +
            }
        }
    }
    if (dev != dev_q) {
        dev_q = dev;
        screen_help((dev & 1u) ? "</>: track  A: pause  L/R: volume  X: output"
                  : (dev & 2u) ? "</>: track  Space: pause  -/+: volume  F3: output"
                               : "KEY1: next track, held: sound output");
    }
    const uint32_t press = b & ~pad_q;
    pad_q = b;
    return press;
}

static void set_volume(unsigned v)
{
    volume = (v > 8u) ? 8u : v;
    gain = vol_gain[volume];
    screen_sound((aout == LKS_AUDIO_SPEAKER) ? "Spkr" : (aout == LKS_AUDIO_PMOD) ? "Pmod" : "HDMI", volume);
}

// The boot menu's volume ("volume=N" in /APPS/LAST.TXT)
static void menu_volume(void)
{
    FIL  f;
    char t[64];
    UINT got;
    if (f_open(&f, "/APPS/LAST.TXT", FA_READ) != FR_OK)
        return;
    if (f_read(&f, t, sizeof(t) - 1u, &got) == FR_OK) {
        t[got] = 0;
        const char *v = strstr(t, "volume=");
        if (v && v[7] >= '0' && v[7] <= '8')
            set_volume((unsigned) (v[7] - '0'));
    }
    (void) f_close(&f);
}

typedef enum { END_FILE, END_SKIP, END_PREV, END_AGAIN, END_ERROR } end_t;

// Play the open file from its first sample: read into the ring while it has
// room, poll the key; at the end of the data, wait for the ring to drain
static end_t play(uint32_t left)
{
    unsigned carry = 0u;
    bool     starved_seen = false;
    uint32_t starved0 = player.starved;
    const uint32_t total_s = frames_total / player.rate;
    const uint32_t start = player.played;
    uint32_t drawn = start;
    uint32_t load_t0 = aclint_mtime_lo(&aclint), load_b0 = busy;
    unsigned load = 0u;
    static int16_t x[SPEC_N];
    static uint8_t level[SPEC_BANDS];

    irq_off();
    rd = wr;
    const uint32_t rd0 = wr;
    player.dec_ticks = player.dec_frames = 0u;
    phase = 0u;
    step = (uint32_t) (((uint64_t) player.rate << 16) / FS_OUT);
    if (step > 0xFFC0u && step < 0x10040u)                 // within 0.1 %: played as it is (44.1 kHz: 0.8 cent low)
        step = 0x10000u;
    irq_on();

    for (;;) {
        press_t k = key_poll();
        if (k == KEY_SHORT)
            return END_SKIP;
        if (k == KEY_LONG)
            next_output();
        const uint32_t pb = pad_press();
        if (pb & PAD_NEXT) {
            paused = false;
            return END_SKIP;
        }
        if (pb & PAD_PREV) {
            paused = false;
            return ((rd - rd0) >= 3u * player.rate) ? END_AGAIN : END_PREV;
        }
        if (pb & PAD_PAUSE)
            paused = !paused;
        if ((pb & PAD_VOL_DN) && volume)
            set_volume(volume - 1u);
        if (pb & PAD_VOL_UP)
            set_volume(volume + 1u);
        if (pb & PAD_OUTPUT)
            next_output();
        starved_seen |= (player.starved != starved0);
        leds(starved_seen);

        // a frame once the ring holds a reserve: a track starts at once
        if ((player.played - drawn >= DRAW_EVERY) && (!left || ring_level() >= RING_N / 4u)) {
            drawn = player.played;
            wait_frames(1u);
            const uint32_t t0 = aclint_mtime_lo(&aclint);
            const uint32_t w  = cap_w;
            for (unsigned i = 0; i < SPEC_N; i++)
                x[i] = cap[(w - SPEC_N + i) % CAP_N];
            spectrum_run(x, level);
            const uint32_t tf = aclint_mtime_lo(&aclint);
            screen_frame(level, (rd - rd0) / player.rate, total_s, load, paused);
            const uint32_t t1 = aclint_mtime_lo(&aclint);
            player.fft_ticks  += tf - t0;
            player.draw_ticks += t1 - tf;
            irq_off();
            busy += t1 - t0;
            irq_on();
            if (t1 - load_t0 >= BOARD_TIME_HZ) {    // the load over the last second
                load = (unsigned) (((uint64_t) (busy - load_b0) * 100u) / (t1 - load_t0));
                load_t0 = t1;
                load_b0 = busy;
            }
        }

        if (left && (codec.fmt == CODEC_MOD)) {
            // a module: 1024 frames when the ring has room
            if (RING_N - 2u - ring_level() >= 1024u) {
                const uint32_t tc = aclint_mtime_lo(&aclint);
                sink_t   sink = { ring, RING_N - 1u, &wr };
                const unsigned n = trk_render(&sink, 1024u);
                const uint32_t dc = aclint_mtime_lo(&aclint) - tc;
                player.dec_ticks += dc;
                player.dec_frames = wr - rd0;
                irq_off();
                busy += dc;
                irq_on();
                if (n < 1024u)
                    left = 0u;
                if (wr != rd)
                    active = (left != 0u);
            }
        } else if (left && (codec.fmt == CODEC_MP3)) {
            // MP3 pulls its bytes itself: a frame when the ring has room
            if (RING_N - 2u - ring_level() >= MP3_FRAME) {
                const uint32_t tc = aclint_mtime_lo(&aclint);
                sink_t   sink = { ring, RING_N - 1u, &wr };
                const int n = mp3_frame(&sink);
                const uint32_t dc = aclint_mtime_lo(&aclint) - tc;
                player.dec_ticks += dc;
                player.dec_frames = wr - rd0;
                irq_off();
                busy += dc;
                irq_on();
                if (n < 0)
                    say("MP3: a frame that cannot be decoded, the rest skipped\n");
                if (n <= 0)
                    left = 0u;
                if (wr != rd)
                    active = (left != 0u);
            }
        } else if (left && (codec.fmt == CODEC_FLAC)) {
            // FLAC pulls its bytes itself: a frame when the ring has room
            if (RING_N - 2u - ring_level() >= flac_block) {
                const uint32_t tc = aclint_mtime_lo(&aclint);
                sink_t   sink = { ring, RING_N - 1u, &wr };
                const int n = flac_frame(&sink);
                const uint32_t dc = aclint_mtime_lo(&aclint) - tc;
                player.dec_ticks += dc;
                player.dec_frames = wr - rd0;
                irq_off();
                busy += dc;
                irq_on();
                if (n < 0)
                    say("FLAC: a frame that cannot be decoded, the rest skipped\n");
                if (n <= 0)
                    left = 0u;
                if (wr != rd)
                    active = (left != 0u);
            }
        } else if (left) {
            // a read whose decoded frames fit the ring's room
            const unsigned room = RING_N - 2u - ring_level();
            unsigned want = CHUNK - (unsigned) (f_tell(&file) % 512u);  // then sector-aligned reads
            if (want > left)
                want = left;
            while (want && (codec_frames_max(carry + want) > room))
                want = (want > 512u) ? want - 512u : 0u;
            if (want) {
                UINT     got;
                uint32_t t0 = player.played, dt;
                if ((f_read(&file, buf + carry, want, &got) != FR_OK) || (got == 0u))
                    return END_ERROR;
                dt = player.played - t0;
                if (dt > player.read_max)
                    player.read_max = dt;
                left -= got;
                got  += carry;
                const uint32_t tc = aclint_mtime_lo(&aclint);
                sink_t   sink = { ring, RING_N - 1u, &wr };
                const unsigned used = codec_run(buf, got, left == 0u, &sink);
                const uint32_t dc = aclint_mtime_lo(&aclint) - tc;
                player.dec_ticks += dc;
                player.dec_frames = wr - rd0;
                irq_off();
                busy += dc;
                irq_on();
                carry = got - used;
                memmove(buf, buf + used, carry);
                if (wr != rd)                       // counts from the first frame to the last read
                    active = (left != 0u);
            }
        } else if (ring_level() < 2u) {
            return END_FILE;
        }
    }
}

//----------------------------------------------------------------------------

int main(void)
{
    const apb_i2s_config_t cfg = {
        .clocks     = { .mdiv = 1u, .ratio = RATIO },
        .format     = APB_I2S_FMT_I2S,
        .mclk       = true,
        .rx_dly     = 1u,
        .tx_packing = APB_I2S_STEREO,
        .rx_packing = APB_I2S_STEREO
    };
    const plic_config_t     pcfg = { .base = BOARD_PLIC_BASE, .num_sources = BOARD_PLIC_SOURCES,
                                     .num_harts = BOARD_PLIC_HARTS, .prio_bits = BOARD_PLIC_PRIO_BITS,
                                     .su_mode = BOARD_PLIC_SU_MODE };

    screen_init();                                  // Belenos, the ACLINT and the board I/O (demo_kit)
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    spectrum_init(FS_OUT);
    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK) ||
        (apb_i2s_set_thresholds(&i2s, TX_THR, 1u) != APB_I2S_OK) ||
        (plic_init(&plic, &pcfg) != PLIC_OK) ||
        (plic_context(&plic, 0u, false, &plic_ctx) != PLIC_OK)) {
        lks_led_set(&lks, 0xE0u);
        for (;;)
            ;
    }
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 1u);
    (void) plic_set_threshold(&plic, plic_ctx, 0u);
    (void) plic_enable(&plic, plic_ctx, BOARD_PLIC_SRC_I2S);
    __asm__ volatile ("csrw mtvec, %0" :: "r" (&trap_handler));
    __asm__ volatile ("csrs mie, %0" :: "r" (1u << 11));      // MEIE
    irq_on();
    say("audio_player\n");
    set_output(lks_audio_output_get(&lks));       // the boot menu's choice, or the build's default

    for (;;) {
        FRESULT fr;
        player.state = 0u;
        screen_message("Reading the card...", "");
        if ((fr = f_mount(&fs, "", 1)) != FR_OK) {
            error_wait("no card", fr);
            continue;
        }
        static bool vol_read;
        if (!vol_read) {                            // the boot menu's volume, once
            vol_read = true;
            menu_volume();
        }
        player.state = 1u;
        if ((fr = scan()) != FR_OK) {
            error_wait("no " MUSIC_DIR " folder", fr);
            continue;
        }
        if (player.tracks == 0u) {
            error_wait("no music file in " MUSIC_DIR, 100u);
            continue;
        }
        say("tracks: ");
        say_num(player.tracks);
        say("\n");

        int      dir = 1;                           // the way an unplayable file is passed
        unsigned tries = 0;
        for (player.track = 0u; player.track < player.tracks;
             player.track = (dir > 0) ? player.track + 1u : (player.track ? player.track - 1u : player.tracks - 1u)) {
            char     path[sizeof(MUSIC_DIR) + 13u] = MUSIC_DIR "/";
            uint32_t size;
            end_t    e;
            if (++tries > player.tracks)            // nothing playable that way: on from the start
                break;
            strcat(path, names[player.track]);
            player.state = 2u;
            say("play ");
            say(names[player.track]);
            if (f_open(&file, path, FA_READ) != FR_OK) {
                say(": cannot open\n");
                continue;
            }
            title[0] = artist[0] = 0;
            const char k = kind(names[player.track]);
            size = (k == 'q') ? qoa_open() : (k == 'f') ? flac_open_file() : (k == 'p') ? mp3_open_file() : (k == 'm' || k == 's' || k == 'x') ? mod_open(k) : wav_open();
            if (size == 0u) {
                say(": not playable\n");
                (void) f_close(&file);
                continue;
            }
            say(": ");
            say_num(player.rate);
            say(" Hz, ");
            say_num(player.channels);
            say(" ch, ");
            say_num(player.bits);
            say(" bit\n");
            if (!title[0]) {                        // the file name without its extension
                memcpy(title, names[player.track], 13u);
                *strrchr(title, '.') = 0;
            }
            screen_track(title, artist, player.track + 1u, player.tracks,
                         (codec.fmt == CODEC_MOD) ? mod_format :
                         (codec.fmt == CODEC_FLAC) ? "FLAC" : (codec.fmt == CODEC_MP3) ? "MP3" : (codec.fmt == CODEC_QOA) ? "QOA" :
                         (codec.fmt == CODEC_ADPCM) ? "ADPCM" : "WAV",
                         player.rate, player.channels);
            e = play(size);
            active = false;
            (void) f_close(&file);
            if (e == END_ERROR) {
                say("read error\n");
                break;                              // remount
            }
            player.files_played++;
            tries = 0;
            if (e == END_AGAIN || e == END_PREV) {
                // the loop steps back once: the same track again, or the one before
                dir = -1;
                if (e == END_AGAIN)
                    player.track = (player.track + 1u) % player.tracks;
            } else {
                dir = 1;
            }
        }
    }
}

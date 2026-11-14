/* Sound for the Tang port, on the sound output the boot menu selected (HDMI,
 * the onboard speaker or a Pmod I2S2), mixed at 22.04 kHz and sent twice per
 * I2S frame: the music (WOLF_STREAM_MUSIC) and the sound effects
 * (WOLF_STREAM_SFX), both rendered offline and streamed from the program's
 * folder on the card.
 *
 * Music: the AdLib songs as IMA ADPCM files (MUSnn.ADP, tools/render_music.c).
 * Effects: one bank (SOUNDS.SFX, tools/render_sfx.c), each sound its digitized
 * version or its rendered AdLib effect, 8-bit unsigned at 7042 Hz.
 *
 * The main loop (arv_audio_pump, from SDL_GetTicks) keeps the buffers filled:
 * a 4 KB ADPCM ring for the music (at the end of a song it reads on from the
 * start and marks the loop point) and a 1 KB buffer per effect channel; the
 * buffers are in VRAM (platform.c). The I2S TX_LOW interrupt (PLIC source
 * BOARD_PLIC_SRC_I2S) makes one stereo sample per free FIFO word: the music
 * decoded, plus each playing channel stepped from 7042 Hz to the output rate
 * and weighted by its left and right attenuation. Effect channels: 0 the
 * player's weapons and 1 the bosses' (as wolfdigimap places them), 2 and 3
 * the rest (a free one, else the oldest). */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "tang_board.h"
#include "apb_i2s.h"
#include "ahb_plic.h"

#define RATIO        384u                   /* MCLK / LRCK: 16.93 MHz / 384 = 44.08 kHz (an HDMI audio rate) */
#define FS_OUT       (BOARD_HCLK_HZ / (4u * RATIO))   /* mixing rate, 22.04 kHz: each sample sent twice */
#define TX_THR       8u                     /* FIFO refill threshold, words        */
#define MUS_BYTES    4096u                  /* music ADPCM ring, a power of two    */
#define MUS_CHUNK    1024u
#define MUS_HEADER   16u                    /* "WADP", rate, samples, reserved     */
#define MUS_VOLUME   160                    /* of 256                              */
#define SFX_CH       4u
#define SFX_BYTES    1024u                  /* per channel, a power of two         */
#define SFX_CHUNK    512u
#define SFX_RATE     7042u
#define SFX_STEP     ((uint32_t) (((uint64_t) SFX_RATE << 16) / FS_OUT))    /* Q16 source steps per output sample */
#define SFX_SOUNDS   87u

uint8_t *arv_audio_buffers (void);          /* platform.c: MUS_BYTES + SFX_CH * SFX_BYTES of VRAM */

static apb_i2s_t  i2s;
static plic_t     plic;
static unsigned   plic_ctx;

/* Music */
static uint8_t           *mring;
static volatile uint32_t  m_wr;             /* bytes written (main loop)           */
static volatile uint32_t  m_rd;             /* nibbles decoded (interrupt)         */
static volatile uint32_t  m_loop = ~0u;     /* the nibble where a loop starts again */
static volatile bool      m_on;
static int                m_fd = -1;
static int32_t            m_pred, m_idx;
uint32_t                  arv_music_starved, arv_music_song = ~0u;

/* Effects */
typedef struct {
    uint8_t           *buf;
    volatile uint32_t  wr, rd;              /* bytes written / consumed            */
    volatile uint32_t  len;                 /* the sound's length                  */
    uint32_t           pos;                 /* the next byte to read from the bank */
    volatile uint32_t  phase;               /* Q16 between rd and rd + 1           */
    volatile int32_t   vl, vr;              /* weights 0..128                      */
    volatile bool      on;
    int                sound;
    uint32_t           start;               /* start order, for stealing the oldest */
} sfx_t;
static sfx_t    ch[SFX_CH];
static int      s_fd = -1;
static bool     s_loaded;
static uint32_t s_off[SFX_SOUNDS], s_len[SFX_SOUNDS], s_clock;
static int      s_last = -1, s_last_ch = -1;
uint32_t        arv_sfx_starved, arv_sfx_played;

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

static inline int32_t clip16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

/* One stereo sample per two free FIFO words (sent twice) */
static void feed(void)
{
    unsigned n = (i2s.tx_depth - apb_i2s_tx_level(&i2s)) / 2u;
    uint32_t r = m_rd, avail = m_on ? (m_wr * 2u) : r;
    while (n--) {
        int32_t l = 0, rr;
        unsigned c;
        if (r < avail) {
            if (r == m_loop) { m_pred = 0; m_idx = 0; }
            unsigned byte = mring[(r >> 1) & (MUS_BYTES - 1u)];
            l = (ima_decode((r & 1u) ? (byte >> 4) : (byte & 15u)) * MUS_VOLUME) >> 8;
            r++;
        } else if (m_on) {
            arv_music_starved++;
        }
        rr = l;
        for (c = 0; c < SFX_CH; c++) {
            sfx_t *s = &ch[c];
            if (!s->on)
                continue;
            if (s->rd >= s->len) { s->on = false; continue; }
            if (s->rd >= s->wr) { arv_sfx_starved++; continue; }
            int32_t v = (int32_t) s->buf[s->rd & (SFX_BYTES - 1u)] - 128;
            l  += v * s->vl;
            rr += v * s->vr;
            s->phase += SFX_STEP;
            s->rd += s->phase >> 16;
            s->phase &= 0xFFFFu;
        }
        const uint32_t w = ((uint32_t) (uint16_t) clip16(rr) << 16) | (uint16_t) clip16(l);
        i2s.regs->TXDATA = w;
        i2s.regs->TXDATA = w;
    }
    m_rd = r;
}

void __attribute__((interrupt("machine"), aligned(4))) arv_trap_handler(void)
{
    uint32_t id;
    while ((id = plic_claim(&plic, plic_ctx)) != 0u) {
        if (id == BOARD_PLIC_SRC_I2S)
            feed();
        (void) plic_complete(&plic, plic_ctx, id);
    }
}

/* The I2S master (on the sound output the boot menu selected), its interrupt
 * on; 0 when ready */
int arv_audio_init(void)
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
    uint8_t *b = arv_audio_buffers();
    unsigned c;

    mring = b;
    for (c = 0; c < SFX_CH; c++)
        ch[c].buf = b + MUS_BYTES + c * SFX_BYTES;
    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK) ||
        (apb_i2s_set_thresholds(&i2s, TX_THR, 1u) != APB_I2S_OK) ||
        (plic_init(&plic, &pcfg) != PLIC_OK) ||
        (plic_context(&plic, 0u, false, &plic_ctx) != PLIC_OK))
        return -1;
    (void) plic_set_priority(&plic, BOARD_PLIC_SRC_I2S, 1u);
    (void) plic_set_threshold(&plic, plic_ctx, 0u);
    (void) plic_enable(&plic, plic_ctx, BOARD_PLIC_SRC_I2S);
    __asm__ volatile ("csrw mtvec, %0" :: "r" (&arv_trap_handler));
    __asm__ volatile ("csrs mie, %0" :: "r" (1u << 11));           /* MEIE */
    irq_on();
    apb_i2s_start(&i2s, true, false);
    apb_i2s_irq_enable(&i2s, APB_I2S_IRQ_TX_LOW);
    return 0;
}

/*------------------------------- music ------------------------------------*/

void arv_music_stop(void)
{
    m_on = false;
    if (m_fd >= 0)
        close(m_fd);
    m_fd = -1;
    arv_music_song = ~0u;
}

/* Song n (MUSnn.ADP); no file: silence */
void arv_music_start(int song)
{
    char name[16];
    arv_music_stop();
    snprintf(name, sizeof(name), "MUS%02d.ADP", song);
    m_fd = open(name, O_RDONLY);
    if (m_fd < 0 || lseek(m_fd, MUS_HEADER, SEEK_SET) != (off_t) MUS_HEADER) {
        arv_music_stop();
        return;
    }
    irq_off();
    m_wr = 0;
    m_rd = 0;
    m_loop = ~0u;
    m_pred = 0;
    m_idx = 0;
    arv_music_song = (uint32_t) song;
    irq_on();
    m_on = true;
}

int arv_music_playing(void)
{
    return m_on;
}

static void music_pump(void)
{
    while (m_on && MUS_BYTES - (m_wr - (m_rd >> 1)) >= MUS_CHUNK) {
        uint8_t *dst = mring + (m_wr & (MUS_BYTES - 1u));
        int      n = read(m_fd, dst, MUS_CHUNK);
        if (n < 0) { arv_music_stop(); return; }
        if (n < (int) MUS_CHUNK) {                                  /* the end: on from the start */
            int m;
            m_loop = (m_wr + (uint32_t) n) * 2u;
            if (lseek(m_fd, MUS_HEADER, SEEK_SET) != (off_t) MUS_HEADER ||
                (m = read(m_fd, dst + n, MUS_CHUNK - (unsigned) n)) < 0) {
                arv_music_stop();
                return;
            }
            n += m;
        }
        m_wr += (uint32_t) n;
        if (n < (int) MUS_CHUNK)
            return;                                                 /* a song shorter than a chunk */
    }
}

/*------------------------------- effects ----------------------------------*/

static bool sfx_load(void)
{
    uint8_t  h[16 + SFX_SOUNDS * 12];
    unsigned i;
    if (s_loaded)
        return s_fd >= 0;
    s_loaded = true;
    s_fd = open("SOUNDS.SFX", O_RDONLY);
    if (s_fd < 0)
        return false;
    if (read(s_fd, h, sizeof(h)) != (int) sizeof(h) || h[0] != 'W' || h[1] != 'S' || h[2] != 'F' || h[3] != 'X') {
        close(s_fd);
        s_fd = -1;
        return false;
    }
    for (i = 0; i < SFX_SOUNDS; i++) {
        const uint8_t *e = h + 16 + 12 * i;
        s_off[i] = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t) e[3] << 24);
        s_len[i] = e[4] | (e[5] << 8) | (e[6] << 16) | ((uint32_t) e[7] << 24);
    }
    return true;
}

static int32_t weight(int pos)                                      /* attenuation 0..15 -> 128..0 */
{
    return (int32_t) ((15 - pos) * 128 / 15);
}

/* Fill a channel's buffer from the bank as far as there is room (a channel
 * being started is filled before it is switched on) */
static void sfx_fill(sfx_t *s, bool starting)
{
    while ((s->on || starting) && s->pos - s_off[s->sound] < s->len && SFX_BYTES - (s->wr - s->rd) >= SFX_CHUNK) {
        uint32_t left = s->len - (s->pos - s_off[s->sound]);
        unsigned k = left < SFX_CHUNK ? left : SFX_CHUNK;
        if (lseek(s_fd, (off_t) s->pos, SEEK_SET) != (off_t) s->pos ||
            read(s_fd, s->buf + (s->wr & (SFX_BYTES - 1u)), k) != (int) k) {
            s->on = false;
            s->len = 0;
            return;
        }
        s->pos += k;
        s->wr  += k;
    }
}

/* Sound n, attenuations l and r (0..15), on channel c (-1: any); returns
 * the channel + 1, 0 when nothing plays */
int arv_sfx_play(int sound, int l, int r, int c)
{
    sfx_t   *s;
    unsigned i;
    if (sound < 0 || sound >= (int) SFX_SOUNDS || !sfx_load() || !s_len[sound])
        return 0;
    if (c < 0 || c >= (int) SFX_CH) {
        c = 2;
        for (i = 2; i < SFX_CH; i++)
            if (!ch[i].on) { c = (int) i; break; }
        if (ch[c].on)
            for (i = 2; i < SFX_CH; i++)
                if (ch[i].start < ch[c].start) c = (int) i;
    }
    s = &ch[c];
    irq_off();
    s->on    = false;
    s->sound = sound;
    s->len   = s_len[sound];
    s->pos   = s_off[sound];
    s->wr    = 0;
    s->rd    = 0;
    s->phase = 0;
    s->vl    = weight(l);
    s->vr    = weight(r);
    s->start = ++s_clock;
    irq_on();
    sfx_fill(s, true);                                              /* the first 1 KB now */
    if (!s->len)
        return 0;
    s->on = true;
    s_last = sound;
    s_last_ch = c;
    arv_sfx_played++;
#ifdef SIMULATION
    {
        char m[32];
        snprintf(m, sizeof(m), "sfx %d ch %d\n", sound, c);
        for (const char *p = m; *p; p++)
            *(volatile uint32_t *) (BOARD_LKS_BASE + 4u) = (uint8_t) *p;     /* the TTY register */
    }
#endif
    return c + 1;
}

void arv_sfx_pan(int c, int l, int r)
{
    if (c >= 0 && c < (int) SFX_CH) {
        ch[c].vl = weight(l);
        ch[c].vr = weight(r);
    }
}

void arv_sfx_stop(void)
{
    unsigned c;
    for (c = 0; c < SFX_CH; c++)
        ch[c].on = false;
}

/* The last sound started, while its channel still plays it (else -1) */
int arv_sfx_playing(void)
{
    if (s_last_ch >= 0 && ch[s_last_ch].on && ch[s_last_ch].sound == s_last)
        return s_last;
    return -1;
}

/*------------------------------- main loop --------------------------------*/

void arv_audio_pump(void)
{
    unsigned c;
    music_pump();
    for (c = 0; c < SFX_CH; c++)
        sfx_fill(&ch[c], false);
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K audio demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// demo_audio -- a music box on the board's own speaker or a Pmod I2S2: the
// I2S master (apb_i2s at 0x10042000) feeds the onboard MAX98357A amplifier,
// or the Pmod I2S2 line out on PMOD1 while the PMOD1 pins belong to GPIO
// alternate function 0.
//
// Synthesis: four voices, each a phase accumulator reading one 256-entry
// wavetable (a fundamental and three overtones) with an exponentially
// decaying envelope, so notes ring and overlap like a music box. The mix is
// computed sample by sample on the CPU and written to the transmit FIFO,
// which paces the loop; the sample counter is the only clock.
//
// Four public-domain pieces play in a loop: Ode to Joy (Beethoven), the
// opening of Fur Elise (Beethoven), the opening of the Prelude in C major
// BWV 846 (Bach), and a sweep of arpeggios over the speaker's range.
//
// BOARD CONTROLS : KEY[1] pressed briefly = next piece; held 3/4 s = the next
// sound output (HDMI, speaker, Pmod I2S2; no HDMI in the panel build), from
// the board I/O AUDIO_OUT as found (KEY[0] = reset).
// LED register: [7:4] piece number, [3] set after a transmit underrun (the
// CPU did not keep up), [2] on the Pmod I2S2, [1] the beat: on for the
// first sixteenth of every beat on the speaker, off for it on the Pmod,
// [0] toggles on every melody note.
//
// Simulation (-DSIMULATION): a higher sample rate (RATIO 256) and the tempo
// eight times faster, the long press sixteen times shorter; src/demo_audio.v decodes the
// amplifier and the Pmod pins.
//----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "apb_i2s.h"

#define VOICES       4u
#define BLOCK        16u                    // samples generated per FIFO write
#define K_RELEASE    40u                    // envelope decay of a released voice
#ifdef SIMULATION
#define RATIO        256u
#define TEMPO_SCALE  8u
#define PRESS_SCALE  16u
#else
#define RATIO        384u
#define TEMPO_SCALE  1u
#define PRESS_SCALE  1u
#endif
#define FS_HZ        (BOARD_HCLK_HZ / (2u * RATIO))
#define LONG_PRESS   (FS_HZ * 3u / (4u * PRESS_SCALE))  // samples: KEY[1] held, output switch

//----------------------------------------------------------------------------
// Wavetable and pitch
//----------------------------------------------------------------------------

static const int16_t wave[256] = {
      5496,   5957,   6390,   6797,   7176,   7528,   7852,   8149,   8419,   8662,   8879,   9071,
      9239,   9383,   9504,   9604,   9684,   9744,   9787,   9813,   9824,   9822,   9806,   9780,
      9743,   9698,   9646,   9587,   9524,   9456,   9386,   9313,   9239,   9165,   9091,   9018,
      8946,   8876,   8809,   8744,   8681,   8621,   8564,   8509,   8457,   8408,   8360,   8315,
      8271,   8228,   8186,   8145,   8103,   8062,   8019,   7975,   7930,   7883,   7833,   7781,
      7726,   7668,   7607,   7542,   7473,   7401,   7325,   7246,   7163,   7076,   6987,   6894,
      6799,   6701,   6601,   6499,   6395,   6291,   6186,   6080,   5975,   5870,   5765,   5662,
      5561,   5461,   5364,   5269,   5176,   5087,   5000,   4916,   4836,   4758,   4684,   4612,
      4543,   4476,   4412,   4349,   4288,   4229,   4170,   4112,   4053,   3994,   3934,   3872,
      3808,   3742,   3673,   3600,   3523,   3442,   3356,   3265,   3168,   3066,   2958,   2843,
      2723,   2596,   2463,   2324,   2178,   2027,   1870,   1707,   1540,   1368,   1192,   1012,
       828,    642,    454,    265,     74,   -116,   -307,   -496,   -685,   -871,  -1054,  -1235,
     -1412,  -1585,  -1754,  -1919,  -2079,  -2234,  -2384,  -2529,  -2669,  -2805,  -2937,  -3064,
     -3187,  -3308,  -3425,  -3541,  -3655,  -3768,  -3881,  -3995,  -4111,  -4229,  -4351,  -4477,
     -4608,  -4746,  -4890,  -5042,  -5203,  -5373,  -5553,  -5743,  -5945,  -6158,  -6383,  -6620,
     -6870,  -7131,  -7405,  -7690,  -7987,  -8295,  -8614,  -8942,  -9279,  -9623,  -9974, -10330,
    -10691, -11053, -11416, -11779, -12139, -12494, -12843, -13184, -13514, -13832, -14136, -14424,
    -14694, -14943, -15170, -15373, -15551, -15701, -15823, -15914, -15973, -16000, -15993, -15951,
    -15873, -15759, -15609, -15423, -15199, -14940, -14644, -14312, -13946, -13546, -13112, -12647,
    -12152, -11629, -11078, -10502,  -9903,  -9283,  -8644,  -7989,  -7319,  -6637,  -5946,  -5248,
     -4545,  -3839,  -3135,  -2433,  -1736,  -1046,   -366,    303,    957,   1596,   2218,   2820,
      3402,   3961,   4498,   5010,
};

// Frequencies of MIDI notes 120..131 in 1/256 Hz; lower octaves by shifting
static const uint32_t top_octave[12] = {
    2143237u, 2270680u, 2405702u, 2548752u, 2700309u, 2860878u,
    3030994u, 3211227u, 3402176u, 3604480u, 3818814u, 4045892u
};

static uint32_t phase_inc(unsigned midi)
{
    uint32_t f = top_octave[midi % 12u] >> (10u - midi / 12u);  // 1/256 Hz
    return (uint32_t) (((uint64_t) f << 24) / FS_HZ);
}

//----------------------------------------------------------------------------
// Voices
//----------------------------------------------------------------------------

typedef struct {
    uint32_t phase, inc;
    uint32_t amp;                           // Q24
    uint32_t k;                             // decay: amp -= amp * k / 65536 per sample
} voice_t;

static voice_t voice[VOICES];

static void note_on(unsigned midi, unsigned k)
{
    unsigned i, v = 0u;
    for (i = 1u; i < VOICES; i++)           // the quietest voice is taken over
        if (voice[i].amp < voice[v].amp)
            v = i;
    voice[v].phase = 0u;
    voice[v].inc   = phase_inc(midi);
    voice[v].amp   = (midi < 52u) ? 0x00C00000u : 0x00FFFFFFu;   // bass a little softer
    voice[v].k     = k + ((midi > 84u) ? (midi - 84u) / 6u : 0u); // high notes fade faster
}

static void release_all(void)
{
    unsigned i;
    for (i = 0u; i < VOICES; i++)
        voice[i].k = K_RELEASE;
}

static void render(uint32_t *out, unsigned n)
{
    unsigned i, v;
    for (i = 0u; i < n; i++) {
        int32_t mix = 0;
        for (v = 0u; v < VOICES; v++) {
            voice_t *p = &voice[v];
            if (p->amp == 0u)
                continue;
            p->phase += p->inc;
            mix += (wave[p->phase >> 24] * (int32_t) (p->amp >> 9)) >> 15;
            p->amp -= ((p->amp >> 10) * p->k) >> 6;
            if (p->amp < 0x1000u)
                p->amp = 0u;
        }
        mix = (mix * 3) >> 3;
        if (mix > 28000)  mix = 28000;
        if (mix < -28000) mix = -28000;
        out[i] = ((uint32_t) (uint16_t) mix << 16) | (uint16_t) mix;  // both channels
    }
}

//----------------------------------------------------------------------------
// Pieces: events of one note and a length in sixteenths; length 0 starts the
// next event at the same time (a chord); note 0 is a rest
//----------------------------------------------------------------------------

typedef struct { uint8_t note, len; } ev_t;

typedef struct {
    const char *name;
    unsigned    bpm, k;                     // tempo, envelope decay
    const ev_t *track[2];
    unsigned    len[2];
} piece_t;

enum {
    E2 = 40, G2 = 43, A2 = 45, B2 = 47, C3 = 48, D3 = 50, E3 = 52, F3 = 53, G3 = 55, Gs3 = 56,
    A3 = 57, B3 = 59, C4 = 60, D4 = 62, E4 = 64, F4 = 65, Fs4 = 66, G4 = 67, Gs4 = 68, A4 = 69,
    B4 = 71, C5 = 72, D5 = 74, Ds5 = 75, E5 = 76, F5 = 77, G5 = 79, A5 = 81, C6 = 84, E6 = 88,
    G6 = 91, C7 = 96, R = 0
};

static const ev_t ode_melody[] = {
    {E4,4},{E4,4},{F4,4},{G4,4}, {G4,4},{F4,4},{E4,4},{D4,4}, {C4,4},{C4,4},{D4,4},{E4,4}, {E4,6},{D4,2},{D4,8},
    {E4,4},{E4,4},{F4,4},{G4,4}, {G4,4},{F4,4},{E4,4},{D4,4}, {C4,4},{C4,4},{D4,4},{E4,4}, {D4,6},{C4,2},{C4,8},
    {R,8}
};
static const ev_t ode_bass[] = {
    {C3,8},{G3,8}, {G2,8},{D3,8}, {C3,8},{G3,8}, {G2,8},{G2,8},
    {C3,8},{G3,8}, {G2,8},{D3,8}, {C3,8},{G3,8}, {G2,8},{C3,8}
};

static const ev_t elise[] = {
    {E5,1},{Ds5,1},
    {E5,1},{Ds5,1},{E5,1},{B4,1},{D5,1},{C5,1},
    {A4,0},{A2,1},{E3,1},{A3,1},{C4,1},{E4,1},{A4,1},
    {B4,0},{E2,1},{E3,1},{Gs3,1},{E4,1},{Gs4,1},{B4,1},
    {C5,0},{A2,1},{E3,1},{A3,1},{E4,1},{E5,1},{Ds5,1},
    {E5,1},{Ds5,1},{E5,1},{B4,1},{D5,1},{C5,1},
    {A4,0},{A2,1},{E3,1},{A3,1},{C4,1},{E4,1},{A4,1},
    {B4,0},{E2,1},{E3,1},{Gs3,1},{E4,1},{C5,1},{B4,1},
    {A4,0},{A2,1},{E3,1},{A3,6},
    {R,6}
};

// One bar of the prelude: a broken chord, twice
#define BAR(a, b, c, d, e) {a,1},{b,1},{c,1},{d,1},{e,1},{c,1},{d,1},{e,1}, \
                           {a,1},{b,1},{c,1},{d,1},{e,1},{c,1},{d,1},{e,1}
static const ev_t prelude[] = {
    BAR(C4, E4, G4, C5, E5),
    BAR(C4, D4, A4, D5, F5),
    BAR(B3, D4, G4, D5, F5),
    BAR(C4, E4, G4, C5, E5),
    BAR(C4, E4, A4, E5, A5),
    BAR(C4, D4, Fs4, A4, D5),
    BAR(B3, D4, G4, D5, G5),
    BAR(B3, C4, E4, G4, C5),
    {C3,0},{C4,0},{E4,0},{G4,0},{C5,16},
    {R,8}
};

static const ev_t sweep[] = {
    {C3,1},{E3,1},{G3,1},{C4,1},{E4,1},{G4,1},{C5,1},{E5,1},{G5,1},{C6,1},{E6,1},{G6,1},{C7,4},
    {G6,1},{E6,1},{C6,1},{G5,1},{E5,1},{C5,1},{G4,1},{E4,1},{C4,1},{G3,1},{E3,1},{C3,4},
    {A2,1},{E3,1},{A3,1},{C4,1},{E4,1},{A4,1},{C5,1},{E5,1},{A5,4},
    {F3,1},{A3,1},{C4,1},{F4,1},{A4,1},{C5,1},{F5,1},{A5,1},{C6,4},
    {G2,1},{D3,1},{G3,1},{B3,1},{D4,1},{G4,1},{B4,1},{D5,1},{G5,4},
    {C3,0},{G3,0},{C4,0},{E4,0},{G4,0},{C5,16},
    {R,8}
};

#define N_OF(a) (sizeof(a) / sizeof((a)[0]))

static const piece_t pieces[] = {
    { "Ode to Joy (Beethoven)",             132u, 3u, { ode_melody, ode_bass }, { N_OF(ode_melody), N_OF(ode_bass) } },
    { "Fur Elise (Beethoven)",              104u, 2u, { elise, 0 },             { N_OF(elise), 0u } },
    { "Prelude in C, BWV 846 (Bach)",        84u, 2u, { prelude, 0 },           { N_OF(prelude), 0u } },
    { "Arpeggios",                          150u, 3u, { sweep, 0 },             { N_OF(sweep), 0u } }
};
#define PIECES N_OF(pieces)

//----------------------------------------------------------------------------
// Sequencer
//----------------------------------------------------------------------------

typedef struct {
    unsigned idx[2], wait[2];
    unsigned tick;
} seq_t;

// One sixteenth: the events due on each track; false once both tracks ended
static bool seq_tick(seq_t *s, const piece_t *p, bool *melody_note)
{
    bool     busy = false;
    unsigned t;
    *melody_note = false;
    for (t = 0u; t < 2u; t++) {
        if (p->track[t] == 0)
            continue;
        while ((s->wait[t] == 0u) && (s->idx[t] < p->len[t])) {
            const ev_t *e = &p->track[t][s->idx[t]++];
            if (e->note != R) {
                note_on(e->note, p->k);
                if (t == 0u)
                    *melody_note = true;
            }
            s->wait[t] = e->len;
        }
        if (s->wait[t] != 0u) {
            s->wait[t]--;
            busy = true;
        }
    }
    s->tick++;
    return busy;
}

//----------------------------------------------------------------------------

static lks_t      lks;
static apb_i2s_t  i2s;

static void say(const char *s)
{
    while (*s)
        lks_tty_putc(&lks, *s++);
    lks_tty_putc(&lks, '\n');
}

static void push(const uint32_t *buf, unsigned n)
{
    unsigned k = 0u;
    while (k < n)
        k += apb_i2s_write(&i2s, buf + k, n - k);
}

// The stream's destination (board I/O AUDIO_OUT), switched with the I2S
// master stopped; the stream restarts on a block of silence. The Pmod's D/A
// needs MCLK; the amplifier is on only for the speaker.
static void set_output(lks_audio_t out)
{
    static const uint32_t silence[BLOCK];
    static const char *const name[4] = { "output: HDMI", "output: speaker", "output: Pmod I2S2 (PMOD1)", "output: none" };
    apb_i2s_stop(&i2s);
    apb_i2s_flush(&i2s, true, false);
    lks_audio_output(&lks, out);
    push(silence, BLOCK);
    apb_i2s_start(&i2s, true, false);
    say(name[out & 3u]);
}

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
    uint32_t buf[BLOCK];
    unsigned piece = 0u, leds = 0u, held = 0u, n;
    bool     under = false, primed = false;
    lks_audio_t out;
    seq_t    seq = { { 0u, 0u }, { 0u, 0u }, 0u };

    lks_init(&lks, BOARD_LKS_BASE);
    if ((apb_i2s_init(&i2s, BOARD_I2S_BASE) != APB_I2S_OK) || (apb_i2s_configure(&i2s, &cfg) != APB_I2S_OK)) {
        lks_led_set(&lks, 0xE0u);
        for (;;)
            ;
    }
    out = lks_audio_output_get(&lks);
    set_output(out);
    say(pieces[piece].name);

    for (;;) {
        const piece_t *p   = &pieces[piece];
        const unsigned spt = FS_HZ * 15u / (p->bpm * TEMPO_SCALE);
        bool           note, key;

        if (!seq_tick(&seq, p, &note)) {    // the piece ended: the next one
            piece = (piece + 1u) % PIECES;
            seq   = (seq_t) { { 0u, 0u }, { 0u, 0u }, 0u };
            say(pieces[piece].name);
            continue;
        }
        if (note)
            leds ^= 1u;
        const bool pmod = (out == LKS_AUDIO_PMOD);
        leds = (leds & 1u) | ((((seq.tick & 3u) == 1u) != pmod) ? 2u : 0u);
        lks_led_set(&lks, (piece << 4) | (under ? 8u : 0u) | (pmod ? 4u : 0u) | leds);

        for (n = 0u; n < spt; n += BLOCK) {
            unsigned m = (spt - n < BLOCK) ? spt - n : BLOCK;
            render(buf, m);
            push(buf, m);
        }

        // Underruns: counted once the FIFO has been full once
        if (!primed && (apb_i2s_tx_level(&i2s) + BLOCK >= i2s.tx_depth)) {
            apb_i2s_irq_clear(&i2s, APB_I2S_IRQ_TX_UNDER);
            primed = true;
        }
        if (primed && (apb_i2s_irq_pending(&i2s) & APB_I2S_IRQ_TX_UNDER))
            under = true;

        // KEY[1], polled once a sixteenth: a long press switches the output
        // while the key is held, a short one starts the next piece on release
        key = lks_key_pressed(&lks, 1u);
        if (key) {
            if ((held < LONG_PRESS) && (held + spt >= LONG_PRESS)) {
                out = (out == LKS_AUDIO_SPEAKER) ? LKS_AUDIO_PMOD
                    : (out == LKS_AUDIO_PMOD && BOARD_HDMI()) ? LKS_AUDIO_HDMI : LKS_AUDIO_SPEAKER;
                set_output(out);
                primed = false;
            }
            if (held < LONG_PRESS)
                held += spt;
        } else {
            if ((held != 0u) && (held < LONG_PRESS)) {
                release_all();
                piece = (piece + 1u) % PIECES;
                seq   = (seq_t) { { 0u, 0u }, { 0u, 0u }, 0u };
                say(pieces[piece].name);
            }
            held = 0u;
        }
    }
}

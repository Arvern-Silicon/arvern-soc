//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K LED demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// A light show on the two SoM LEDs (LED register bits 0 and 1).
//
// BRIGHTNESS
//   Each LED has a 16-bit first-order sigma-delta modulator, run by the main
//   loop at every pass (around a megahertz on the board): the LED toggles far
//   too fast to flicker and its average duty is the requested brightness.
//   Brightness is perceptual, 0..255, mapped to the duty b * (b + 2) (gamma 2,
//   255 -> 65535: fully on).
//
// ANIMATION
//   One animation step per millisecond of mtime. Eleven shows of SHOW_MS each
//   play in a loop; KEY[1] skips to the next one, and over RTT a key 0-9 or 'a'
//   jumps to a show. The show number is also written to LED register bits
//   [7:4] (no LED there): the testbench follows the shows with it.
//
//    0 BREATHE     both LEDs breathe together
//    1 HEARTBEAT   lub on one LED, dub on the other, 70 bpm
//    2 PING-PONG   a ball of light bouncing between the two, ever faster
//    3 CANDLES     two independent flames, with the odd gust of wind
//    4 BOUNCE      a dropped ball: height on one LED, impacts on the other
//    5 STORM       lightning strikes in the dark, with afterglow
//    6 FIREFLIES   slow random glows, each LED on its own
//    7 MORSE       "ARVERN" in Morse code, the other LED marks each word
//    8 LIGHTHOUSE  a rotating beam sweeping across both LEDs
//    9 POLICE      double strobes, left then right
//   10 SUPERNOVA   a pulse that speeds up until it burns steady, then fades
//
// BOARD CONTROLS : KEY[1] = next show (KEY[0] = reset).
//----------------------------------------------------------------------------

#include "tang_board.h"
#include "led_key_sw.h"
#include "ahb_aclint.h"
#include "arvern_irq.h"
#include "SEGGER_RTT.h"

#define rtt_printf(...)  SEGGER_RTT_printf(0, __VA_ARGS__)

static lks_t    lks;
static aclint_t aclint;

// One animation step per millisecond, SHOW_MS steps per show. Simulation runs
// a step every 50 mtime ticks (300 hclk cycles, room for the modulator between
// steps) and shortens the shows.
#ifdef SIMULATION
#define TICK      50u
#define SHOW_MS   200u
#else
#define TICK      (BOARD_TIME_HZ / 1000u)
#define SHOW_MS   9000u
#endif
#define MAX_CATCH 8u                     // steps run back to back before time slips

enum { S_BREATHE = 0, S_HEARTBEAT, S_PINGPONG, S_CANDLES, S_BOUNCE, S_STORM,
       S_FIREFLIES, S_MORSE, S_LIGHTHOUSE, S_POLICE, S_SUPERNOVA, NUM_SHOWS };

static const char *const SHOW_NAMES[NUM_SHOWS] = {
    "BREATHE", "HEARTBEAT", "PING-PONG", "CANDLES", "BOUNCE", "STORM",
    "FIREFLIES", "MORSE", "LIGHTHOUSE", "POLICE", "SUPERNOVA"
};

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

// Raised cosine, one period over 256 steps: 0 at 0, 255 at 128
static const uint8_t wave8_lut[256] = {
      0,   0,   0,   0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   9,
     10,  11,  12,  14,  15,  17,  18,  20,  21,  23,  25,  27,  29,  31,  33,  35,
     37,  40,  42,  44,  47,  49,  52,  54,  57,  59,  62,  65,  67,  70,  73,  76,
     79,  82,  85,  88,  90,  93,  97, 100, 103, 106, 109, 112, 115, 118, 121, 124,
    127, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173,
    176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215,
    218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244,
    245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246,
    245, 244, 243, 241, 240, 238, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220,
    218, 215, 213, 211, 208, 206, 203, 201, 198, 196, 193, 190, 188, 185, 182, 179,
    176, 173, 170, 167, 165, 162, 158, 155, 152, 149, 146, 143, 140, 137, 134, 131,
    128, 124, 121, 118, 115, 112, 109, 106, 103, 100,  97,  93,  90,  88,  85,  82,
     79,  76,  73,  70,  67,  65,  62,  59,  57,  54,  52,  49,  47,  44,  42,  40,
     37,  35,  33,  31,  29,  27,  25,  23,  21,  20,  18,  17,  15,  14,  12,  11,
     10,   9,   7,   6,   5,   5,   4,   3,   2,   2,   1,   1,   1,   0,   0,   0,
};

static inline uint32_t wave8(uint32_t phase) { return wave8_lut[phase & 0xFFu]; }

static uint32_t rng = 0x2545F491u;       // xorshift32
static inline uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static inline uint32_t rnd_range(uint32_t lo, uint32_t hi) { return lo + rnd() % (hi - lo + 1u); }

// An exponentially decaying 16-bit level: x -= x >> k per step
static inline uint32_t decay(uint32_t x, unsigned k) { return x - (x >> k); }
static inline uint32_t max_u(uint32_t a, uint32_t b) { return (a > b) ? a : b; }

//-----------------------------------------------------------------------------
// Show state (cleared at every show start)
//-----------------------------------------------------------------------------
static uint32_t show;                    // current show
static uint32_t t;                       // milliseconds since the show started
static uint32_t out0, out1;              // brightness 0..255 for LED0 / LED1

static struct {
    uint32_t l0, l1;                     // 16-bit decaying levels
    uint32_t ph;                         // 16.16 phase accumulator
    int32_t  h, v;                       // BOUNCE: height and speed (Q24)
    uint32_t f0, f1;                     // CANDLES: flame levels (8.8)
    uint32_t g0, g1;                     // CANDLES: flame targets (8.8)
    uint32_t n0, n1;                     // next-event times
    uint32_t c0, c1;                     // per-LED event start (FIREFLIES), flashes left (STORM)
    uint32_t pos;                        // MORSE: position in the message
} s;

static void show_start(uint32_t n)
{
    uint8_t *p = (uint8_t *)&s;
    for (unsigned i = 0; i < sizeof(s); i++) p[i] = 0;
    show = n;
    t    = 0;
    rtt_printf("[show] %u %s\n", (unsigned)n, SHOW_NAMES[n]);
}

//-----------------------------------------------------------------------------
// The shows: one call per millisecond, sets out0 / out1
//-----------------------------------------------------------------------------

// Starts half-way up, so the two LEDs are lit from the first step
static void step_breathe(void)
{
    out0 = out1 = wave8(64u + t * 256u / 4000u);
}

static void step_heartbeat(void)
{
    uint32_t b = t % 857u;               // 70 beats per minute
    if (b == 0u)   s.l0 = 65535u;        // lub
    if (b == 160u) s.l1 = 65535u;        // dub
    if (b == 160u) s.l0 = max_u(s.l0, 30000u);
    s.l0 = decay(s.l0, 6);
    s.l1 = decay(s.l1, 5);
    out0 = s.l0 >> 8;
    out1 = s.l1 >> 8;
}

static void step_pingpong(void)
{
    uint32_t period = 1600u - (1400u * t) / SHOW_MS;           // 1.6 s -> 0.2 s
    s.ph += (65536u * 256u) / period;                           // 16.16 phase
    uint32_t x = wave8(s.ph >> 16);                             // 0 = LED0 side, 255 = LED1 side
    out0 = ((255u - x) * (255u - x)) >> 8;                      // sharpened
    out1 = (x * x) >> 8;
}

static void flame(uint32_t *f, uint32_t *g, uint32_t *next)
{
    if (t >= *next) {                    // a new flicker target
        *g    = rnd_range(110u, 255u) << 8;
        *next = t + rnd_range(20u, 90u);
    }
    if (*g > *f) *f += (*g - *f) >> 3;
    else         *f -= (*f - *g) >> 4;
}

static void step_candles(void)
{
    if (t == 0u) { s.f0 = s.f1 = 180u << 8; }
    if (rnd() % 2500u == 0u) {           // a gust: both flames duck together
        s.g0 = s.g1 = 25u << 8;
        s.n0 = s.n1 = t + 120u;
    }
    flame(&s.f0, &s.g0, &s.n0);
    flame(&s.f1, &s.g1, &s.n1);
    out0 = s.f0 >> 8;
    out1 = s.f1 >> 8;
}

#define B_TOP   (1 << 24)
#define B_G     93                       // falls from the top in 600 ms
static void step_bounce(void)
{
    if (t == 0u) { s.h = B_TOP; s.v = 0; }
    s.v -= B_G;
    s.h += s.v;
    if (s.h < 0) {                       // impact: flash, lose a fifth of the speed
        s.h  = -s.h;
        s.v  = -s.v - (-s.v >> 2) + (-s.v >> 4);
        uint32_t hit = (uint32_t)s.v * 65535u / 56000u;        // the harder, the brighter
        s.l1 = max_u(s.l1, (hit > 65535u) ? 65535u : hit);
        if (s.v < 4000) { s.h = B_TOP; s.v = 0; }              // at rest: drop it again
    }
    s.l1 = decay(s.l1, 5);
    out0 = (uint32_t)s.h >> 16;
    out1 = s.l1 >> 8;
}

static void step_storm(void)
{
    if (t == 0u) { s.c0 = 3u; s.c1 = 2u; s.n0 = 60u; }          // it opens with a strike
    if (s.c0 == 0u && rnd() % 1200u == 0u) {                    // a strike: 2-4 flashes
        s.c0 = rnd_range(2u, 4u);
        s.c1 = rnd() % 3u;                                      // 0: LED0, 1: LED1, 2: both
        s.n0 = t;
    }
    if (s.c0 != 0u && t >= s.n0) {
        if (s.c1 != 1u) s.l0 = 65535u;
        if (s.c1 != 0u) s.l1 = 65535u;
        s.c0--;
        s.n0 = t + rnd_range(40u, 140u);
    }
    s.l0 = decay(s.l0, 4);
    s.l1 = decay(s.l1, 4);
    out0 = max_u(s.l0 >> 8, 3u);                                // a faint sky glow
    out1 = max_u(s.l1 >> 8, 3u);
}

// Firefly glow: rises in 400 ms, holds 150 ms, fades in 900 ms
static uint32_t firefly(uint32_t *start, uint32_t *next)
{
    if (t >= *next) {
        *start = t;
        *next  = t + 1450u + rnd_range(200u, 2500u);
    }
    uint32_t a = t - *start;
    if (*next == 0u || a >= 1450u) return 0u;
    if (a < 400u)  return wave8(a * 128u / 400u);
    if (a < 550u)  return 255u;
    return wave8(128u + (a - 550u) * 128u / 900u);
}

static void step_fireflies(void)
{
    if (t == 0u) { s.n0 = 0u; s.n1 = 700u; }
    out0 = firefly(&s.c0, &s.n0);
    out1 = firefly(&s.c1, &s.n1);
}

// "ARVERN": '.' dot, '-' dash, ' ' letter gap, '/' word gap
static const char MORSE[] = ".- .-. ...- . .-. -./";
#define UNIT 110u
static void step_morse(void)
{
    if (t >= s.n0) {                     // next symbol
        char c = MORSE[s.pos];
        s.pos  = (MORSE[s.pos + 1u] != '\0') ? s.pos + 1u : 0u;
        switch (c) {
            case '.': s.c0 = 1u; s.n0 = t + 2u * UNIT; s.c1 = t + UNIT;     break;
            case '-': s.c0 = 1u; s.n0 = t + 4u * UNIT; s.c1 = t + 3u * UNIT; break;
            case ' ': s.c0 = 0u; s.n0 = t + 2u * UNIT;                       break;
            default:  s.c0 = 0u; s.n0 = t + 8u * UNIT; s.l1 = 65535u;        break;
        }
    }
    if (s.c0 && t >= s.c1) s.c0 = 0u;    // end of the dot or dash
    s.l0 = s.c0 ? 65535u : decay(s.l0, 3);
    s.l1 = decay(s.l1, 7);
    out0 = s.l0 >> 8;
    out1 = s.l1 >> 8;
}

static void step_lighthouse(void)
{
    uint32_t a  = t * 256u / 3000u;      // one turn in 3 s
    uint32_t c0 = wave8(a), c1 = wave8(a + 128u);
    c0 = (c0 * c0) >> 8; c0 = (c0 * c0) >> 8; c0 = (c0 * c0) >> 8;   // narrow beam
    c1 = (c1 * c1) >> 8; c1 = (c1 * c1) >> 8; c1 = (c1 * c1) >> 8;
    out0 = max_u(c0, 2u);
    out1 = max_u(c1, 2u);
}

static void step_police(void)
{
    uint32_t p  = t % 700u;              // 2 flashes left, 2 right, every 0.7 s
    uint32_t on = (p % 90u) < 45u;
    out0 = (p < 180u && on) ? 255u : 0u;
    out1 = (p >= 350u && p < 530u && on) ? 255u : 0u;
}

static void step_supernova(void)
{
    uint32_t burn = SHOW_MS * 6u / 10u, glow = SHOW_MS * 7u / 10u;
    if (t < burn) {                      // ever faster alternating pulses
        uint32_t period = 1000u - (960u * t) / burn;
        s.ph += (65536u * 256u) / period;
        out0 = wave8(s.ph >> 16);
        out1 = wave8((s.ph >> 16) + 128u);
    } else if (t < glow) {               // burns steady
        out0 = out1 = 255u;
    } else {                             // fades out
        out0 = out1 = 255u - (255u * (t - glow)) / (SHOW_MS - glow);
    }
}

static void show_step(void)
{
    switch (show) {
        case S_BREATHE:    step_breathe();    break;
        case S_HEARTBEAT:  step_heartbeat();  break;
        case S_PINGPONG:   step_pingpong();   break;
        case S_CANDLES:    step_candles();    break;
        case S_BOUNCE:     step_bounce();     break;
        case S_STORM:      step_storm();      break;
        case S_FIREFLIES:  step_fireflies();  break;
        case S_MORSE:      step_morse();      break;
        case S_LIGHTHOUSE: step_lighthouse(); break;
        case S_POLICE:     step_police();     break;
        default:           step_supernova();  break;
    }
    if (++t >= SHOW_MS)
        show_start((show + 1u < NUM_SHOWS) ? show + 1u : 0u);
}

// RTT: 0-9 / a select a show
static void poll_rtt_commands(void)
{
    int c;
    while ((c = SEGGER_RTT_GetKey()) >= 0) {
        if (c >= '0' && c <= '9')  show_start((uint32_t)(c - '0'));
        else if (c == 'a')         show_start(10u);
    }
}

//=============================================================================
// main
//=============================================================================
int main(void)
{
    const aclint_config_t acfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    aclint_init(&aclint, &acfg);
    lks_init(&lks, BOARD_LKS_BASE);
    arv_irq_init();

    SEGGER_RTT_Init();
    rtt_printf("\n=== aRVern two-LED light show ===\n"
               "KEY[1]: next show, RTT 0-9 / a: select a show\n");

    uint32_t acc0 = 0u, acc1 = 0u;       // sigma-delta accumulators
    uint32_t duty0 = 0u, duty1 = 0u;
    uint32_t key_q = 0u;
    uint32_t last  = aclint_mtime_lo(&aclint);

    show_start(S_BREATHE);

    for (;;) {
        // Animation: one step per elapsed millisecond (a bounded catch-up)
        uint32_t now = aclint_mtime_lo(&aclint);
        if (now - last >= TICK) {
            unsigned n = 0u;
            while (now - last >= TICK && n < MAX_CATCH) {
                last += TICK;
                show_step();
                n++;
            }
            if (now - last >= TICK) last = now;                 // time slips
            duty0 = out0 * (out0 + 2u);
            duty1 = out1 * (out1 + 2u);

            uint32_t key = lks_key_pressed(&lks, 1u);           // KEY[1]: next show
            if (key && !key_q)
                show_start((show + 1u < NUM_SHOWS) ? show + 1u : 0u);
            key_q = key;
            poll_rtt_commands();
        }

        // Brightness: one sigma-delta step per LED
        acc0 += duty0;
        acc1 += duty1;
        lks_led_set(&lks, (show << 4) | ((acc1 >> 15) & 2u) | (acc0 >> 16));
        acc0 &= 0xFFFFu;
        acc1 &= 0xFFFFu;
    }

    return 0;
}

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (DE0-Nano-SoC LED demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// arvern FPGA "cool LED" demo -- a fully interrupt-driven light show on the
// 8 board LEDs. main() only configures the peripherals and interrupts, then
// idles in wfi; ALL LED activity happens in interrupt context.
//
// INTERRUPT SOURCES
//   - Machine timer  (MTIP, MIP[7])     : the animation clock. Each tick the
//                                         current mode draws one frame, then
//                                         re-arms mtimecmp (which clears MTIP).
//   - SW change      (platform IRQ[1],  : SW[0] = count/scan direction,
//                     MIP[17])            SW[2:1] = animation speed (4 levels).
//   - KEY[1] press   (platform IRQ[0],  : cycles the display mode (and acts as
//                     MIP[16])            the action button in the game mode).
//
//   The mode can also be selected from the host by sending a digit over the RTT
//   down-channel -- see poll_rtt_commands(). That is polled from the main loop,
//   not from an interrupt.
//
//   NOTE: the platform pending bits MIP[31:16] are latched in this core, so the
//   SW and KEY handlers clear BOTH the peripheral source AND the MIP bit,
//   source-first (the timer's MTIP is level-based, cleared by the mtimecmp
//   write). KEY[0] is the board reset; SW[3] is unused.
//
// DISPLAY MODES (KEY[1] cycles through them)
//   0 COUNTER  : binary up/down counter on the LED bar (direction = SW[0]).
//   1 SCANNER  : single lit LED bouncing left/right (Larson / "Knight Rider").
//   2 BAR      : a bar that grows from one end to full, then collapses.
//   3 SPARKLE  : pseudo-random pattern from an 8-bit maximal-length LFSR.
//   4 BREATHE  : software-PWM "breathing" -- a smooth brightness wave that
//                ripples across the bar (runs at a faster fixed timer rate).
//   5 GAME     : reaction-time game. After a random delay all LEDs flash (GO);
//                press KEY[1] as fast as you can. Success shows a STEADY score
//                bar (faster = more LEDs lit); a false start (press before GO)
//                or being too slow shows a BLINKING error. The result is held
//                a couple of seconds, then the game auto-returns to mode 0.
//
// BOARD CONTROLS : KEY[1] = next mode / react,  SW[0] = direction,
//                  SW[2:1] = speed.  (KEY[0] = reset.)
//----------------------------------------------------------------------------

#include "mylib/my_func.h"
#include "SEGGER_RTT.h"

// RTT (SEGGER Real Time Transfer) -- target->host printf over the debug link,
// with no UART and no halting: the host reads the ring buffer straight out of
// SRAM over SBA while the core keeps running. Channel 0 is configured
// NO_BLOCK_SKIP, so with no debugger attached these calls just drop the text and
// the light show is unaffected.
//
// Channel 0 also carries commands the other way (host -> target): see
// poll_rtt_commands() below.
//
// View it with the arvern-minidebug RTT tab, arvern-gdbserver --rtt-port, or
// SEGGER's JLinkRTTViewer / Ozone. NOT with OpenOCD: it registers `rtt setup`
// commands only for certain target types and `riscv` is not one of them.
#define rtt_printf(...)  SEGGER_RTT_printf(0, __VA_ARGS__)

static const char *const MODE_NAMES[] = {
    "COUNTER", "SCANNER", "BAR", "SPARKLE", "BREATHE", "GAME"
};

//-----------------------------------------------------------------------------
// CSR access helpers
//-----------------------------------------------------------------------------
#define read_csr(reg)        ({ unsigned int __v; __asm__ volatile ("csrr %0, " #reg : "=r"(__v)); __v; })
#define write_csr(reg, val)  ({ __asm__ volatile ("csrw " #reg ", %0" :: "r"(val) : "memory"); })
#define set_csr(reg, val)    ({ __asm__ volatile ("csrs " #reg ", %0" :: "r"(val) : "memory"); })
#define clear_csr(reg, val)  ({ __asm__ volatile ("csrc " #reg ", %0" :: "r"(val) : "memory"); })

// mstatus / mie bit positions
#define MSTATUS_MIE     (1u << 3)          // global machine interrupt enable
#define MIE_MTIE        (1u << 7)          // machine timer       (MIP[7], MTIP)
#define MIE_PLATFORM0   (1u << 16)         // platform IRQ[0] = KEY (MIP[16])
#define MIE_PLATFORM1   (1u << 17)         // platform IRQ[1] = SW  (MIP[17])

// mcause values (interrupt bit 31 + cause code)
#define MCAUSE_IRQ_TIMER (0x80000000u | 7u)
#define MCAUSE_IRQ_KEY   (0x80000000u | 16u)
#define MCAUSE_IRQ_SW    (0x80000000u | 17u)

// Peripheral KEY_SW source bits: [3:0]=SW[3:0], [5:4]=KEY[1:0]
#define SW_MASK   0x0Fu                    // SW[3:0]
#define KEY1_BIT  (1u << 5)               // KEY[1]  (KEY[0] is the board reset)

// Timer tick base. mtime increments at the LF clock, 5 MHz (1 tick = 200 ns) on
// the board and in simulation alike; simulation only uses shorter intervals.
// SW[2:1] selects one of four speeds (index into speed_interval[]).
#ifdef SIMULATION
static const unsigned int speed_interval[4] = {   80u,   50u,   30u,   15u };
#define BREATHE_INTERVAL  4u                 // fast PWM carrier sub-step (sim)
#else
static const unsigned int speed_interval[4] = {1000000u, 500000u, 200000u, 80000u};  // 200/100/40/16 ms
#define BREATHE_INTERVAL  600u               // PWM sub-step: 120 us -> ~130 Hz refresh @ 64 steps (board)
#endif

#define PWM_STEPS  64u                       // breathing PWM resolution (sub-steps per refresh)
#define BR_SPREAD  8u                        // per-LED brightness phase offset (0 = all LEDs breathe in sync)
#define WAVE_SLOW  3u                        // wave advances every WAVE_SLOW PWM cycles (larger = slower breath)

// Reaction-game timing (tick = GAME_INTERVAL mtime units)
#ifdef SIMULATION
#define GAME_INTERVAL    20u
#define GAME_SCORE_HOLD  6u                  // (kept short in simulation)
#define GAME_FAIL_HOLD   6u
#else
#define GAME_INTERVAL    200000u             // 40 ms poll on the board
#define GAME_SCORE_HOLD  64u                 // ~2.6 s steady score (time to read it)
#define GAME_FAIL_HOLD   48u                 // ~2 s blinking error
#endif
#define GAME_GO_TIMEOUT  40u                 // ticks to react before "too slow"
#define GAME_BUCKET      (GAME_INTERVAL)     // react-time -> LED-bar scaling (~40 ms / LED)

//-----------------------------------------------------------------------------
// Display modes
//-----------------------------------------------------------------------------
enum { MODE_COUNTER = 0, MODE_SCANNER, MODE_BAR, MODE_SPARKLE, MODE_BREATHE,
       MODE_GAME, NUM_MODES };

// Reaction-game sub-states
enum { GAME_WAIT = 0, GAME_GO, GAME_SCORE, GAME_FAIL };

//-----------------------------------------------------------------------------
// State shared with interrupt context (startup.S zeroes .bss before main)
//-----------------------------------------------------------------------------
volatile int          mode;              // current display mode (cycled by KEY)
volatile int          led_dir;           // +1 up / -1 down (SW[0])
volatile unsigned int interval;          // current timer interval (SW[2:1])

static unsigned int   counter_val;       // MODE_COUNTER
static int            scan_pos, scan_dir;// MODE_SCANNER (Larson bounce)
static int            bar_level, bar_dir;// MODE_BAR (expanding/collapsing bar)
static unsigned int   lfsr;              // MODE_SPARKLE (8-bit Galois LFSR)
static unsigned int   pwm_phase;         // MODE_BREATHE PWM sub-step (0..PWM_STEPS-1)
static unsigned int   wave_pos;          // MODE_BREATHE travelling-wave position
static unsigned int   wave_sub;          // MODE_BREATHE wave-speed prescaler
static int            game_state;        // MODE_GAME sub-state (GAME_*)
static unsigned int   game_timer;        // MODE_GAME tick countdown
static unsigned int   go_time;           // mtime captured at the GO flash

//-----------------------------------------------------------------------------
// RTT events recorded in interrupt context, PRINTED from the main loop.
//
// Never call SEGGER_RTT_printf from an ISR. It formats a string and takes the
// RTT lock (which masks interrupts) -- thousands of cycles. The KEY and SW
// pending bits are LATCHED single bits (MIP[16]/MIP[17]), so every press that
// arrives inside that window collapses into the one already-pending bit and is
// silently lost. A burst of key presses then produces far fewer mode changes
// than the user made -- which is exactly what it looks like: a dropped press.
//
// Recording an event is a couple of stores; the main loop prints it after the
// next `wfi` wake, at most one animation frame later.
//-----------------------------------------------------------------------------
#define EV_MODE   (1u << 0)              // mode changed by KEY  -> ev_mode
#define EV_REACT  (1u << 1)              // game reaction scored -> ev_react/_bar
#define EV_FAIL   (1u << 2)              // game false start / too slow

static volatile unsigned int ev_pending; // bitmask of EV_*
static volatile int          ev_mode;    // new mode, for EV_MODE
static volatile unsigned int ev_react;   // reaction time in ticks, for EV_REACT
static volatile unsigned int ev_bar;     // score bar, for EV_REACT

//-----------------------------------------------------------------------------
// MTIMER helpers (64-bit mtime / mtimecmp over a 32-bit bus)
//-----------------------------------------------------------------------------
static inline unsigned long long mtime_read(void)
{
    unsigned int lo = MTIME_LO;          // LO first: latches the HI snapshot
    unsigned int hi = MTIME_HI;
    return ((unsigned long long)hi << 32) | lo;
}

static inline void mtimecmp_write(unsigned long long v)
{
    MTIMECMP_LO = 0xFFFFFFFFu;            // park unreachable (no spurious MTIP)
    MTIMECMP_HI = (unsigned int)(v >> 32);
    MTIMECMP_LO = (unsigned int)(v & 0xFFFFFFFFu);
}

static inline void mtimer_set_next(unsigned int iv)
{
    mtimecmp_write(mtime_read() + iv);
}

//-----------------------------------------------------------------------------
// Per-mode frame update (called once per timer tick)
//-----------------------------------------------------------------------------
static inline void frame_counter(void)
{
    counter_val += (unsigned int)led_dir;
    P1_LED_CTRL = counter_val & 0xFF;
}

static inline void frame_scanner(void)    // single lit LED bouncing 0..7
{
    scan_pos += scan_dir;
    if      (scan_pos >= 7) { scan_pos = 7; scan_dir = -1; }
    else if (scan_pos <= 0) { scan_pos = 0; scan_dir = +1; }
    P1_LED_CTRL = (1u << scan_pos);
}

static inline void frame_bar(void)         // bar grows 0..8 then shrinks
{
    bar_level += bar_dir;
    if      (bar_level >= 8) { bar_level = 8; bar_dir = -1; }
    else if (bar_level <= 0) { bar_level = 0; bar_dir = +1; }
    P1_LED_CTRL = (bar_level >= 8) ? 0xFFu : ((1u << bar_level) - 1u);
}

static inline void lfsr_step(void)         // 8-bit maximal-length Galois LFSR
{
    unsigned int lsb = lfsr & 1u;
    lfsr >>= 1;
    if (lsb) lfsr ^= 0xB8u;
}

static inline void frame_sparkle(void)     // pseudo-random pattern (LFSR)
{
    lfsr_step();
    P1_LED_CTRL = lfsr & 0xFFu;
}

// Gamma-corrected brightness LUT (gamma 2.2): a linear brightness index 0..31
// maps to a perceptually-even PWM duty 0..63. Without this, linear PWM steps
// look harsh/jumpy -- almost all the visible change crowds into the low end.
static const unsigned char gamma_lut[32] = {
     0,  0,  0,  0,  1,  1,  2,  2,
     3,  4,  5,  6,  8,  9, 11, 13,
    15, 17, 19, 21, 24, 27, 30, 33,
    36, 39, 43, 46, 50, 54, 59, 63,
};

// Per-LED PWM duty: a triangle brightness ramp (0..31..0) shifted per LED and
// advanced by the travelling wave, then gamma-corrected for a smooth fade.
static inline unsigned int breathe_duty(unsigned int led)
{
    unsigned int p   = (led * BR_SPREAD + wave_pos) & 63u;   // 0..63
    unsigned int idx = (p < 32u) ? p : (63u - p);            // triangle 0..31..0
    return gamma_lut[idx];                                   // -> duty 0..63
}

static inline void frame_breathe(void)     // software-PWM "breathing" wave
{
    unsigned int b = 0, i;
    for (i = 0; i < 8u; i++)
        if (pwm_phase < breathe_duty(i)) b |= (1u << i);
    P1_LED_CTRL = b;

    if (++pwm_phase >= PWM_STEPS) {        // one full PWM cycle done
        pwm_phase = 0;
        if (++wave_sub >= WAVE_SLOW) {     // advance the wave more slowly
            wave_sub = 0;
            wave_pos++;
        }
    }
}

static inline void reset_mode_state(void)
{
    scan_pos = 0; scan_dir = +1;
    bar_level = 0; bar_dir = +1;
    pwm_phase = 0; wave_pos = 0; wave_sub = 0;
}

//-----------------------------------------------------------------------------
// Reaction game (MODE_GAME)
//   WAIT  : LEDs dark for a random delay, then flash all on ("GO")
//   GO    : all LEDs lit; the player presses KEY[1] as fast as possible
//   SCORE : success -- a STEADY LED bar shows the reaction time
//           (faster = MORE LEDs), held a couple of seconds, then auto-return.
//   FAIL  : false start (press before GO) or too slow (no press in time) --
//           a BLINKING all-LEDs error, held a couple of seconds, then return.
//   So: steady bar = your score, blinking = you failed.
//-----------------------------------------------------------------------------
static inline void game_start(void)
{
    game_state = GAME_WAIT;
    lfsr_step();
    game_timer  = 6u + (lfsr & 0x0Fu);     // random 6..21 ticks before GO
    P1_LED_CTRL = 0x00;
}

// react time (mtime units) -> lit-LED count: faster reaction lights MORE LEDs
// (a solid bar from the bottom). Always at least 1 (you did react).
static inline unsigned int score_bar(unsigned int react)
{
    unsigned int bucket = react / GAME_BUCKET;        // 0 = lightning fast
    unsigned int n = (bucket >= 7u) ? 1u : (8u - bucket);
    return (n >= 8u) ? 0xFFu : ((1u << n) - 1u);
}

static inline void enter_fail(void)
{
    ev_pending |= EV_FAIL;               // printed from the main loop, not here
    game_state = GAME_FAIL;
    game_timer = GAME_FAIL_HOLD;
}

static inline void frame_game(void)
{
    switch (game_state) {
        case GAME_WAIT:
            if (--game_timer == 0u) {
                game_state  = GAME_GO;
                go_time     = (unsigned int)mtime_read();
                P1_LED_CTRL = 0xFF;                // GO!
                game_timer  = GAME_GO_TIMEOUT;
            }
            break;
        case GAME_GO:
            if (--game_timer == 0u) enter_fail();  // too slow
            break;                                 // else hold 0xFF, await KEY
        case GAME_SCORE:                           // steady score bar, then return
            if (--game_timer == 0u) { mode = MODE_COUNTER; reset_mode_state(); }
            break;
        case GAME_FAIL:                            // blinking error, then return
        default:
            if (--game_timer == 0u) { mode = MODE_COUNTER; reset_mode_state(); }
            else P1_LED_CTRL = ((game_timer >> 2) & 1u) ? 0xFFu : 0x00u;
            break;
    }
}

//-----------------------------------------------------------------------------
// Mode switching
//
// Shared by the two things that can change mode: the KEY[1] handler and an RTT
// command. It does NOT touch interrupts -- the trap handler already runs with
// them off, and the RTT path (thread context) wraps this in its own critical
// section. Doing it here instead would silently re-enable interrupts inside the
// trap handler.
//-----------------------------------------------------------------------------
static inline void apply_mode(int m)
{
    mode = m;
    reset_mode_state();
    if (mode == MODE_GAME) game_start();
}

//-----------------------------------------------------------------------------
// RTT down-channel commands (host -> target)
//
// Called from the main loop after `wfi` returns, NEVER from an interrupt: it can
// call SEGGER_RTT_printf, which takes the RTT lock, and the timer tick is what
// wakes us -- so commands are serviced at the frame rate of the current mode
// (fastest in BREATHE, ~once per animation step otherwise). More than good
// enough for typing a digit, and it costs nothing when no debugger is attached.
//
// Digits select a mode directly, using the same numbering the log already
// prints ("mode -> 1 (SCANNER)"). Anything else is echoed back as ignored, so a
// stray keystroke explains itself rather than vanishing.
//-----------------------------------------------------------------------------
static void poll_rtt_commands(void)
{
    int c;

    while ((c = SEGGER_RTT_GetKey()) >= 0) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;                       // line endings from the terminal

        if (c >= '0' && c < '0' + NUM_MODES) {
            // `mode` and the per-mode state are read by the timer ISR every
            // tick; changing them piecemeal would let a frame render against a
            // half-switched mode. Save/restore rather than unconditionally
            // re-enabling, so this stays correct if it is ever called with
            // interrupts already off.
            unsigned int prev = read_csr(mstatus);
            clear_csr(mstatus, MSTATUS_MIE);
            apply_mode(c - '0');
            if (prev & MSTATUS_MIE) set_csr(mstatus, MSTATUS_MIE);

            rtt_printf("[rtt ] mode -> %d (%s)\n", mode, MODE_NAMES[mode]);
        } else {
            rtt_printf("[rtt ] '%c' ignored -- send 0..%d\n",
                       (char)c, NUM_MODES - 1);
        }
    }
}

//-----------------------------------------------------------------------------
// Print whatever the interrupt handlers recorded. Main-loop context only -- see
// the ev_* declarations for why these are not printed where they happen.
//
// The snapshot-and-clear runs with interrupts masked so an event raised midway
// through cannot be dropped: without that, an ISR firing between the read and
// the clear would have its bit erased and its message lost.
//-----------------------------------------------------------------------------
static void poll_rtt_events(void)
{
    unsigned int pending, react, bar;
    int          m;

    if (!ev_pending)                     // fast path: nothing happened
        return;

    unsigned int prev = read_csr(mstatus);
    clear_csr(mstatus, MSTATUS_MIE);
    pending    = ev_pending;
    m          = ev_mode;
    react      = ev_react;
    bar        = ev_bar;
    ev_pending = 0;
    if (prev & MSTATUS_MIE) set_csr(mstatus, MSTATUS_MIE);

    if (pending & EV_MODE)
        rtt_printf("[key ] mode -> %d (%s)\n", m, MODE_NAMES[m]);
    if (pending & EV_REACT)
        rtt_printf("[game] reaction %u ticks -> score bar 0x%02x\n", react, bar);
    if (pending & EV_FAIL)
        rtt_printf("[game] fail (false start or too slow)\n");
}

// Timer interval for the current mode (breathing & game run at fixed rates).
static inline unsigned int current_interval(void)
{
    if (mode == MODE_BREATHE) return BREATHE_INTERVAL;
    if (mode == MODE_GAME)    return GAME_INTERVAL;
    return interval;
}

//=============================================================================
// Machine-mode trap handler (direct-mode mtvec target)
//=============================================================================
void __attribute__((interrupt("machine"), aligned(4))) trap_handler(void)
{
    unsigned int cause = read_csr(mcause);

    if (cause == MCAUSE_IRQ_TIMER) {
        switch (mode) {
            case MODE_COUNTER: frame_counter(); break;
            case MODE_SCANNER: frame_scanner(); break;
            case MODE_BAR:     frame_bar();     break;
            case MODE_BREATHE: frame_breathe(); break;
            case MODE_GAME:    frame_game();    break;
            default:           frame_sparkle(); break;
        }
        mtimer_set_next(current_interval());   // re-arm; clears MTIP at source
    }
    else if (cause == MCAUSE_IRQ_KEY) {        // KEY[1] press
        if (mode == MODE_GAME) {
            // KEY is the game's action button
            if (game_state == GAME_WAIT) {         // pressed too early -> fail
                enter_fail();
            } else if (game_state == GAME_GO) {    // valid reaction -> score
                unsigned int react = (unsigned int)mtime_read() - go_time;
                unsigned int bar   = score_bar(react);
                P1_LED_CTRL = bar;
                game_state  = GAME_SCORE;
                game_timer  = GAME_SCORE_HOLD;
                ev_react = react; ev_bar = bar;    // printed from the main loop
                ev_pending |= EV_REACT;
            }
            // GAME_SCORE / GAME_FAIL: ignore further presses (auto-returns)
        } else {
            apply_mode((mode + 1 >= NUM_MODES) ? 0 : mode + 1);   // next mode
            ev_mode = mode;                        // printed from the main loop
            ev_pending |= EV_MODE;
        }
        // clear source first, read back, then the latched MIP[16]
        P1_KEY_SW_IRQ_VAL = KEY1_BIT;
        (void)P1_KEY_SW_IRQ_VAL;
        clear_csr(mip, MIE_PLATFORM0);
    }
    else if (cause == MCAUSE_IRQ_SW) {         // SW change -> dir + speed
        unsigned int sw = P1_KEY_SW_VAL & SW_MASK;
        led_dir  = (sw & 0x1u) ? -1 : 1;       // SW[0]
        interval = speed_interval[(sw >> 1) & 0x3u];  // SW[2:1]
        // clear source first, read back, then the latched MIP[17]
        P1_KEY_SW_IRQ_VAL = SW_MASK;
        (void)P1_KEY_SW_IRQ_VAL;
        clear_csr(mip, MIE_PLATFORM1);
    }
}

//=============================================================================
// main
//=============================================================================
int main(void)
{
    SEGGER_RTT_Init();
    rtt_printf("\n=== aRVern leds demo ===\n");
    rtt_printf("RTT up: %d bytes, down: %d bytes\n",
               BUFFER_SIZE_UP, BUFFER_SIZE_DOWN);

    mode        = MODE_COUNTER;
    counter_val = 0;
    lfsr        = 0xA5u;                  // nonzero LFSR seed
    reset_mode_state();

    // Sample the live switches at reset so direction/speed match the physical SW
    // positions from the first frame: the SW IRQ only fires on a *change*, so
    // without this the settings would stay at their defaults until the user
    // touched a switch.
    unsigned int sw = P1_KEY_SW_VAL & SW_MASK;
    led_dir  = (sw & 0x1u) ? -1 : 1;               // SW[0]   -> direction
    interval = speed_interval[(sw >> 1) & 0x3u];   // SW[2:1] -> speed
    rtt_printf("switches 0x%x -> dir %s, interval %u\n",
               sw, (led_dir > 0) ? "up" : "down", interval);

    // Peripheral IRQ config:
    //   SW[2:0] -> any-edge (live dir/speed updates)
    //   KEY[1]  -> falling edge (active-low press)
    P1_KEY_SW_IRQ_VAL  = 0x3F;           // clear stale pending [5:0]
    P1_KEY_SW_IRQ_EDGE = 0x00;           // KEY[1] (bit5) falling = press
    P1_KEY_SW_IRQ_BOTH = 0x07;           // SW[2:0] any-edge
    P1_KEY_SW_IRQ_EN   = KEY1_BIT | 0x07;// KEY[1] + SW[2:0]

    // First frame + first timer deadline
    P1_LED_CTRL = 0x00;
    mtimer_set_next(interval);

    // Trap vector (direct) + enable timer, KEY, SW interrupts
    write_csr(mtvec, (unsigned int)&trap_handler);
    set_csr(mie, MIE_MTIE | MIE_PLATFORM0 | MIE_PLATFORM1);
    set_csr(mstatus, MSTATUS_MIE);

    rtt_printf("running: KEY[1] cycles mode, SW[0] dir, SW[2:1] speed\n");
    rtt_printf("commands: send a digit 0-%d to select a mode "
               "(0=COUNTER 1=SCANNER 2=BAR 3=SPARKLE 4=BREATHE 5=GAME)\n",
               NUM_MODES - 1);

    // Drawing stays fully interrupt-driven; `wfi` returns on every timer tick
    // (and on KEY/SW), which is what paces the RTT command poll. With no
    // debugger attached the poll finds an empty buffer and costs a few cycles.
    for (;;) {
        __asm__ volatile ("wfi");
        poll_rtt_events();               // print what the ISRs recorded
        poll_rtt_commands();             // host -> target keystrokes
    }

    return 0;
}

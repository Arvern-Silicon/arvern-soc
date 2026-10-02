/*----------------------------------------------------------------------------
 * busmash -- data-bus saturation firmware for SBA arbitration testing
 *
 * THIS IS NOT A DEMO. It blinks nothing and produces no output. Its only job is
 * to keep the core's data AHB port as busy as possible, so that the Debug
 * Module's System Bus Access has to arbitrate for the bus while a debugger reads
 * and writes memory on the running hart. Run it with debug/arvern-stress.cfg,
 * which detects it automatically and polls the control block below.
 * See README.md for the full picture.
 *
 * WHY IT EXISTS
 *   aRVern's SBA shares the core's data AHB port and arbitrates for it, so a
 *   debugger can access memory without halting the hart. On the FPGA that path
 *   is only meaningfully tested if the hart actually competes for the bus --
 *   and ordinary firmware does not. With the `leds` demo loaded, exactly 1 of 16
 *   .bss words changed across 200 debugger reads, and an 8 KiB debugger block
 *   read took the same time whether the hart was halted or running: the arbiter
 *   was never asked to arbitrate. Any "SBA works on a running hart" result
 *   gathered that way is a best case. This firmware closes that blind spot.
 *
 * WHAT IT DOES
 *   An unbounded store -> load -> verify loop over its own private region, using
 *   an ADDRESS-TAGGED pattern (&word ^ seed) rather than a constant, so a
 *   mis-arbitrated transfer that returns the *wrong word* is caught, not merely
 *   one that returns garbage. Results are published in the control block.
 *
 * THE INVARIANT EVERYTHING RESTS ON -- disjoint memory
 *   The hart and the debugger must never touch the same bytes; that is what
 *   makes corruption attributable. link.ld enforces it:
 *
 *     0x80000000 +24K  this firmware's .data/.bss/.stack
 *     0x80006000  +4K  the DEBUGGER's scratch window -- NEVER touched here
 *     0x80007000  +4K  the hammer region + control block -- debugger never writes
 *
 *   So corruption is detectable in BOTH directions: ERRS below catches
 *   debugger->hart, and the stress script's address-tagged pattern over the
 *   0x80006000 window catches hart->debugger. If you extend this firmware, do
 *   not let it write outside its own region.
 *
 * IF YOU CHANGE THE OPTIMIZATION FLAGS
 *   A write-then-read-back-then-compare loop is exactly what GCC deletes. The
 *   pointers are volatile to prevent that; verify with
 *     grep -cE '\t(sw|lw)' busmash.lst
 *   If that reaches zero the loop is gone and every test using it passes
 *   vacuously.
 *---------------------------------------------------------------------------*/

#include <stdint.h>

/* Region and control block placed by link.ld -- no addresses hardcoded here. */
extern uint32_t __hammer_start[];
extern uint32_t __hammer_end[];
extern uint32_t __hammer_cb[];

/* Control block, polled over SBA while this loop runs. Offsets are contractual
 * with debug/arvern-stress.cfg. */
enum {
    CB_MAGIC = 0,   /* 'HAMR' -- proves the firmware is loaded AND this layout */
    CB_HB,          /* heartbeat: ++ per pass. Frozen => the hart died or stalled */
    CB_ERRS,        /* readback mismatches. Nonzero => debugger corrupted the hart */
    CB_PASSES,      /* completed verify passes */
    CB_BAD_ADDR,    /* first/last mismatch: where */
    CB_BAD_GOT,     /*                      what we read */
    CB_BAD_WANT,    /*                      what we wrote */
};

#define HAMMER_MAGIC 0x48414D52u   /* 'H','A','M','R' */

int main(void)
{
    volatile uint32_t *cb  = (volatile uint32_t *)__hammer_cb;
    volatile uint32_t *reg = (volatile uint32_t *)__hammer_start;
    const uint32_t words   = (uint32_t)(__hammer_end - __hammer_start);

    for (int i = 0; i < 7; i++) {
        cb[i] = 0;
    }
    cb[CB_MAGIC] = HAMMER_MAGIC;

    uint32_t seed = 0;

    for (;;) {
        /* Store pass: every word carries its own address, xor a rotating seed. */
        for (uint32_t i = 0; i < words; i++) {
            reg[i] = (uint32_t)(uintptr_t)&reg[i] ^ seed;
        }

        /* Load pass: read it straight back. `reg` is volatile, so the compiler
         * cannot fold this into the stores above -- check the .lst still shows
         * sw/lw pairs after any optimisation-flag change. */
        for (uint32_t i = 0; i < words; i++) {
            uint32_t want = (uint32_t)(uintptr_t)&reg[i] ^ seed;
            uint32_t got  = reg[i];
            if (got != want) {
                cb[CB_ERRS]++;
                cb[CB_BAD_ADDR] = (uint32_t)(uintptr_t)&reg[i];
                cb[CB_BAD_GOT]  = got;
                cb[CB_BAD_WANT] = want;
            }
        }

        cb[CB_PASSES]++;
        cb[CB_HB]++;
        seed += 0x01010101u;
    }
}

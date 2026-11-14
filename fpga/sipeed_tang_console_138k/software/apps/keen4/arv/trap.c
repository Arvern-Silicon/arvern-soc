/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      trap.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Traps of the Keen 4 port (arv_trap_entry, startup.S):
 *   - interrupts go to the handler installed with arv_irq_handler (sound)
 *   - a misaligned load or store (causes 4 and 6: the core does not split
 *     them) is done byte by byte and the program goes on; they are counted,
 *     and the first ones are reported on the TTY with their PC, so the code
 *     doing them can be found and changed
 *   - anything else is reported (cause, PC, value) and stops the program
 * The faulting instruction is decoded for its register and its width: LH,
 * LHU, LW, SH, SW, and the compressed C.LW, C.SW, C.LWSP, C.SWSP, C.LH,
 * C.LHU, C.SH; the address is mtval.
 *----------------------------------------------------------------------------*/
#include <stdint.h>
#include "tang.h"


static void (*irq_handler)(void);
uint32_t arv_misaligned;                        /* emulated accesses             */

void arv_irq_handler(void (*h)(void)) { irq_handler = h; }

static void fatal(uint32_t cause, uint32_t pc, uint32_t val)
{
    arv_printf("keen4: trap cause %u at pc %x (value %x)\n", (unsigned) cause, (unsigned) pc, (unsigned) val);
    arv_halt();
}

uint32_t arv_trap(uint32_t *x, uint32_t cause, uint32_t pc, uint32_t addr)
{
    uint32_t ins, len, reg, size = 0, sext = 0, v;
    volatile uint8_t *p = (volatile uint8_t *) addr;
    if (cause & 0x80000000u) {
        if (irq_handler)
            irq_handler();
        return pc;
    }
    if (cause != 4u && cause != 6u)
        fatal(cause, pc, addr);
    ins = *(volatile uint16_t *) pc;
    if ((ins & 3u) == 3u) {
        ins |= (uint32_t) *(volatile uint16_t *) (pc + 2u) << 16;
        len = 4u;
        const uint32_t f3 = (ins >> 12) & 7u;
        if ((ins & 0x7Fu) == 0x03u) {           /* LOAD  */
            reg = (ins >> 7) & 31u;
            size = (f3 & 3u) == 1u ? 2u : (f3 & 3u) == 2u ? 4u : 0u;
            sext = f3 == 1u;
        } else if ((ins & 0x7Fu) == 0x23u) {    /* STORE */
            reg = (ins >> 20) & 31u;
            size = f3 == 1u ? 2u : f3 == 2u ? 4u : 0u;
        } else {
            reg = 0u;
        }
    } else {
        const uint32_t q = ins & 3u, f3 = ins >> 13;
        len = 2u;
        reg = 0u;
        if (q == 0u && (f3 == 2u || f3 == 6u)) {            /* C.LW / C.SW            */
            reg = 8u + ((ins >> 2) & 7u);
            size = 4u;
        } else if (q == 0u && f3 == 4u) {                   /* Zcb: C.LH(U), C.SH     */
            const uint32_t op = (ins >> 10) & 7u;
            reg = 8u + ((ins >> 2) & 7u);
            if (op == 1u || op == 3u) {
                size = 2u;
                sext = (op == 1u) && ((ins >> 6) & 1u);
            }
        } else if (q == 2u && (f3 == 2u || f3 == 6u)) {     /* C.LWSP / C.SWSP        */
            reg = f3 == 2u ? (ins >> 7) & 31u : (ins >> 2) & 31u;
            size = 4u;
        }
    }
    if (!size || reg > 15u)
        fatal(cause, pc, addr);
    if (arv_misaligned++ < 16u)
        arv_printf("keen4: misaligned %s at pc %x, address %x\n", cause == 4u ? "load" : "store",
                   (unsigned) pc, (unsigned) addr);
    if (cause == 4u) {
        v = p[0] | (uint32_t) p[1] << 8;
        if (size == 4u)
            v |= (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
        else if (sext)
            v = (uint32_t) (int32_t) (int16_t) v;
        if (reg)
            x[reg] = v;
    } else {
        v = x[reg];
        p[0] = (uint8_t) v;
        p[1] = (uint8_t) (v >> 8);
        if (size == 4u) {
            p[2] = (uint8_t) (v >> 16);
            p[3] = (uint8_t) (v >> 24);
        }
    }
    return pc + len;
}

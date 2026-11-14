/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      led_key_sw.c
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Driver of the DE0-Nano-SoC LED / key / switch peripheral.
 *----------------------------------------------------------------------------*/
#include "led_key_sw.h"

void lks_irq_config(lks_t *dev, uint32_t mask, lks_edge_t edge)
{
    LKS_TypeDef *r = dev->regs;
    mask &= LKS_SW_ALL | LKS_KEY_ALL;
    r->IRQ_BOTH = (edge == LKS_EDGE_BOTH)    ? (r->IRQ_BOTH | mask) : (r->IRQ_BOTH & ~mask);
    r->IRQ_EDGE = (edge == LKS_EDGE_RISING)  ? (r->IRQ_EDGE | mask) : (r->IRQ_EDGE & ~mask);
    r->IRQ_VAL  = mask;                                   /* drop stale events */
    r->IRQ_EN  |= mask;
}

void lks_irq_disable(lks_t *dev, uint32_t mask)
{
    dev->regs->IRQ_EN &= ~mask;
    dev->regs->IRQ_VAL = mask;
}

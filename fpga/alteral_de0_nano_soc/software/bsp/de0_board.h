/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      de0_board.h
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * DE0-Nano-SoC example SoC: memory map, clocks and interrupt wiring
 * (rtl/verilog/ahb_decoder.v, ahb_bus_system.v, arvern_fpga.v).
 *----------------------------------------------------------------------------*/
#ifndef DE0_BOARD_H
#define DE0_BOARD_H

/* Clocks */
#define BOARD_HCLK_HZ         50000000u     /* core and bus clock                 */
#define BOARD_MTIME_HZ        5000000u      /* ACLINT mtime tick (clk_lf)         */

/* Timebase handed to the ACLINT driver: the board-level simulation runs the
 * firmware's delays 500 times faster */
#ifdef SIMULATION
#define BOARD_TIME_HZ         (BOARD_MTIME_HZ / 500u)
#else
#define BOARD_TIME_HZ         BOARD_MTIME_HZ
#endif

/* Memory map */
#define BOARD_ROM_BASE        0x20000000u   /* program memory, 48 KB, executable  */
#define BOARD_SRAM_BASE       0x80000000u   /* data SRAM, 48 KB                   */
#define BOARD_VRAM_BASE       0x82000000u   /* Belenos VRAM window, 160 KB        */
#define BOARD_VRAM_WORDS      40960u        /* two 320 x 240 frames + 10 KB spare */
#define BOARD_ACLINT_BASE     0x02000000u   /* ACLINT: MSWI, MTIMER, SSWI         */
#define BOARD_PLIC_BASE       0x0C000000u   /* PLIC, 4 MB window                  */
#define BOARD_LKS_BASE        0x10040000u   /* LEDs / keys / switches             */
#define BOARD_BLNS_BASE       0x10050000u   /* Belenos register port              */
#define BOARD_GPIO_BASE       0x10041000u   /* GPIO, pins on GPIO_1[25:0]         */
#define BOARD_GPIO_NR         26u           /* GPIO_NR                            */
#define BOARD_GPIO_ARD_BASE   0x10042000u   /* GPIO, pins on ARDUINO_IO[15:0]     */
#define BOARD_GPIO_ARD_NR     16u           /* GPIO_NR: D0-D13, SDA (14), SCL (15) */

/* Platform interrupts (irq_platform_i[n] -> mip[16+n], mcause 16+n) */
#define BOARD_IRQ_BELENOS     16u           /* Belenos irq_o                      */

/* PLIC (NUM_SOURCES 47, NUM_HARTS 1, SU_MODE_EN 0, PRIO_BITS 3): context 0
 * drives mip.MEIP (mcause 11); the core has no S-mode, so no S context. KEY
 * and SW reach the hart only through the PLIC; sources 29-31 are tied 0,
 * 32-47 are the Arduino GPIO. */
#define BOARD_PLIC_SOURCES      47u
#define BOARD_PLIC_HARTS        1u
#define BOARD_PLIC_PRIO_BITS    3u
#define BOARD_PLIC_SU_MODE      0
#define BOARD_PLIC_SRC_GPIO(n)  (1u + (n))  /* GPIO pin n, n = 0..25              */
#define BOARD_PLIC_SRC_KEY      27u         /* KEY edge (apb_led_key_sw)          */
#define BOARD_PLIC_SRC_SW       28u         /* SW edge  (apb_led_key_sw)          */
#define BOARD_PLIC_SRC_GPIO_ARD(n) (32u + (n)) /* Arduino GPIO pin n, n = 0..15    */

#endif /* DE0_BOARD_H */

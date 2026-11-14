/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      tang_board.h
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Tang Console 138K example SoC: memory map, clocks and interrupt wiring
 * (rtl/verilog/ahb_decoder.v, ahb_bus_system.v, arvern_fpga.v).
 *----------------------------------------------------------------------------*/
#ifndef TANG_BOARD_H
#define TANG_BOARD_H

/* Display: one firmware runs on both bitstreams, the RGB panel build
 * (Belenos DIF_TYPE 4) and the HDMI build (DIF_TYPE 5, its own 27 MHz pixel
 * clock); BOARD_HDMI() reads the build from BLNS_CFG.DIF_TYPE ([15:13]), for
 * the programs without the Belenos driver (the Belenos ones: tang_video.h,
 * from the driver's capabilities) */
#define BOARD_HDMI()          ((((*(volatile unsigned int *) 0x10050004u) >> 13) & 7u) == 5u)

/* Clocks */
#define BOARD_HCLK_HZ         33854167u     /* core and bus clock, both builds    */
#define BOARD_MTIME_HZ        5642361u      /* ACLINT mtime tick (clk_lf, hclk/6) */
#define BOARD_SD_DATA_HZ      ((BOARD_HCLK_HZ + 1u) / 2u)  /* microSD data rate: SPI clock hclk/2 (rounded up, as the divider is) */

/* Timebase handed to the ACLINT driver: the board-level simulation runs the
 * firmware's delays 500 times faster */
#ifdef SIMULATION
#define BOARD_TIME_HZ         (BOARD_MTIME_HZ / 500u)
#else
#define BOARD_TIME_HZ         BOARD_MTIME_HZ
#endif

/* Memory map */
#define BOARD_ROM_BASE        0x20000000u   /* program memory, 160 KB, executable */
#define BOARD_SRAM_BASE       0x80000000u   /* data SRAM, 128 KB                  */
#define BOARD_ROM_SIZE        0x28000u      /* ROM_SIZE of arvern_fpga.v          */
#define BOARD_SRAM_SIZE       0x20000u      /* SRAM_NX_SIZE of arvern_fpga.v      */
#define BOARD_VRAM_BASE       0x82000000u   /* Belenos VRAM window, 272 KB        */
#define BOARD_VRAM_WORDS      69632u        /* two 480 x 272 frames + 17 KB spare */

/* RGB panel (Sipeed 4.3-inch) */
#define BOARD_LCD_W           480u
#define BOARD_LCD_H           272u

/* RGB panel timing (blns_dpi_timing_t): 480 x 272 at 60 Hz, 11.3 MHz pixel
 * clock, 535 x 346 per frame: clk_sys / 3, or the build's own 11.285 MHz
 * pixel clock when DPI_CAPS reports DPI_PCLK_SEP. The panel latches its data
 * on the clock's falling edge: on its own pixel clock (a pixel a cycle) the
 * rising edge would fall where the data changes. BOARD_LCD_PCLK_SEP() reads
 * DPI_CAPS; after blns_init: caps.dpi_caps & BLNS_DPI_CAPS_PCLK_SEP_Msk */
#define BOARD_LCD_PCLK_SEP()  (((*(volatile unsigned int *) 0x100501FCu) >> 6) & 1u)
#define BOARD_LCD_TIMING_SEP(sep) { (sep) ? 0u : 2u, 480u, 8u, 4u, 43u, 272u, 52u, 10u, 12u, BLNS_DPI_EDGE_FALL }
#define BOARD_LCD_TIMING      BOARD_LCD_TIMING_SEP(BOARD_LCD_PCLK_SEP())

/* HDMI timing (blns_dpi_timing_t): 720 x 480 at 60 Hz on the 27.03 MHz pixel
 * clock, CTA-861 VIC 3 (a 16:9 picture), with its AVI InfoFrame values */
#define BOARD_HDMI_TIMING     BLNS_DPI_720X480_60
#define BOARD_HDMI_VIC        3u
#define BOARD_HDMI_PICTURE    BLNS_AVI_PICTURE_16_9
/* HDMI audio (a bitstream with DPI_AUDIO_EN): the I2S stream at 44.08 kHz
 * (hclk / 768), sent as 44.1 kHz 16-bit audio (N = 6272, the CTS measured);
 * the Source Product Description the TV may show */
#define BOARD_HDMI_AUDIO_N    6272u
#define BOARD_HDMI_VENDOR     "aRVern"
#define BOARD_HDMI_PRODUCT    "Tang Console"
/* HDMI picture in the signal. BOARD_HDMI_STRETCH 1: the display stretches
 * the signal to its full width (the usual setting); the picture is sized to
 * keep its own shape there, from the display's shape in its EDID, or
 * BOARD_HDMI_ASPECT (width / height x 100: 178 = 16:9) when it has none.
 * 0: the picture is the largest of its shape in signal pixels, letterboxed
 * (AVI active format 16:9) */
#define BOARD_HDMI_STRETCH    1
#define BOARD_HDMI_ASPECT     178u
#define BOARD_ACLINT_BASE     0x02000000u   /* ACLINT: MSWI, MTIMER, SSWI         */
#define BOARD_PLIC_BASE       0x0C000000u   /* PLIC, 4 MB window                  */
#define BOARD_LKS_BASE        0x10040000u   /* LEDs / keys / switches             */
#define BOARD_BLNS_BASE       0x10050000u   /* Belenos register port              */
#define BOARD_GPIO_PMOD_BASE  0x10041000u   /* GPIO: 0-7 PMOD0, 8-15 PMOD1        */
#define BOARD_GPIO_PMOD_NR    16u           /* GPIO_NR                            */
#define BOARD_I2S_BASE        0x10042000u   /* I2S master: speaker or Pmod I2S2   */
#define BOARD_USB0_BASE       0x10043000u   /* USB host (apb_usb_host), USB-A J15  */
#define BOARD_USB1_BASE       0x10044000u   /* USB host (apb_usb_host), USB-A J16  */
#define BOARD_SPI_BASE        0x10045000u   /* SPI controller: microSD slot       */

/* Platform interrupts (irq_platform_i[n] -> mip[16+n], mcause 16+n) */
#define BOARD_IRQ_BELENOS     16u           /* Belenos irq_o                      */

/* I2S master (apb_i2s, 16-word FIFOs): the audio clocks divide hclk. Its
 * stream goes to the output the board I/O AUDIO_OUT selects
 * (lks_audio_output): HDMI, the onboard speaker or a Pmod I2S2 on PMOD1
 * (the Pmod needs MCLK). Pmod I2S2 on PMOD1: D/A MCLK 8, LRCK 10, SCLK 12,
 * SDIN 14; A/D MCLK 9, LRCK 11, SCLK 13, SDOUT 15 (JP1 on SLV); those GPIO
 * pins belong to the Pmod while it is the output */
#define BOARD_I2S_PMOD_PINS   0xFF00u       /* GPIO pins of the Pmod I2S2         */

/* SPI controller (apb_spi, LANES_MAX 1, CS_NR 1, 16-entry FIFOs): SCK
 * divides hclk. The microSD slot in SPI mode: CLK = SCK, CMD = IO0, DAT0 =
 * IO1, DAT3 = chip select 0. The slot is shared with the BL616 through an
 * analog switch the BL616 side selects. Card detect: lks_sd_det_level */
#define BOARD_SPI_CS_SD       0u            /* chip select of the microSD slot    */

/* PLIC (NUM_SOURCES 31, NUM_HARTS 1, SU_MODE_EN 0, PRIO_BITS 3): context 0
 * drives mip.MEIP (mcause 11); the core has no S-mode, so no S context. KEY,
 * SW, the I2S master, the SPI controller and the USB host ports reach the hart
 * only through the PLIC; sources 19-26 and 31 are tied 0. */
#define BOARD_PLIC_SOURCES      31u
#define BOARD_PLIC_HARTS        1u
#define BOARD_PLIC_PRIO_BITS    3u
#define BOARD_PLIC_SU_MODE      0
#define BOARD_PLIC_SRC_GPIO_PMOD(n) (1u + (n)) /* PMOD GPIO pin n, n = 0..15      */
#define BOARD_PLIC_SRC_I2S      17u         /* I2S master (apb_i2s irq_o)         */
#define BOARD_PLIC_SRC_SPI      18u         /* SPI controller (apb_spi irq_o)     */
#define BOARD_PLIC_SRC_KEY      27u         /* KEY edge (apb_board_io)            */
#define BOARD_PLIC_SRC_SW       28u         /* SW edge  (apb_board_io)            */
#define BOARD_PLIC_SRC_USB0     29u         /* USB host J15 (apb_usb_host irq_o)    */
#define BOARD_PLIC_SRC_USB1     30u         /* USB host J16 (apb_usb_host irq_o)    */

#endif /* TANG_BOARD_H */

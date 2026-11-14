//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K microSD driver test)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// tst_sd -- the microSD slot through the apb_spi driver and its SD-card
// layer (apb_spi at 0x10045000): card initialisation, then 512-byte blocks
// at hclk / 2 (16.9 MHz).
//
// Simulation (-DSIMULATION): the stimulus (src/tst_sd.v) puts the apb_spi
// bench's SD card model on the slot (16 blocks, byte i = 7 i + 13 (i / 512)).
// LED register: 0x30 memory map (a word written and read back at the top of
// every program-memory and SRAM bank above the image and the stack, the
// program memory through the data port), 0x31 init, 0x32 single-block read
// (block 1), 0x33
// multi-block read (2-3), 0x34 single-block write (5), 0x35 multi-block write
// (6-7), 0x36 read back (5-7); 0xA5 pass, 0xE0 | step on the first failure.
//
// Board: initialise the card and read block 0, once a second. LEDs: 0xA5 the
// block ends with the 0x55 0xAA boot-sector signature, 0xA0 read but no
// signature, 0xE1 initialisation failed (no card, or the slot switched to the
// BL616), 0xE2 the read failed.
//----------------------------------------------------------------------------

#include <stdint.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "apb_spi.h"
#include "apb_spi_sd.h"

#define LED_PASS       0xA5u
#define LED_FAIL       0xE0u
#define PCLK_HZ        33333333u
#define DATA_HZ        BOARD_SD_DATA_HZ
#define WORDS          128u                     // per block

extern char         __romdatastart[], __romdatacopysize[], _stack_end[];

static lks_t        lks;
static apb_spi_t    spi;
static apb_spi_sd_t card;
static uint32_t     buf[3u * WORDS];

#ifdef SIMULATION

static void fail(unsigned step)
{
    lks_led_set(&lks, LED_FAIL | step);
    for (;;)
        ;
}

// Byte i of the card: the model's initial content, or the pattern this test
// writes into blocks 5-7
static uint8_t byte_of(uint32_t i, int written)
{
    uint32_t b = i >> 9;
    return written ? (uint8_t) (i * 3u + b * 29u + 0x5Au) : (uint8_t) (i * 7u + b * 13u);
}

// Word w of blocks [first, ...) in the driver's order (byte 0 in bits 7:0)
static uint32_t word_of(uint32_t first, uint32_t w, int written)
{
    uint32_t i = first * 512u + 4u * w;
    return (uint32_t) byte_of(i, written) | ((uint32_t) byte_of(i + 1u, written) << 8) |
           ((uint32_t) byte_of(i + 2u, written) << 16) | ((uint32_t) byte_of(i + 3u, written) << 24);
}

static void fill(uint32_t first, uint32_t n, int written)
{
    for (uint32_t w = 0u; w < n * WORDS; w++)
        buf[w] = word_of(first, w, written);
}

// The top word of every 8 KB bank above `low` in [base, base + size)
static void mem_check(uint32_t base, uint32_t size, uint32_t low)
{
    for (uint32_t a = base + 8188u; a < base + size; a += 8192u) {
        volatile uint32_t *w = (volatile uint32_t *) a;
        if (a < low)
            continue;
        *w = a ^ 0x5A5AA5A5u;
        if (*w != (a ^ 0x5A5AA5A5u))
            fail(0u);
    }
}

static void check(uint32_t first, uint32_t n, int written, unsigned step)
{
    for (uint32_t w = 0u; w < n * WORDS; w++)
        if (buf[w] != word_of(first, w, written))
            fail(step);
}

int main(void)
{
    const apb_spi_config_t cfg = { .base = BOARD_SPI_BASE };

    lks_init(&lks, BOARD_LKS_BASE);

    lks_led_set(&lks, 0x30u);
    mem_check(BOARD_ROM_BASE, BOARD_ROM_SIZE, (uint32_t) &__romdatastart + (uint32_t) &__romdatacopysize);
    mem_check(BOARD_SRAM_BASE, BOARD_SRAM_SIZE, (uint32_t) &_stack_end);

    lks_led_set(&lks, 0x31u);
    if ((apb_spi_init(&spi, &cfg) != APB_SPI_OK) || (spi.lanes_max != 1u) || (spi.cs_nr != 1u))
        fail(1u);
    if (apb_spi_sd_init(&card, &spi, BOARD_SPI_CS_SD, PCLK_HZ, DATA_HZ, 1u) != APB_SPI_SD_OK)
        fail(1u);
    if (!card.block_addr)
        fail(1u);

    lks_led_set(&lks, 0x32u);
    if (apb_spi_sd_read(&card, 1u, buf, 1u) != APB_SPI_SD_OK)
        fail(2u);
    check(1u, 1u, 0, 2u);

    lks_led_set(&lks, 0x33u);
    if (apb_spi_sd_read(&card, 2u, buf, 2u) != APB_SPI_SD_OK)
        fail(3u);
    check(2u, 2u, 0, 3u);

    lks_led_set(&lks, 0x34u);
    fill(5u, 1u, 1);
    if (apb_spi_sd_write(&card, 5u, buf, 1u) != APB_SPI_SD_OK)
        fail(4u);

    lks_led_set(&lks, 0x35u);
    fill(6u, 2u, 1);
    if (apb_spi_sd_write(&card, 6u, buf, 2u) != APB_SPI_SD_OK)
        fail(5u);

    lks_led_set(&lks, 0x36u);
    for (uint32_t w = 0u; w < 3u * WORDS; w++)
        buf[w] = 0u;
    if (apb_spi_sd_read(&card, 5u, buf, 3u) != APB_SPI_SD_OK)
        fail(6u);
    check(5u, 3u, 1, 6u);

    lks_led_set(&lks, LED_PASS);
    for (;;)
        ;
}

#else

int main(void)
{
    const apb_spi_config_t cfg = { .base = BOARD_SPI_BASE };

    lks_init(&lks, BOARD_LKS_BASE);
    (void) apb_spi_init(&spi, &cfg);
    for (;;) {
        unsigned led;
        if (apb_spi_sd_init(&card, &spi, BOARD_SPI_CS_SD, PCLK_HZ, DATA_HZ, 1u) != APB_SPI_SD_OK)
            led = LED_FAIL | 1u;
        else if (apb_spi_sd_read(&card, 0u, buf, 1u) != APB_SPI_SD_OK)
            led = LED_FAIL | 2u;
        else
            led = ((buf[WORDS - 1u] >> 16) == 0xAA55u) ? LED_PASS : 0xA0u;
        lks_led_set(&lks, led);
        for (volatile uint32_t d = 0u; d < 2000000u; d++)
            ;
    }
}

#endif

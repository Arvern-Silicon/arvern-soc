/* FatFs disk layer on the microSD slot (apb_spi SD layer), one drive */
#include <stdint.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "tang_board.h"
#include "apb_spi.h"
#include "apb_spi_sd.h"

#define PCLK_HZ  33333333u
#define DATA_HZ  BOARD_SD_DATA_HZ

static apb_spi_t    spi;
static apb_spi_sd_t card;
static DSTATUS      stat = STA_NOINIT;
static uint32_t     bounce[128];                 /* word-aligned block buffer */

DSTATUS disk_status(BYTE pdrv) { return pdrv ? STA_NOINIT : stat; }

DSTATUS disk_initialize(BYTE pdrv)
{
    const apb_spi_config_t cfg = { .base = BOARD_SPI_BASE };
    if (pdrv) return STA_NOINIT;
    if (apb_spi_init(&spi, &cfg) != APB_SPI_OK) return stat;
    /* A card left in the middle of a transfer (the previous program reset)
       can miss the first initialisation: a few tries, 10 ms apart */
    for (unsigned k = 0u; (k < 4u) && (stat & STA_NOINIT); k++) {
        if (k)
            for (volatile uint32_t d = 0u; d < PCLK_HZ / 400u; d++)
                ;
        if (apb_spi_sd_init(&card, &spi, BOARD_SPI_CS_SD, PCLK_HZ, DATA_HZ, 1u) == APB_SPI_SD_OK)
            stat = 0;
    }
    return stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || (stat & STA_NOINIT)) return RES_NOTRDY;
    if (((uintptr_t) buff & 3u) == 0u)
        return (apb_spi_sd_read(&card, sector, (uint32_t *) buff, count) == APB_SPI_SD_OK) ? RES_OK : RES_ERROR;
    for (; count; count--, sector++, buff += 512) {
        if (apb_spi_sd_read(&card, sector, bounce, 1u) != APB_SPI_SD_OK) return RES_ERROR;
        memcpy(buff, bounce, 512);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || (stat & STA_NOINIT)) return RES_NOTRDY;
    if (((uintptr_t) buff & 3u) == 0u)
        return (apb_spi_sd_write(&card, sector, (const uint32_t *) buff, count) == APB_SPI_SD_OK) ? RES_OK : RES_ERROR;
    for (; count; count--, sector++, buff += 512) {
        memcpy(bounce, buff, 512);
        if (apb_spi_sd_write(&card, sector, bounce, 1u) != APB_SPI_SD_OK) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void) buff;
    if (pdrv) return RES_PARERR;
    return (cmd == CTRL_SYNC) ? RES_OK : RES_PARERR;
}

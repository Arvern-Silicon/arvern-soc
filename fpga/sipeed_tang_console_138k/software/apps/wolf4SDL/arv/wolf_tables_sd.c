/* The constant tables of port/wolf_tables.c, read from the card at start-up
 * (WOLF_SD_TABLES) instead of taking ROM: TABLES.BIN in the program's folder,
 * written by tools/gen_tables_bin.py from port/wolf_tables.c:
 *     "WTB1", then the entry counts of finetangent, sintable and pixelangle_bp
 *     (32-bit little-endian), then the three tables, little-endian */
#include <stdint.h>
#include <string.h>
#include "wl_def.h"
#include "ff.h"

int32_t  finetangent[FINEANGLES/4];
fixed    sintable[ANGLES+ANGLES/4];
uint16_t pixelangle_bp[363];
const fixed *costable = sintable+(ANGLES/4);
const int pixelangle_bp_n = 363;

static int read_all(FIL *f, void *dst, UINT n)
{
    UINT got = 0;
    return f_read(f, dst, n, &got) == FR_OK && got == n;
}

/* 0 when the tables are loaded */
int wolf_tables_load(void)
{
    FIL      f;
    uint32_t h[4];
    int      ok;

    if (f_open(&f, "TABLES.BIN", FA_READ) != FR_OK)
        return -1;
    ok = read_all(&f, h, sizeof(h)) && memcmp(h, "WTB1", 4) == 0
         && h[1] == FINEANGLES/4 && h[2] == ANGLES+ANGLES/4 && h[3] == 363
         && read_all(&f, finetangent, sizeof(finetangent))
         && read_all(&f, sintable, sizeof(sintable))
         && read_all(&f, pixelangle_bp, sizeof(pixelangle_bp));
    f_close(&f);
    return ok ? 0 : -1;
}

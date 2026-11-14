# FatFs — FAT file system for the Tang Console 138K apps

[FatFs](http://elm-chan.org/fsw/ff/) R0.15a by ChaN (1-clause BSD-style
license, [`LICENSE.txt`](LICENSE.txt)), unmodified, with this board's
configuration and disk layer:

| File | Content |
|---|---|
| `ff.c`, `ff.h`, `diskio.h` | the FatFs module, as distributed |
| `ffconf.h` | the configuration shared by the apps: read-write, FAT12/16/32 (no exFAT), 8.3 names (no long file names), code page 437, relative paths (`f_chdir`), one volume, the tiny buffer option, no real-time clock |
| `sd_diskio.c` | the disk layer: one drive on the microSD slot, through the `apb_spi` driver and its SD-card layer (`arvern-ips/apb_spi/sw`), 16.67 MHz. The card is initialised up to 4 times, 10 ms apart: a card left in the middle of a transfer by the previous program (a reset, an FPGA reload) can miss the first try |

`ff.c` includes the `ffconf.h` next to it, so an app changes the
configuration here, for every app at once. An app adds `ff.c` and
`sd_diskio.c` to its sources, `-I` this folder, the `apb_spi` driver
(`src/apb_spi.c`, `sd/apb_spi_sd.c`) and the board support.

Card layout used by the apps: one folder per program under `/APPS/<NAME>/`,
shared media under `/MEDIA/PICTURES/` and `/MEDIA/MUSIC/`, 8.3 upper-case
names.

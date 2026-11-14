# zork — Zork I, II and III on the Tang Console 138K

Infocom's *Zork* trilogy (1980–1982), the text adventures, run by Ryan C.
Gordon's MojoZork Z-machine on the aRVern SoC of the Tang Console 138K: the
text on Belenos (80 x 25 characters on HDMI, 60 x 17 on the panel), typed on
a USB keyboard or played with a gamepad's command buttons, the story files
and the saved games on the microSD card. It is started from the boot menu
([`../boot_menu/README.md`](../boot_menu/README.md)), which then asks which
of the three games to play.

## Licence

- `stories/`: the three story files (`ZORKn.Z3`), compiled by Infocom and
  released by Microsoft under the MIT licence ([`stories/LICENSE`](stories/LICENSE)).
- `mojozork/`: MojoZork (`mojozork.c`), zlib licence
  ([`mojozork/LICENSE.txt`](mojozork/LICENSE.txt)). Altered for this port:
  two 16-bit copies from story memory use `memcpy` (the core does not do
  unaligned loads); [`SOURCE.txt`](SOURCE.txt) records the versions used.
- The port's own files (`arv/`, `sim/`) are BSD-3-Clause.

## Build and install

```bash
make                         # -> zork.elf (hardware), zork_sim.elf (simulation)
../boot_menu/tools/sd_install.py zork.elf /Volumes/<CARD> --name ZORK \
    --title "Zork I-III" --desc "Infocom, 1980-1982 - the text adventures" \
    --add stories/ZORK1.Z3 --add stories/ZORK2.Z3 --add stories/ZORK3.Z3
```

The program uses about 36 KB of program memory; a story (87 to 93 KB) is
read into the SRAM heap.

## Playing

Type commands on a USB keyboard (Backspace, Enter). A gamepad types whole
commands:

| Button | Command |
|---|---|
| d-pad | north, south, west, east |
| Select + up / down | up, down |
| A | look |
| B | inventory |
| X | take all |
| Y | wait |
| Start | Enter |

`save` and `restore` use `ZORKn.SAV` in `/APPS/ZORK`. A `[MORE]` waits for
a key when a screenful of text has gone by.

## How it works

`arv/zork_main.c` compiles MojoZork inside itself and maps its library
calls: its `fgets` is the console's line editor, its text goes to the
console, its files to FatFs (`arv/stdio_tang.c`), its `save.dat` to the
current game's `ZORKn.SAV`; the status line it computes (room, score,
moves) is drawn inverted at the top before each command. `arv/console.c`
draws the 8 x 16 font of the demo kit into the 1 bit per pixel frame,
wraps words, scrolls and reads the keyboard and the gamepad.

## Simulation

[`sim/zork.v`](sim/zork.v) (linked as `sim/rtl_sim/src/zork.v` to run)
chooses Zork I with A and plays north, east, look and inventory with the
gamepad; the simulation build echoes the story's text on the TTY. The card
image comes from `sim/gen_sdimage.py`:

```bash
sim/gen_sdimage.py sim stories/ZORK1.Z3 stories/ZORK2.Z3 stories/ZORK3.Z3
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NOTRACE" ./run zork -nodump
```

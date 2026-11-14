# wolf4SDL — Wolfenstein 3D on the Tang Console 138K

id Software's Wolfenstein 3D (1992) through Wolf4SDL, its portable C
version, ported to the aRVern SoC of the Tang Console 138K: the picture on
Belenos (320 x 200, 8 bpp, scaled to the panel or the HDMI screen), a USB
gamepad, music and sound effects on the board's sound output (as the boot
menu set it), the game data read from the microSD card. It runs the
shareware episode and is started from the boot menu
([`../boot_menu/README.md`](../boot_menu/README.md)).

Wolf4SDL rather than id's original source: id's release is 16-bit DOS code
(far pointers, a segment memory manager, x86 assembly, scalers generated at
run time, 16-bit `int`), Wolf4SDL is portable 32-bit C with the original game
logic and a small platform layer (video, input, timing, sound, files).

## Licence

- `Wolf4SDL/`: the Wolf4SDL source, `github.com/KS-Presto/Wolf4SDL` at
  commit `dc8b250`, with the port's changes (recorded in
  [`port/wolf4sdl.patch`](port/wolf4sdl.patch)). Wolf4SDL is under GPL-2.0
  or id Software's limited-use licence, at the user's choice
  ([`Wolf4SDL/license-gpl.txt`](Wolf4SDL/license-gpl.txt),
  [`Wolf4SDL/license-id.txt`](Wolf4SDL/license-id.txt)); **this port uses it
  under GPL-2.0**, so the program built here is GPL-2.0. The port's own files
  (`arv/`, `port/`, `stub/`, `tools/`, `sim/`) are BSD-3-Clause, which
  GPL-2.0 accepts. The rest of this repository is not affected.
- `Wolf4SDL/mame/fmopl.c`: the MAME OPL2 emulator (MAME licence,
  non-commercial, [`Wolf4SDL/license-mame.txt`](Wolf4SDL/license-mame.txt)).
  The program does not use it (`port/opl_silent.c` stands in); only the
  host tools that render the music and the sound effects for the card do.
- `third_party/fatfs/`: ChaN's FatFs R0.15a (its 1-clause BSD-style
  licence, [`third_party/fatfs/LICENSE.txt`](third_party/fatfs/LICENSE.txt)).
- The game data is id Software's and is **not** in this repository:
  `tools/fetch_data.py` downloads it (next section).

## Game data

```bash
tools/fetch_data.py          # -> data/
```

downloads the shareware episode v1.4 (`wolf3dsw.zip`, the Internet
Archive's item `wolf3dsw`), checks the archive and each file against their
SHA-256, writes the eight `*.WL1` files into `data/`, then builds the two
host tools (`cc`) and makes `data/SOUNDS.SFX` (the sound effects,
`tools/render_sfx`) and `data/music/MUSnn.ADP` (the songs,
`tools/render_music`).

The card's layout follows the convention of every program on it: one folder
per program under `/APPS`, the program's current directory, which holds its
data, configuration and saves; `/MEDIA/PICTURES` and `/MEDIA/MUSIC` shared;
8.3 upper-case names. `/APPS/WOLF3D/` takes the `*.WL1` files, `SOUNDS.SFX`
and the `.ADP` songs. The program changes to that folder
(`arv_chdir("/APPS/WOLF3D")`, FatFs relative paths) and runs the game with
`--configdir /APPS/WOLF3D`.

## Build and install

```bash
make                         # -> wolf4SDL.elf (hardware), wolf4SDL_sim.elf (simulation)
../boot_menu/tools/sd_install.py wolf4SDL.elf /Volumes/<CARD> --name WOLF3D \
    --title "Wolfenstein 3D" --desc "id Software, 1992" \
    --add data/AUDIOHED.WL1 --add data/AUDIOT.WL1 ...
```

The top `Makefile` builds both images in `arv/` (`obj/`, `obj_sim/`); the
simulation build (`-DSIMULATION`) runs the game clock 500 times faster.
Target: the RV32E core of the Tang (`rv32emc` with Zba, Zbb, Zbs, Zbc, Zcb;
newlib and libgcc from the `rv32ec/ilp32e` multilib), 160 KB of program
memory, 128 KB of SRAM (8 KB stack, the heap the rest), the Belenos VRAM.
The image uses about 148 KB of the program memory.

## Files

| Path | What |
|---|---|
| `Wolf4SDL/` | the game (Wolf4SDL), with the port's changes |
| `arv/` | the Tang build: `Makefile`, start-up and linker script, the SDL subset on Belenos, the gamepad and the timer (`sdl_tang.c`), sound (`audio.c`), VRAM layout and file helpers (`platform.c`), newlib system calls over FatFs (`syscalls.c`), the FatFs disk layer over the `apb_spi` SD layer (`diskio.c`), `ffconf.h`, stubs for the SDL calls the game makes but the port does not need (`sdl_stubs.c`) |
| `port/` | the game-side helpers: constant tables for the integer build (`wolf_tables.c`, made and checked by `gen_tables.c`), integer `atan2` (`wolf_atan2.c`), the silent OPL (`opl_silent.c`), no debug keys (`no_debug.c`), the changes to Wolf4SDL (`wolf4sdl.patch`) |
| `stub/` | just enough of `SDL_mixer.h` and `sys/uio.h` for the cross-compiler |
| `host/` | a macOS build with SDL2 (silent mixer, misaligned-access checks, `WOLF_DUMP=1` saves a frame a second): the same game changes, debugged on the host |
| `tools/` | `fetch_data.py`; `render_sfx.c`, `render_music.c` (the card's sounds and songs); `gen_tables_bin.py`; `play.py`, `dump.py` (the board over the J-Link, below) |
| `sim/` | the Tang simulation: the stimulus, the card model and its image |
| `third_party/fatfs/` | FatFs R0.15a (`source/ffconf.h` is a link to `arv/ffconf.h`: the include next to `ff.c` wins) |
| `measure_size.sh` | compiles every game source for the target and prints the sizes and the largest static arrays |
| `data/` | the game data, from `tools/fetch_data.py` (not in git) |

## How it works

**Changes to the game** (build options of `arv/Makefile`, all in
`port/wolf4sdl.patch`):

| Option | What |
|---|---|
| (always) | `#pragma pack(1)` only on the data-file structures, so the game's own structures stay aligned (a misaligned access traps on the core); `itoa` / `ltoa` as a small decimal conversion (no `snprintf`) |
| `WOLF_NO_SIGNON` | no 64 KB sign-on picture, no "press a key" wait: the attract loop runs without input |
| `WOLF_NO_FLOAT` | no floating point: the tangent, sine and ray-angle tables are constants made on the host with the original formulas (`port/gen_tables.c` checks them for every view width); the actors' `atan2` in whole degrees from a Q24 tangent table with exact 64-bit comparisons (the original's single-precision result but for 2 cases in 397 620 vectors, within 1e-5 degree of a whole degree); par times, view height, sprite rotation and the sound resampler in integer or fixed point |
| `WOLF_FIXEDMUL_INLINE` | the 16.16 multiply inlined (`mul` + `mulh`) |
| `WOLF_PAGE_CACHE` | the `VSWAP` pages (walls, sprites, sounds) read on demand into 4 KB slots in VRAM, the least recently used replaced; one `lseek` and one `read` straight into the slot |
| `WOLF_GR_CACHE` | the `VGAGRAPH` pictures read and expanded on demand: those from 4 KB into one 64000-byte VRAM slot (with the large texts at its end), the others into a 10 KB heap cache, least recently used freed (the two fonts stay, outside that budget); pictures kept planar and read through the planar layout; the Huffman decoder reads through a 256-byte buffer. A map's planes are expanded in that slot (emptied: its pictures are read again when drawn) rather than in heap buffers, which a level started from the menus (a saved game loaded) finds too fragmented |
| `WOLF_MAP_TWO_PLANES`, `WOLF_PLATFORM_MAPS` | the optional third map plane neither allocated nor loaded; the two others in VRAM |
| `WOLF_SHIFTS_ON_DEMAND` | the palette flashes (damage, bonus) computed when needed (no 9 KB of tables) |
| `WOLF_NO_ADLIB_MUSIC`, `WOLF_NO_DIGI` | no OPL synthesis and no 16-bit copies of the digitised sounds on the board: the sound comes from the streamed files instead |
| `WOLF_STREAM_MUSIC`, `WOLF_STREAM_SFX` | music and sound effects streamed from the card (`arv/audio.c`, below) |
| `WOLF_GPU_CLEAR`, `WOLF_GPU_WALLS`, `WOLF_GPU_SPRITES` | ceiling and floor as two GPU fills; every wall column, and every sprite column, a GPU texture span (below) |
| `WOLF_VSYNC` | the view copied to the displayed frame during the vertical blanking: no tearing |
| `WOLF_HEAP_REPORT` | an allocation failure names the caller and the heap used |

Files are opened unbuffered (`arv_fopen`: no 1 KB newlib buffer per file,
FatFs has its own sector buffer) and `arv_fread` serves a request with one
`read()`. `_stat` is implemented over FatFs.

**VRAM** (272 KB): the displayed frame A (`0x00000`) and the drawn frame B
(`0x10000`), 320 x 200 each; the page cache from `0x20000` (16 slots of 4
KB, the last two the audio buffers); the 64000-byte big-picture slot; the
two map planes (2 x 8 KB).

**Video** (`arv/sdl_tang.c`): the game draws into frame B; its screen
update is a Belenos GPU copy of B onto A. The palette is the Belenos LUT;
the DPI scaler fits the 320 x 200 frame to the panel or the HDMI picture
(HDMI: the board's video format with its AVI InfoFrame, HDMI audio and an
SPD InfoFrame; the link is watched and restarted as the demos do, see
[`../../lib/demo_kit/README.md`](../../lib/demo_kit/README.md)). With `WOLF_GPU_WALLS` a wall's 4 KB page in the cache is a
64 x 64 texture whose rows are its columns: each wall column is one
vertical texture span (`BLNS_SWAP_CL`), the texture and the step sent only
when they change; columns under 6 pixels stay on the CPU. Sprites
(`WOLF_GPU_SPRITES`) are expanded from their column posts into a slot of
their own, the texels outside the posts set to a colour the sprite does not
use, which the span skips. The 3D view's copy is not waited for: the next
frame's game logic overlaps it, and every write into frame B waits for the
GPU first. With these options the game runs at 52-60 frames per second in
play.

**Sound** (`arv/audio.c`): mixed at 22.04 kHz and sent twice per I2S frame
(44.08 kHz, an HDMI audio rate) on the output the boot menu selected. The
music is IMA ADPCM (`MUSnn.ADP`, rendered offline from the AdLib songs
through Wolf4SDL's OPL emulator, at 700 register writes a second as its IMF
player); the effects are one bank (`SOUNDS.SFX`: each sound's digitised
version or its rendered AdLib effect, 8-bit at 7042 Hz). The main loop
keeps the buffers filled (a 4 KB ADPCM ring, 1 KB per effect channel, in
VRAM); the I2S `TX_LOW` interrupt makes one stereo sample per free FIFO
word: the music decoded, plus four effect channels (0 the player's weapons,
1 the bosses', 2 and 3 the rest) stepped to the output rate and panned.

**Input**: one SDL joystick from a gamepad on either USB-A port: the d-pad
as the axes, buttons {A, B, X, Y, -, -, Start, -, -, Select}; Wolf4SDL's
joystick handling does the rest (A: attack and select, B: strafe and back,
Start: the menu, Select: the next weapon).

**Card start**: a card left in the middle of a transfer (a program reset,
the FPGA reloaded) misses the first initialisation; `disk_initialize` tries
four times, 10 ms apart.

## On the board, over the J-Link

The firmware keeps counters for a debugger:

| Symbol | Counts |
|---|---|
| `PMMisses`, `PMMissTicks` | page-cache misses, machine-timer ticks spent reading them |
| `CAGrLoads`, `CAGrTicks`, `CAGrLog[16]` | pictures decoded from `VGAGRAPH`, the time, the last 16 chunk numbers |
| `arv_read_calls`, `arv_read_bytes`, `arv_read_ticks` | every `read()` (FatFs `f_read`) |
| `fpscounter` / `fps` | the game's frame counter (set `fpscounter` to 1) |
| `arv_pad_inject` | `USB_BTN_*` bits added to the gamepads: a debugger presses buttons |
| `arv_gpu_walls_on` | 0: the walls drawn by the CPU, for comparison frames |

`tools/play.py elf [load]` (pylink-square) loads the ELF, starts a new game
through `arv_pad_inject`, walks, turns, fires and opens doors, and logs the
counters every second; time is the ACLINT machine timer (`arv_mtime()`).
`tools/dump.py out.png` saves the displayed frame with its palette, read
over the J-Link (`out.png.raw`: the 8-bit pixels, for diffs).

## Simulation

The stimulus ([`sim/wolf4SDL.v`](sim/wolf4SDL.v), linked as
`sim/rtl_sim/src/wolf4SDL.v`) echoes the TTY, saves panel frames and
drives a gamepad through the game: the menu, "Read This!", a new game on
episode 1, a walk to the start room's door and a shot. The card model
(`sim/sd_card_img.v`, the `apb_spi` bench's card with 4096 blocks) reads the
image `sim/gen_sdimage.py` makes from the game data (a 2 MB MBR + FAT12 card
with the card layout, by macOS `hdiutil`):

```bash
sim/gen_sdimage.py sim data/*.WL1          # sim/sd_image.bin, sim/sd_image.mem
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NO_TIMEOUT -D NOTRACE" ./run wolf4SDL -nodump
```

Under Verilator, without the instruction trace, it simulates about 14 ms a
second.

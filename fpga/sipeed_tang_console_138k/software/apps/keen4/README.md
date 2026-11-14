# keen4 — Commander Keen 4 on the Tang Console 138K

id Software's *Commander Keen in Goodbye, Galaxy!* (episode 4, 1991)
through Omnispeak, David Gow's portable C reimplementation of the Keen
Galaxy engine, ported to the aRVern SoC of the Tang Console 138K: the
picture on Belenos at 4 bits per pixel (320 x 200, the 16 EGA colours,
scaled to the panel or the HDMI screen), the level graphics drawn by the
GPU, USB gamepads and keyboards, the PC speaker effects and the AdLib songs
on the board's sound output, the game data read from the microSD card. It
runs the shareware episode and is started from the boot menu
([`../boot_menu/README.md`](../boot_menu/README.md)).

## Licence

- `omnispeak/`: Omnispeak at commit `46cefb3` (its `LICENSE`, `AUTHORS`,
  `README`, the sources the Keen 4 build uses, `data/keen4/`), with the
  port's changes, recorded in [`port/omnispeak.patch`](port/omnispeak.patch).
  Omnispeak is GPL-2.0 ([`omnispeak/LICENSE`](omnispeak/LICENSE)); its OPL2
  emulator (`omnispeak/src/opl/dbopl.c`, the DOSBox team's) too. The program
  built here is GPL-2.0. The port's own files (`arv/`, `tools/`, `sim/`)
  are BSD-3-Clause, which GPL-2.0 accepts; `arv/keen4_vars.c` and `.h` are
  generated from Omnispeak's scripts. The rest of this repository is not
  affected.
- FatFs: the shared copy in [`../../lib/fatfs`](../../lib/fatfs).
- The game data is id Software's and is **not** in this repository:
  `tools/fetch_data.py` downloads it (next section).

## Game data

```bash
tools/fetch_data.py          # -> data/
```

downloads the shareware episode v1.4 as Omnispeak's author distributes it
(`4keen14.zip`), checks the archive and each file against their SHA-256,
writes `AUDIO.CK4`, `EGAGRAPH.CK4` and `GAMEMAPS.CK4` into `data/` with
Omnispeak's own data files (`omnispeak/data/keen4`), then makes the files
the port reads besides them:

| File | Made by | What |
|---|---|---|
| `GFX4BPP.CK4` | `tools/make_gfx.py` | the 16 x 16 tiles, the masked tiles and the sprites at 4 bits per pixel, a 1-bit mask with the masked ones (808 KB) |
| `MUSnn.ADP` | `tools/render_music.c` (the host's `cc`) | the six AdLib songs, played through the OPL2 emulator at 22.04 kHz, IMA ADPCM (2 MB) |
| `TITLE.PNG` | `tools/make_gfx.py` (with Pillow) | the title screen, the boot menu's preview |

The program changes to its folder on the card, `/APPS/KEEN4`, which holds
the `*.CK4` and `MUS*.ADP` files and the saved games and configuration.

## Build and install

```bash
make                         # -> keen4.elf (hardware), keen4_sim.elf (simulation)
../boot_menu/tools/sd_install.py keen4.elf /Volumes/<CARD> --name KEEN4 \
    --title "Commander Keen 4" --desc "Goodbye, Galaxy! - id Software, 1991" \
    --image data/TITLE.PNG --add data/AUDIO.CK4 --add data/EGAGRAPH.CK4 ...
```

(every `data/*.CK4` and `data/MUS*.ADP` with `--add`). The top `Makefile`
builds both images in `arv/` (`obj/`, `obj_sim/`); the simulation build
(`-DSIMULATION`) runs the game clock 500 times faster and reports its frames
and map loads on the TTY. Target: the RV32E core of the Tang (`rv32emc` with
Zba, Zbb, Zbs, Zbc, Zcb; newlib-nano), 160 KB of program memory (the image
uses about 138 KB), 128 KB of SRAM, the Belenos VRAM. The build uses
link-time optimisation and `-msave-restore` (the prologues and epilogues
shared).

## Files

| Path | What |
|---|---|
| `omnispeak/` | the engine, with the port's changes (`ARV_TANG`, `ARV_LINKED_VARS`) |
| `arv/` | the Tang build: `Makefile`, start-up and trap handler (`startup.S`, `trap.c`), the board (`board.c`: Belenos, HDMI hot plug, USB, the timer, the card), the video back-end (`vl_tang.c`), its drawing primitives (`gfx.c`), the tile and sprite cache (`gfxcache.c`), the memory manager (`mm_tang.c`), the file layer over FatFs (`fs_tang.c`), the input, sound and timer back-ends (`in_tang.c`, `sd_tang.c`, `audio.c`), the small C library parts (`libc_tang.c`), the scripts' variables as C (`keen4_vars.c`, `.h`) |
| `port/` | `omnispeak.patch`, the changes to Omnispeak |
| `tools/` | `fetch_data.py`, `make_gfx.py`, `render_music.c`, `gen_vars.py` |
| `sim/` | the Tang simulation: the stimulus and the card image generator |

## How it works

**Memory.** The game was written for 640 KB; here it has 160 KB of program
memory, 128 KB of SRAM and 272 KB of VRAM, so the data moves where the GPU
or the CPU uses it best:

| Where | What |
|---|---|
| program memory | the code, the 370 actions and the scripts' constants (`keen4_vars.c`) |
| SRAM | the game's state, the tile information (23 KB, read by the physics all the time), 8 KB of stack, a 30 KB heap for the game's memory blocks |
| VRAM 0 – 32 KB | the displayed frame, 320 x 200 at 4 bpp |
| VRAM 32 – 40 KB | the music ring |
| VRAM 40 – 44 KB | the GPU command lists that hold the drawing while a present waits for the frame end |
| VRAM 44 – 46 KB | the staging ring: text, 8 x 8 tiles and pictures (in bands) converted for the GPU |
| VRAM 46 – 272 KB | the VRAM heap: the game's other memory blocks (the map, pictures, fonts), its surfaces (the 336 x 224 screen and tile buffer, 37 KB each), and above them the cache: tiles, sprites and the large pictures converted to 4 bpp |

The engine's memory manager (`MM_GetPtr` and its purge levels) is
`mm_tang.c`: a block has an 8-byte header (size, flags, owner, purge level),
goes to the SRAM heap first and to the VRAM heap next, and when neither has
room the purgeable blocks go, the highest level first. The status window's
two surfaces (68 KB) exist only while it is shown.

**The scripts.** Omnispeak reads its variables (1270 integers, strings,
arrays and the 370 actions) from text scripts at start-up, into hash tables,
and looks each one up by name when the game uses it. The Tang build links
them instead: `tools/gen_vars.py` turns what the engine's own parser
produces (`CK_VAR_Dump`, built with `ARV_DUMP_VARS` and `CK_VAR_TYPECHECK`)
into `keen4_vars.c` and `.h`, and every `CK_INT`, `CK_ACTION`, `CK_STRING`
... becomes a constant. That leaves out the tables, the parser and about
900 lookups by name, some of them every frame.

**Video** (`vl_tang.c`, Omnispeak's back-end interface). The surfaces are
4 bpp in the VRAM. The level graphics are drawn by the GPU from the cache
(`gfxcache.c`): a 16 x 16 tile is a copy; a masked tile or a sprite is its
1-bit mask expanded in colour 0 (the drawn pixels cleared) then its pixels
ORed in, what the EGA latches did; the white flash of a hit enemy is the
mask expanded in white. The GPU draws a sprite at any x, so there are no
pre-shifted copies. The cache reads a record from `GFX4BPP.CK4` the first
time it is drawn, into the free VRAM above the heap, from the end down; when
the heap needs that room, or it is full, it is flushed whole and fills again
with what is drawn next. The score box, whose digits the game writes into
the sprite, has its own block. The 1-bit pictures (the fonts, the intro's
bitmaps) and the planar ones (the 8 x 8 tiles, the pictures) are converted
by the CPU into a 2 KB staging ring in the VRAM, in the GPU's form (bit
order reversed, 4 bpp), a picture larger than the ring in bands of lines,
and drawn from there by the GPU, an expansion or a copy, in order with the
rest: the CPU does not wait for the GPU first. Only when the ring wraps
does it wait, once, for the GPU's queued work. A masked planar draw (the
8 x 8 masked tiles, the masked pictures) is two: its opaque pixels cleared
(the inverted mask expanded in colour 0), then its pixels ORed in. A
picture larger than the ring (the title screen, the menu's 320 x 200
background) is converted once into the cache above the heap instead, and
drawn from there, when the cache has room for it with 16 KB left free for
the heap (`gfxcache.c`, at most 8 pictures); it goes with the tiles at a
flush. In the simulation the title screen is cached (the free VRAM then
falls to the 16 KB kept, with no more flushes than before); the menu's
background is not (in the menus and the levels the VRAM is nearly full:
36 KB free at the lowest, in holes) and goes through the ring. A plane-masked fill is two GPU fills (the kept planes
ANDed, the colour's bits ORed), but up to 4 pixels (the Star Wars text, a
pixel at a time) the CPU's, faster there; the plane-masked picture and
1-bit draws are drawn by the CPU, after waiting. Each frame, the
320 x 200 window at the scroll position is copied by the GPU into the
displayed frame, at the end of a refresh unless the scan is past the
picture. The GPU holds such a present until the refresh ends; meanwhile
the next draws (and the next present) are recorded into a command list,
called at the frame end, so the CPU does not wait behind it in the 8-word
command FIFO (`gfx.c`).

**Sound** (`audio.c`). The I2S `TX_LOW` interrupt makes a 22.04 kHz sample
per free FIFO pair (each sent twice, 44.08 kHz): the PC speaker tone the
game programs (a softened square wave) plus the song streamed from the
card (IMA ADPCM, an 8 KB ring the main loop keeps filled). The same
interrupt runs the game's timer service 140 times a second (560 with
music), as the PC's timer interrupt did; the effects are the PC speaker's
(the AdLib setting plays them too).

**Input** (`in_tang.c`). The gamepads and keyboards of both USB-A ports,
as the PC keyboard the game reads by default: the d-pad is the arrows, A
Ctrl (jump; select in the menus), B Alt (pogo), X and Y Space (fire), Start
Esc (the menu), Select Enter (the status window). A keyboard's keys are the
PC's.

**Speed.** On the board the world map runs at about 33 frames a second
while Keen walks, the original's usual pace (a frame every two 70 Hz
ticks).

**Left out.** The intro's scroller (its screen is 1264 pixels wide) and the
story text (a 336 x 1024 surface): the title screen is shown instead; the
debug keys, Paddle War, the other episodes' code.

**Traps.** A misaligned load or store (the core does not split them) is
done byte by byte by the trap handler and reported on the TTY; anything
else stops the program with its cause and address.

## Simulation

The stimulus ([`sim/keen4.v`](sim/keen4.v), linked as
`sim/rtl_sim/src/keen4.v`) echoes the TTY, saves panel frames and drives a
gamepad: during the first demo Start (the main menu), A (New Game), A
(normal): the world map loads, then Keen walks right; it passes after 60
frames on the world map with no fault. The card model
(`sim/rtl_sim/src/sd_card_img.v`, 4096 blocks) reads the image
`sim/gen_sdimage.py` makes from the game data (a 2 MB MBR + FAT12 card with
the card layout, by macOS `hdiutil`; the songs left out):

```bash
sim/gen_sdimage.py sim data/*.CK4          # sim/sd_image.bin, sim/sd_image.mem
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NOTRACE" ./run keen4 -nodump
```

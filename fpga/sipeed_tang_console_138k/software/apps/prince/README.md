# prince — Prince of Persia on the Tang Console 138K

Jordan Mechner's *Prince of Persia* (DOS, 1990) through SDLPoP, Dávid
Nagy's C reconstruction of the DOS game, in the version picopop made for
small microcontrollers, ported to the aRVern SoC of the Tang Console 138K:
the picture on Belenos (320 x 200, 256 colours, scaled to the panel or the
HDMI screen), USB gamepads and keyboards, the digitised sounds and the
AdLib music on the board's sound output, the game's resources read from
the microSD card. It is started from the boot menu
([`../boot_menu/README.md`](../boot_menu/README.md)).

## Licence

- `sdlpop/`: SDLPoP as picopop's fork has it (commit `33cb52b`), the files
  the build uses, with the port's changes, recorded in
  [`port/sdlpop.patch`](port/sdlpop.patch); `sdlpop/resources.h` is the
  port's. SDLPoP is GPL-3.0 ([`COPYING`](COPYING)).
- `picosdl/`: picopop's subset of SDL2 (commit `49248e7`), BSD-2-Clause
  ([`picosdl/LICENSE`](picosdl/LICENSE)), with the port's changes in
  [`port/picosdl.patch`](port/picosdl.patch).
- `tools/datfile.py` is picopop's DAT reader (`tools/assets/datfile.py`,
  commit `06b976f`), unchanged; `tools/make_pak.py` takes its palette table
  from picopop; `tools/render_music.py` ports SDLPoP's MIDI player. picopop
  is GPL-3.0.
- The program built here is GPL-3.0. The port's own files (`arv/`,
  `tools/fetch_data.py`, `sim/`) are BSD-3-Clause, which GPL-3.0 accepts.
  The rest of this repository is not affected.
- FatFs: the shared copy in [`../../lib/fatfs`](../../lib/fatfs).
- The game's resources are the DOS game's and are **not** in this
  repository: `tools/fetch_data.py` downloads them (next section).

## Game data

```bash
make data                    # tools/fetch_data.py -> data/
```

downloads SDLPoP's repository at a fixed commit, checks its `data/` folder
(the DOS game's resources, as DAT files or as folders of PNG and BIN files)
against the SHA-256 of its file list, then makes the files the port reads
(Python 3 with Pillow):

| File | Made by | What |
|---|---|---|
| `*.PAK` | `tools/make_pak.py` | one pack per DAT file: its images at 4 bits per pixel with their palette row, its other resources as they are (690 KB) |
| `MUSnn.ADP` | `tools/render_music.py` | the 22 MIDI tunes played through an OPL3 model at 22.05 kHz, IMA ADPCM (4 MB) |
| `TITLE.PNG` | `tools/fetch_data.py` | the title screen, the boot menu's preview |

The tunes take about five minutes of processor time, spread over the cores
(a minute or two on most computers): the sequencer is SDLPoP's (its timing, its
voice allocation and the frequencies and volumes it gives the chip), the
chip a two-operator OPL3 at its own rate (49716 Hz) resampled to 22.05 kHz,
in pure Python, the tunes in parallel.

The program changes to its folder on the card, `/APPS/PRINCE`, which holds
the `*.PAK` and `MUS*.ADP` files.

## Build and install

```bash
make                         # -> prince.elf (hardware), prince_sim.elf (simulation)
../boot_menu/tools/sd_install.py prince.elf /Volumes/<CARD> --name PRINCE \
    --title "Prince of Persia" --desc "Jordan Mechner's 1989 classic - SDLPoP on the Tang" \
    --image data/TITLE.PNG --add data/KID.PAK --add data/MUS54.ADP ...
```

(every `data/*.PAK` and `data/MUS*.ADP` with `--add`). The top `Makefile`
builds both images in `arv/` (`obj/`, `obj_sim/`); the simulation build
(`-DSIMULATION`) runs the game clock 500 times faster and reports its frames
and levels on the TTY. Target: the RV32E core of the Tang (`rv32emc` with
Zba, Zbb, Zbs, Zbc, Zcb; newlib-nano), 160 KB of program memory (the image
uses about 116 KB), 128 KB of SRAM, the Belenos VRAM. The build uses
link-time optimisation and `-msave-restore`.

## Files

| Path | What |
|---|---|
| `sdlpop/` | the game, with the port's changes (`ARV_TANG`) |
| `picosdl/` | the SDL2 subset: surfaces, blitters, the palette, events, the audio mixer |
| `arv/` | the Tang build: `Makefile`, start-up and trap handler (`startup.S`, `trap.c`), the board (`board.c`: Belenos, HDMI hot plug, USB, the timer, the card), picosdl's back-end (`psdl_tang.c`), the packs (`pak.c`), the music stream (`music.c`), the sound output (`audio.c`), the C library parts (`stdio_tang.c`, `libc_tang.c`) |
| `port/` | `sdlpop.patch`, `picosdl.patch`, the changes to the two |
| `tools/` | `fetch_data.py`, `make_pak.py`, `datfile.py`, `render_music.py` |
| `sim/` | the Tang simulation: the stimulus and the card image generator |

## How it works

**Memory.** SDLPoP loads every sprite set of a level as 8-bit images, more
than the Tang's 128 KB of SRAM; here the resources stay on the card and in
the VRAM until they are drawn:

| Where | What |
|---|---|
| program memory | the code and the game's constant tables |
| SRAM | the game's state, the open packs' tables, picosdl's surface headers and event queue, 8 KB of stack |
| VRAM 0 – 64 KB | the displayed frame, 320 x 200 at 8 bpp |
| VRAM 64 – 128 KB | the frame the game draws in |
| VRAM 128 – 144 KB | the digitised sound that plays |
| VRAM 144 – 152 KB | the music ring |
| VRAM 152 – 164 KB | the font's glyphs |
| VRAM 164 – 172 KB | picosdl's surface arena (the peels) |
| VRAM 172 – 272 KB | the image cache |

**Video** (`psdl_tang.c`). picopop keeps one frame, as the Pico's panel
shows only what is sent to it; Belenos shows its frame continuously, so the
game draws in a second one and each present (the whole frame, or the
rectangle the game names) is a GPU copy to the displayed frame, at the end
of a refresh unless the scan is past the image or less than a third into
it. The game waits for the copy before it draws again (picosdl's
`PSDL_PresentSync`).

**Drawing** (`psdl_tang.c`, picosdl's `psdl_blit.c`). The GPU draws every
blit and fill whose pixels are all in the VRAM, which is nearly all of
them: the 4-bit images by colour expansion (value v drawn as the image's
palette row + v: keyed, opaque, mirrored by the destination walk, one
colour by raster op 15, XOR by op 5 on the opaque expansion), the 8-bit
surfaces (the frame, the peels in picosdl's arena) by copies, the fills by
fills. It runs behind the CPU; the CPU waits for it before it touches VRAM
pixels itself (picosdl's CPU loops, `SDL_LockSurface`) and before the image
cache is emptied. A test build draws every GPU operation with the CPU loops
too, into a copy of the destination (the displayed frame serves as the
scratch, so its pictures are not the game's), and reports on the TTY every
50 presents the operations checked and those that differ ("prince: gpu
check ..."): set `FLAVOR="-DSIMULATION -DPSDL_GPU_CHECK"` for the
simulation image in the top `Makefile`, `rm -rf arv/obj_sim` (the objects
do not follow a change of defines), `make`, run the simulation, then
restore the line and remove `arv/obj_sim` again.

**Resources** (`pak.c`, `resources.h`). Opening a DAT file opens its pack
and reads its table. A sprite set becomes 4-bit images (picosdl's
`PSDL_SURF_NIBBLE`) whose pixels are read from the card into the VRAM cache
the first time a blit needs them (`PSDL_SURF_LAZY`); the blitter adds the
image's palette row as it draws. When the cache is full it is emptied whole
and fills again with what is drawn next. Any other resource (levels,
palettes, sounds) is read from the card when the game asks for it.

**Sound** (`audio.c`, `music.c`). picosdl's mixer runs in the I2S `TX_LOW`
interrupt at 22.04 kHz (each sample sent twice, 44.08 kHz): the digitised
sound the game plays, read from the card into the VRAM, plus the tune
streamed from its `MUSnn.ADP` file through an 8 KB ring that the main loop
keeps filled.

**Input** (`psdl_tang.c`). The gamepads and keyboards of both USB-A ports,
as the PC keyboard: the d-pad is the arrows (run, jump, crouch; Up also
climbs), A Shift (grab, pick up, strike), B Up (jump), Y Down (crouch), X
Space (time left), Start Enter, Select Esc (pause). A keyboard's keys are
the PC's.

## Simulation

`sim/prince.v` is the stimulus of the Tang's board-level simulation: the
card model holds `sim/sd_image.bin`, made by

```bash
python3 sim/gen_sdimage.py sim data/*.PAK data/MUS54.ADP
```

(a 2 MB FAT12 image: the packs and the intro tune). A gamepad presses Start
until level 1 starts, then holds Right; the run passes when level 1 has
drawn 100 frames with no trap.

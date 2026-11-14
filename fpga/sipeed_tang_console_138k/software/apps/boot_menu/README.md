# boot_menu — Tang Console 138K program menu

Lists the programs on the microSD card, lets you pick one and starts it. Built
into the bitstream (and programmed into the flash), it is what the board shows
at power-up; the mode button brings it back at any time. The cross-toolchain
and ISA are read from the RTL-generated `march_config.sh`, so the build always
matches the core you synthesized.

## Using it

| Control | Action |
|---|---|
| gamepad up / down, keyboard arrows | move the selection |
| gamepad A or Start, keyboard Enter or Space | start the program, open a folder of programs, or go back out of one (`<< Back`) |
| gamepad B, keyboard Esc | in a folder of programs: back to the main list |
| keyboard letter or digit | the next program whose title starts with it |
| gamepad Y, keyboard F2 | music on / off (the sound effects stay) |
| gamepad X, keyboard F3 | the sound output: HDMI → speaker → Pmod I2S2 (no HDMI in the panel build) |
| gamepad L / R, keyboard `-` / `=` (or keypad `-` / `+`) | volume down / up, 0 to 8 (6 by default) |
| gamepad Select, keyboard F1 | the system information page (Select, B, A, F1, Esc, Enter or a `KEY[1]` press closes it) |
| any key or button, `KEY[1]` | skip the boot animation |
| `KEY[1]` press | next program (wraps around) |
| `KEY[1]` held 0.6 s | start the program (open the folder, go back) |
| `KEY[0]` | restart the running program |
| mode button (SW2) | reload the FPGA from the flash: back to the menu |

The help line at the bottom shows the controls of what is plugged in: a
gamepad's, a keyboard's (both: in turn every four seconds), or `KEY[1]`'s
when there is neither; it changes as a device comes or goes.

The mode button is the FPGA's `RECONFIG_N`: it brings the menu back only when
the menu's bitstream is the one in the flash (`2_program_flash.sh`); after
`1_program_fpga.sh` it reloads whatever the flash holds. On the HDMI build,
holding `KEY[1]` for a second also restarts the HDMI link; the link
recovers on its own when the monitor comes back (see
[`../../lib/demo_kit/README.md`](../../lib/demo_kit/README.md)).

## The card

One folder per program under `/APPS`, 8.3 upper-case names:

```
/APPS/WOLF3D/APP.BIN     the program
/APPS/WOLF3D/APP.TXT     optional: a title line, a description line
/APPS/WOLF3D/APP.IMG     optional: the preview shown beside the list
/APPS/WOLF3D/APP.CRC     optional: the CRC-32 of APP.BIN, checked while loading
/APPS/WOLF3D/...         the program's own files
```

A folder without an `APP.BIN` that holds program folders is a *folder of
programs* (one level, directly under `/APPS`), for instance the demos:

```
/APPS/DEMOS/APP.TXT          optional: its title and description in the list
/APPS/DEMOS/APP.IMG          optional: its preview
/APPS/DEMOS/LOGO/APP.BIN     a program in it, with its own files as above
```

The list shows the programs first, then the folders of programs (`>>` in the
size column); opening one lists its programs under a `<< Back` entry.

`APP.BIN` is the program memory image of the app's hardware ELF from
`0x20000000`, what the J-Link would load, up to 159.75 KB. Any other folder
without an `APP.BIN` is not listed. `APP.CRC` holds its CRC-32 (that of zlib, as in
`zip` or `crc32`), eight hex digits; the loader computes the image's CRC
while reading it and, on a mismatch, does not start it: the screen says so,
the LEDs show `0x26`, and the mode button brings the menu back (it is
overwritten by then). Without an `APP.CRC` (or with one that is not eight
hex digits) nothing is checked. [`tools/sd_install.py`](tools/sd_install.py)
writes these files from an ELF onto the mounted card:

```bash
CARD=/Volumes/OLIV_ARVERN
tools/sd_install.py ../wolf4SDL/wolf4SDL.elf $CARD --name WOLF3D --title "Wolfenstein 3D" \
                    --desc "id Software, 1992"
tools/sd_install.py ../demo_arvern_logo/demo_arvern_logo.elf $CARD --name LOGO --title "aRVern logo demo" \
                    --image logo.png --in DEMOS
```

`--in FOLDER` installs into a folder of programs (`/APPS/DEMOS/LOGO`),
giving the folder an `APP.TXT` with its name as the title if it has none.
`--image PICTURE` converts a picture (PNG, JPEG, ...; it needs Pillow) into
the program's preview, `APP.IMG`: fitted into 128 x 96, reduced to 112
colours. `--add FILE` (repeatable) copies other files the app reads into its
folder.
Use the hardware ELF (`<app>.elf`), not the simulation one (`<app>_sim.elf`).
The menu reads the folders in name order and shows up to 32 programs.

The menu writes `/APPS/LAST.TXT` when it starts a program (only when its
content changes): the program's folder on the first line (`WOLF3D`, or
`DEMOS/LOGO` in a folder of programs, which then opens at power-up), then `music=1`
(or `0`), `volume=6` (0 to 8) and one sound output per display build,
`output_hdmi=0` and `output_panel=2` (0 HDMI, 1 speaker, 2 Pmod I2S2): a card
moved between the HDMI and the panel bitstreams keeps each build's choice. At
power-up the program named there is selected and the music, the volume and
this build's sound output are set from it (the other build's line is kept as
it is); without its line the output is the build's default, HDMI in the HDMI
build and the Pmod I2S2 in the panel build. The title band shows the output
and the volume (`HDMI 6`, `Spkr 6`, `Pmod 6`, the digit a `-` while the
music is off). The programs started from the menu play on the output it
selected (board I/O `AUDIO_OUT`, kept through the launch). A card that refuses the
write (read-only, full) is left as it is.

`APP.IMG` is a 12-byte header, `AIMG`, the width and the height (16 bits
each, little-endian, at most 128 x 96), the number of colours N (one byte, 1
to 112) and three zero bytes; then N colours of three bytes (red, green,
blue); then width x height bytes, row by row from the top, each a colour
number below N. The menu shows it centred in the panel right of the list
once the selection has stayed on the program for a quarter of a second, in
LUT entries 128-239; without one (or with an invalid one), and for a
folder or the way back, the panel shows soft coloured blobs (three sine
waves summed, in a ramp of one of eight hues), the hue and the pattern taken
from the folder's name: each entry keeps its own.

## The card over the J-Link

While the menu runs, a host reads and writes the card's files through a
J-Link, the core never halted (the board keeps the card; nothing is
swapped): [`tools/sdlink.py`](tools/sdlink.py) fills a mailbox in the SRAM
([`sdlink.h`](sdlink.h), at `0x8001FE00`, with a 64 KB buffer below it, both
in the loader's staging area) and the menu ([`sdlink.c`](sdlink.c)) does the
file operations with its FatFs. While a session lasts the menu shows a
"J-Link: SD card" panel with the file and the amount written, and ignores
its inputs; at its end (or 10 s without a command) it reads the card again.

```bash
tools/sdlink.py ls /APPS
tools/sdlink.py get /APPS/LAST.TXT
tools/sdlink.py put wolf4SDL.bin /APPS/WOLF3D/APP.BIN
tools/sdlink.py mkdir /MEDIA/MUSIC
tools/sdlink.py rm /APPS/OLD/APP.BIN
tools/sdlink.py sync ../../../WIP_sdcard_update/APPS /APPS     # what differs (size, CRC-32)
```

A file is written under a temporary name in its folder, checked against its
CRC-32 computed by the menu, then renamed, so an interrupted copy never
leaves a half-written file under its name. The script drives J-Link
Commander (`JLinkExe`, through a pseudo-terminal) and needs nothing else;
it writes at about 74 KB/s with a 4 MHz JTAG clock (`--speed`).

Without a card (or without a FAT file system on it) the menu says so and
tries again every 2 s. While the list is shown it reads the card's first
sector every 2 s: a card taken out loses its power and answers again only
after a new initialisation, so the read fails and the menu goes back to
mounting; the card put back, or another card, is then read again. The card
detect pin is not used: it does not follow the card on this board.

## How it works

- The menu draws in an 8 bpp frame of the Belenos VRAM (480 x 272, scaled
  on the HDMI monitor), with an 8 x 16 font
  ([`font_mono.h`](../../lib/demo_kit/font_mono.h)). The Belenos GPU draws
  the rectangles (fills; the gradient background one per band of rows of
  the same colour), the text and the 2-bit wordmark and subtitle (colour
  expansions whose source, in the program memory, is pushed behind the
  command), the same pixels as the CPU's; the CPU draws the logos, the
  preview pictures and the stars of the drift, after waiting for the GPU.
  The menu reads the card through FatFs
  ([`../../lib/fatfs`](../../lib/fatfs)). The title band shows the small
  aRVern logo and the wordmark of the logo demo
  ([`logo_data.h`](logo_data.h), taken from
  [`../demo_arvern_logo/logo_data.h`](../demo_arvern_logo/logo_data.h) by
  `make logo`, [`tools/logo_subset.py`](tools/logo_subset.py), with the large
  logo and the subtitle of the boot animation), in LUT entries 64-115 after
  the menu's own colours.
- **Boot animation.** At power-up ([`intro.c`](intro.c), after the first two
  scenes of the logo demo): the logo painted one colour layer at a time over
  a gradient, the wordmark typed in, the subtitle, then pulses of light
  running along the circuit traces while they power up and the project's
  address (github.com/Arvern-Silicon) fades in, and a fade to black into the
  menu; about six seconds, with the boot chime. Any key, button or `KEY[1]`
  skips it (the press does nothing else; the chime fades out). On HDMI it
  starts after two seconds of a black screen and a silent stream: a monitor
  plays the sound a few seconds after it shows the picture.
- **Stars and glass.** The menu sits on glass panels (a dark fill, a thin
  edge, a lighter top line): the list (eight programs a page) and the
  preview beside it, the description and the help line, each as wide as its
  text; the messages, the information page and the loading screen take
  their own panels. Behind them a field of 480 stars drifts towards the
  viewer over a dark blue gradient ([`gfx.c`](gfx.c)): points in a box
  projected once a frame, near ones drawn 2 x 2 and brighter, each frame
  erasing what the previous one drew (the gradient depends on the row
  only, so any rectangle or text can be drawn over it and restored). Each
  panel's area, and the title band's, is registered, and no star is drawn
  or erased inside one. Starting a program is a jump to hyperspace: for
  0.7 s the stars speed up into streaks around the loading panel. The
  selection bar glows. The palette is composed once a frame (the animated
  entries, then the fade) into the LUT bank the refresh is not showing,
  without waiting: Belenos' `FRAME_CNT` tells when a frame has passed.
  The menu fades in from black at power-up (half a second) and fades out
  with the confirm sound when a program starts, all but the title being
  loaded and its progress bar, which the loader then fills. The simulation
  build draws the panels but no stars.
- **Sound.** The boot animation plays a chime of about seven seconds, an
  original in E: over a sub, a detuned low pad glides up an octave while a
  band of noise sweeps open, glass tones climb into the reverb, an Eadd9
  chord blooms as the traces power up, sparkles follow and the reverb rings
  out (eleven sine voices, some with an octave or a twelfth added, with
  attack and decay; a state-variable filter on the noise; a reverb of four
  damped combs and two all-passes). It plays at the default volume: the
  saved settings are read from the card after the animation. A short
  chiptune, which waits for the chime to ring out, then loops while the menu is shown; moving the
  selection blips, starting a program plays a rising arpeggio, an error
  (a program refused, the card gone) a low two-tone. The synthesizer
  ([`synth.c`](synth.c)) has four voices, integer only: a 25 % pulse lead, a
  triangle bass, a noise drum and a square for the effects, with
  exponential envelopes updated every 64 samples. The I2S master streams it
  at 44.08 kHz (MCLK / LRCK ratio 384, each rendered 22.04 kHz sample sent
  twice, MCLK on for the Pmod's D/A) to the selected sound output: HDMI
  audio, the onboard speaker or a Pmod I2S2 on PMOD1. The I2S `TX_LOW` interrupt (PLIC source 17, FIFO at or below 8 of
  its 16 words) renders the free words, so the sound goes on through card
  reads and frame waits, at about 7 % of the CPU (some 60 instructions a
  sample).
- **USB ports.** Two icons in the title band, left of the volume, show what
  the USB-A ports hold (left J15, right J16), from the port's state and the
  HID decoder's device kind: a keyboard, a mouse or a gamepad (light grey),
  a device being enumerated (a grey socket), a device the port does not
  support or failed to enumerate, full-speed devices included (an orange
  cross), nothing (a dim socket). Plugging a device in plays a rising
  chime, pulling it out a falling one, an unsupported device the error tone.
- **System information.** Select (F1) replaces the list with a page of ten
  lines: the core (`marchid`, the release from `mimpid`, the `misa` letters,
  the privilege modes) and, on aRVern, its build from `marv_cfg` (`0xFFF`:
  compressed and bit-manipulation levels, multiplier and divider, PMP and
  HPM counts, `Zicntr`, debug and its triggers, custom CSRs); the clocks;
  the memories; the display; the menu's build date and the size of its
  loader block; the card's volume (size, FAT type, cluster size); and what
  the USB ports hold, with the device's vendor and product IDs.
- **Program memory.** A program replaces the menu in the program memory, the
  only memory the core executes from. The menu keeps the code that loads in
  a *loader block* at the top of the program memory (16 KB reserved, about
  14.2 KB used, from `0x20023F00`), just below the launch routine
  ([`stub.S`](stub.S), the last 256 bytes, which no `APP.BIN` reaches): the
  loader ([`loader.c`](loader.c)), FatFs, the SD-card layer, the SPI driver
  and the C and compiler support libraries. Everything else (startup,
  interface, sound, graphics, USB; about 46 KB) sits from `0x20000000` up
  and is dead once the load starts.
- **Loading.** The menu checks the program's size, writes `LAST.TXT`, opens
  `APP.BIN` and reads `APP.CRC`, jumps to hyperspace, lets the confirm sound
  and the fade play out (at most 0.3 s), erases the stars, stops the sound, waits for the GPU and turns the
  interrupts off; then the loader reads the image in three parts at sector
  boundaries: below the loader block straight into place, over it into an
  SRAM staging area after the menu's data and stack (14.5 KB used of about
  100 KB), above it straight into place again, filling the progress bar and
  computing the CRC on the way. The launch
  routine copies the staged part over the loader block and jumps to
  `0x20000000`, where the program starts as after a reset. The menu's own
  size does not limit the programs: only the loader block is staged. A read
  error during the load cannot return to the menu (it is overwritten): the
  LEDs show `0x24`, the TTY says so, and the mode button reloads the menu.
  [`tools/check_loader.py`](tools/check_loader.py) runs after every link and
  fails the build if anything in the loader block refers to code or data
  below it (the relocations stay in the ELF, `-Wl,-q`; the loaded image is
  unchanged).
- The SoC is not reset in between: a program starts with the peripherals as
  the menu left them and sets up the ones it uses: the display on, the
  card's file system closed, the I2S master stopped with empty FIFOs, its
  interrupt enables and threshold at their reset values (its clock and
  format settings stay), the sound output as selected (board I/O
  `AUDIO_OUT`), PLIC source 17
  disabled at priority 0, `mie` clear, `mstatus.MIE` clear and `mtvec` at
  its value from reset.

The status block `menu` (a symbol in the ELF, magic `BMNU`) holds the state,
the last error, the selection, the size of the image being loaded, its
staged bytes and its CRC-32 (computed and expected) for a debugger; `audio`
(magic `AUDO`) the stream's state, samples written, underruns and
interrupts. The LED register shows `0x80` |
the selection, `0x40` while loading, `0x20` | an error code (1 no card, 2 no
`/APPS`, 3 no program, 4 read error, 5 program too large, 6 CRC mismatch).

## Build

```bash
make                 # -> boot_menu.elf (+ .ihex, .lst, .size), boot_menu_sim.*, sim/sd_image.*
make clean
```

`TC_OPT` and `INST_MODE` work as in the other apps (see
[`../demo_leds/README.md`](../demo_leds/README.md)).

## Load & run

```bash
make run              # = run-jtag: JTAG/FT232H  (DTM_TYPE=0, the default)
../../../synthesis/gowin/0_create_bitstream.sh boot_menu hdmi    # or lcd
../../../synthesis/gowin/2_program_flash.sh boot_menu_hdmi       # the menu at every power-up
```

The other transports and the debug targets are those of
[`../demo_leds/README.md`](../demo_leds/README.md).

## Simulation

```bash
cd ../../../sim/rtl_sim/run && SIMULATION_DEFINES="-D VERY_LONG_TIMEOUT" ./run boot_menu -nodump
```

`make` writes the simulated card, [`sim/gen_card.py`](sim/gen_card.py): a
2 MB FAT12 image with two test programs ([`sim/apptest.S`](sim/apptest.S),
each checking the sum of its own image: APPA of a few bytes, APPB of about
159 KB, reaching past the loader block so its load has all three parts;
`make APPB_PAD=98304` keeps it below the block; APPA has an `APP.CRC`,
APPB one with `make APPB_CRC=1`, so the CRC check runs on the load the test
starts), a folder without `APP.BIN` and an `APP.BIN` over the size limit. The stimulus
([`src/boot_menu.v`](../../../sim/rtl_sim/src/boot_menu.v)) drives `KEY[1]`
alone and the card's presence: it checks an empty slot at reset and the
card then inserted, the listing, the selection moves and wrap, the refusal
of the oversized program, the card taken out and put back while the list is
shown, and that the larger program loads and runs with a correct image
(the menu also writes `/APPS/LAST.TXT` before it). In the simulation build
the input is polled at a fixed short interval instead of once a frame, every
phase of the boot animation lasts one frame, the fades are a frame or two
and the tune plays twenty times faster.

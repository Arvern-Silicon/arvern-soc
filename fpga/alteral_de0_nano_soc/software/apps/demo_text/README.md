# demo_text — text on Belenos

Text drawn by the Belenos GPU on the LT24 (320 × 240, 8 bpp). Every character is one
`EXEC_EXPAND`: the CPU pushes its few FIFO words and moves on while the GPU draws it. The
glyphs of the mono and big fonts come straight from ROM, pushed behind their `EXEC_EXPAND`
(the immediate source, `GPU_IMM_EN`); the sans font is read from a copy in the VRAM.

| Scene | LEDs | What happens | Belenos feature |
|---|---|---|---|
| 1 console | 1 | a boot log typed in, a blinking cursor, the screen scrolling up | 1 bpp font (8 × 16), opaque expansion (the cell background is written too), each glyph's 4 words pushed from ROM (immediate source); scrolling is one GPU copy |
| 2 typography | 2 | a chrome headline over a gradient, antialiased paragraphs | 4 bpp font: 15 gradient steps, shimmering by palette rotation (`LUT_ROT`); 2 bpp text mapped onto 3-entry palette ramps |
| 3 rotation | 3 | a marquee running round the screen edge | the same glyphs drawn in four directions by the destination swaps of `EXEC_EXPAND` |
| 4 rotozoom | 4 | the title spins and zooms in, then breathes | the title expanded once into an 8 bpp image, then drawn rotated and scaled: one `EXEC_TEXSPAN` per screen row (free pitch, transparent background) |
| 5 credits | 5 | closing card | — |

**KEY[1]** skips to the next scene; **SW[0]** holds the current one; **SW[2]** shows the
CPU load on the LEDs.

## Fonts

`text_data.h` holds three fonts and the title, rendered from IBM Plex Sans (see
`THIRD_PARTY.md`) by [`tools/text2c.py`](tools/text2c.py) in the layout `EXEC_EXPAND`
reads: fixed cells packed glyph after glyph, row by row, pixels LSB first.

| Font | Depth | Cell | Characters |
|---|---|---|---|
| mono | 1 bpp | 8 × 16 | printable ASCII |
| sans | 2 bpp | 10 × 20 | printable ASCII, antialiased |
| big | 4 bpp | 22 × 32 | capitals, digits, a little punctuation; values 1–15 run down the glyph |

The file is committed; `make text [PREVIEW=dir]` regenerates it (needs Pillow and the
IBM Plex Sans fonts installed). The arrays are word-aligned, and a mono or big cell is a
whole number of 32-bit words (4 and 88): with the immediate source (`caps.gpu_imm`) each of
these glyphs, and the title, goes from ROM straight to the GPU (`blns_gpu_expand_imm`). A
sans cell (400 bits) starts inside a word, so at run time the sans font is copied into the
VRAM area of frame 1, which the text scenes do not show; without the immediate source all
the fonts are.

## Build and run

```
make            # demo_text.elf / .ihex
make run        # load over the JTAG DTM (see the Makefile for UART / I2C)
```

The board-level simulation (`-DSIMULATION`: every scene shortened, then the LEDs show
`0xA5`) saves the panel image after every frame as `frame_log/fb_<n>.png`:

```
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NO_TIMEOUT" ./run demo_text -nodump
```

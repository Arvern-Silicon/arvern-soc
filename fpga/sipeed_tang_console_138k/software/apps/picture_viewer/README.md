# picture_viewer — Tang Console 138K picture viewer

Shows the JPEG, PNG, GIF, BMP and QOI pictures of the microSD card's
`/MEDIA/PICTURES` folder in name order on the board's screen (the panel or
the HDMI monitor), each fitted to 480 x 272 at 16 bits a pixel, centred on
black; an animated GIF plays. The cross-toolchain and ISA are read from the
RTL-generated `march_config.sh`, so the build always matches the core you
synthesized.

## Using it

1. Put pictures in `/MEDIA/PICTURES` on a FAT12/16/32 card, with 8.3 names
   (`PIC0001.JPG`, ...); the name order is the showing order.
2. Start the viewer from the boot menu (or load it, below).

| Gamepad | Keyboard | Board | Action |
|---|---|---|---|
| right | → | `KEY[1]` short press | next picture |
| left | ← | | previous picture |
| A, Start | Space, Enter | `KEY[1]` held 3/4 s | slideshow on / off (a picture every 5 s) |
| B | I | | information bar on / off: the name, size and format, the decoding time, the picture's number |
| | | `KEY[0]` | reset: back to the first picture |

**Pictures.** The decoders ([`jpeg.c`](jpeg.c), [`png.c`](png.c) with
[`inflate.c`](inflate.c), [`gif.c`](gif.c), [`bmp.c`](bmp.c), [`qoi.c`](qoi.c))
take any size up to 32768 x 32768:

| Format | Decoded |
|---|---|
| JPEG | baseline and extended sequential, progressive; grey or YCbCr (RGB by an Adobe marker), any sampling (4:4:4, 4:2:2, 4:2:0, ...), restart markers. Not: arithmetic coding, 12-bit, CMYK, lossless |
| PNG | every colour type and bit depth (16 bits by the upper byte), palette and colour-key transparency, alpha, Adam7 interlacing |
| GIF | 87a and 89a, local palettes, interlacing, transparency, animation (delays, disposal to the background; "to the previous" is kept as drawn) |
| BMP | uncompressed 1, 4, 8, 16, 24 and 32 bits, bit-field masks, bottom-up and top-down (no RLE) |
| QOI | RGB and RGBA |

Transparency blends over the screen's black. A picture the viewer cannot
show (a variant above, a damaged file, too large for the memory) leaves its
name and the reason on the screen; the next one is a key press away.

## How it works

- **The output** ([`scale.c`](scale.c)). A decoder outputs RGB888 rows, top
  to bottom. A picture larger than the screen shrinks to fit, its aspect
  kept: each screen pixel the average of the source area it covers, a
  source pixel on its edge counted by the part inside (a box filter, no
  division in the pixel loops). A smaller one grows by the largest whole
  factor that fits; a JPEG the memory made the decoder reduce below its fit
  grows back to it smoothly instead (bilinear). The RGB565 pixels take a 4 x 4 ordered dither (tables:
  no clamping in the pixel loop). Frames (GIF, interlaced PNG) are drawn
  with the nearest source pixel, their transparent pixels left as they are.
- **JPEG**. Huffman decoding with a 9-bit look-ahead, and a combined table
  that gives a short code's run, value and total length in one lookup; the
  accurate integer IDCT (the LLM factorisation, 13-bit constants), with
  shortcuts for a block holding only its DC term and for rows without AC
  terms; the JFIF colour conversion in libjpeg's fixed point. At full size
  the output is libjpeg's (`djpeg -dct int -nosmooth`) bit for bit, baseline
  and progressive, every sampling, restart markers. A picture larger than
  the screen decodes at 1/2, 1/4 or 1/8 of its size (the largest reduction
  that keeps it at least as large as its fit) by a 4 x 4, 2 x 2 or 1 x 1
  IDCT of the lowest frequencies, averaging the full IDCT over each square;
  a subsampled component at a lower reduction than luma, so that no chroma
  detail is lost. A progressive file keeps the coefficients the reduced IDCT
  needs, and which of the others are non-zero (for the refinement scans),
  for every block until its last scan. When they do not fit in the memory,
  the chroma goes to luma's reduction, then both to a further one; at 1/8
  a block keeps its DC term alone and the AC scans are skipped (about
  2000 x 1100 fits). A larger file is drawn at 1/8 from its first scan
  alone (every component's DC, less its last refinement bits), the later
  scans not read: any size shows, quickly, at the resolution of an 8 x 8
  block.
- **PNG**. A zlib decompressor ([`inflate.c`](inflate.c): stored, fixed and
  dynamic blocks, a 32 KB window, 10-bit Huffman look-ahead) pulled across
  the IDAT chunks, the five row filters, then the pixels to RGB. An
  interlaced file is drawn pass by pass, each pixel as the block the later
  passes refine.
- **GIF**. LZW decoding straight onto the screen, a row at a time; an
  animation's frames follow their delays while the picture is shown.
- **Memory**. The decoders take their working memory from a 96 KB block of
  the SRAM and from the program memory after the program image, up to the
  boot menu's launch routine (about 100 KB). The information bar saves the
  pixels it covers in the VRAM after the frame.
- **The GPU** (Belenos). It clears the screen (a fill), saves and restores
  the pixels under the information bar (copies) and draws the text (colour
  expansions of the 8 x 16 font, the glyphs pushed behind the command from
  the program memory), the same pixels as the CPU would; the decoders'
  output and the bar's darkening are the CPU's, and wait for the GPU first.

Decoding time (instructions on spike, RV32E, at the 1.22 cycles an
instruction measured in simulation for the audio decoders):

| Picture | Time |
|---|---|
| JPEG 480 x 272, 4:2:0 | about 0.6 s |
| JPEG 1920 x 1080 (decoded at 1/4) | 1.6 to 2.4 s |
| JPEG 4096 x 2160, 3.4 MB (decoded at 1/8) | about 13 s |
| progressive JPEG 1920 x 1080 (at 1/8, grown back) | about 1.2 s |
| progressive JPEG 4096 x 2160 (its first scan) | about 1.7 s |
| PNG 517 x 301, RGB | about 2.1 s |
| PNG 800 x 600, RGBA | about 4.6 s |
| GIF 400 x 250 | about 0.6 s |
| BMP 640 x 400, 24 bits | about 1.0 s |
| QOI 480 x 272 | about 0.5 s |

The time of a large JPEG is its Huffman decoding (every coefficient has to
be read, whatever the reduction).

## Build

```
make
```

builds `picture_viewer.elf` (hardware) and `picture_viewer_sim.elf`
(`-DSIMULATION`), and the simulated
card.

## Load & run

From the boot menu: put the app on the card with
[`../boot_menu/tools/sd_install.py`](../boot_menu/tools/sd_install.py)
(`sd_install.py picture_viewer.elf CARD --name VIEWER --title "Picture
viewer"`). Over the J-Link: `make run`.

## Simulation

```
cd ../../../sim/rtl_sim/run
./run picture_viewer -nodump
```

`make` writes the simulated card, [`sim/gen_card.py`](sim/gen_card.py): a
2 MB image (an MBR, one FAT12 partition, pure Python) whose
`/MEDIA/PICTURES` holds pictures of four coloured quadrants: seven small
ones, which grow (a baseline and a progressive JPEG, kept in the script as
Pillow made them; an RGB PNG using every filter, an interlaced palette PNG
with a transparent quadrant, a two-frame animated GIF, a BMP and a QOI
written by the script), a JPEG cut short, and five larger than the screen,
which shrink (a 1024 x 576 JPEG decoded at 1/2, a 640 x 360 progressive
JPEG whose coefficients fit the memory at 1/4, a PNG, a GIF and a bottom-up
BMP). The stimulus
([`src/picture_viewer.v`](../../../sim/rtl_sim/src/picture_viewer.v)) waits
for each picture, checks the middle of each quadrant on the panel model's
image against its colour (for a large picture, two corner pixels too), that
the GIF shows both frames, that the cut JPEG gives an error, and that the
list wraps, pressing `KEY[1]` to move on.

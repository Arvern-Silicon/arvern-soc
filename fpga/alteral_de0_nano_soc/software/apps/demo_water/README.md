# demo_water — ripples around the aRVern logo

Water ripples on the LT24 (320 × 240, 8 bpp): the 2D wave equation on a 160 × 120 height
grid, shaded from its slope, with the full-resolution aRVern logo (200 × 140) standing in
the middle of the pool. The pool's edges and the logo are walls, so the waves bounce off
them and cross each other.

| Scene | LEDs | What happens |
|---|---|---|
| 1 rain | 1 | random drops |
| 2 two sources | 2 | two points above the logo pulse in step: interference fringes between them, echoes off the logo |
| 3 wake | 3 | a source moving round the pool |

The logo's circuit traces cycle through palette rotation (`LUT_ROT`). **KEY[1]** skips to
the next scene; **SW[0]** holds the current one; **SW[2]** shows the CPU load on the LEDs.

## How it is drawn

- **CPU**: one simulation step per frame (two byte buffers; next = half the sum of the four
  neighbours − previous, damped, saturated with Zbb `min`/`max`), then the shaded cells into a
  160 × 120 image, four per word. The logo's cells (from its run tables,
  `../demo_arvern_logo/logo_data.h`, at grid resolution) are held at zero.
- **GPU**, in the gap after each refresh: the water image scaled ×2 into frame 0 by one
  textured span (`EXEC_TEXSPAN`, step ½) per row; the logo itself (transparent copy);
  its shadow, 6 pixels down and right: a transparent copy of the logo with the palette remap
  `DST` (`GPU_REMAP_EN`), so each water pixel under the logo's shape becomes T[D], a darker
  shade of itself (a 256-byte table after the command list: water shade × 9/20, the logo's
  colours unchanged), and the ripples go on, dimmed, under the shadow. Without the remap the
  shadow is the flat colour of raster op 15 (the fill colour wherever the logo is opaque),
  drawn before the logo.
- **Timing**: everything fits one 60 Hz frame (about 87 % of it). The step and the render
  are small row functions written for the 16 registers of RV32E, skip the cells under the
  logo, and the step pushes the GPU's 240 spans a few at a time between its rows, so the CPU
  never waits for room in the GPU's command FIFO.
- **Memory**: the frame 1 VRAM area holds the water image and the logo image, which gets a
  2-pixel bevel at start-up (lit on the top-left edges, shaded on the bottom-right ones). So
  the picture has a single frame buffer, composed between two refreshes.

## Build and run

```
make            # demo_water.elf / .ihex
make run        # load over the JTAG DTM (see the Makefile for UART / I2C)
```

The board-level simulation (`-DSIMULATION`) runs the three scenes shortened, four steps a
frame, then the LEDs show `0xA5`; its refresh gap is much shorter than on the board, so a
frame may show part of the previous composition there.

```
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NO_TIMEOUT" ./run demo_water -nodump
```

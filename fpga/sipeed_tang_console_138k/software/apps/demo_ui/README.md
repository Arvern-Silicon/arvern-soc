# demo_ui — a 16 bpp user interface drawn by the GPU

A small user interface on the RGB panel (480 × 272, RGB565, shown 1:1), every pixel of it drawn by the
Belenos GPU: per-channel gradients, x/y coordinates, the clip window, spans, text and lines.

| Scene | LEDs | What happens |
|---|---|---|
| 1 dashboard | 1 | gradient background and title bar, rounded cards with shadows: a progress bar and its value, a level meter and its value, a clock with a seconds bar; three buttons pressed in turn |
| 2 equalizer | 2 | 24 bars coloured by their height on screen (green, yellow, red) with falling peaks |
| 3 colour | 3 | a red × green square at an animated blue, the blue slider, a cursor with the colour under it and its hex value |
| 4 monitor | 4 | a line chart of two signals (load, temperature) scrolling by a pixel a frame over a dashed grid and a red limit line, and a wireframe gauge of the newest load: a ring, ticks coloured by zone, a segmented value arc and a triangle needle, its reading below |

**KEY[1]** skips to the next scene; **SW[0]** holds the current one; **SW[2]** shows the
CPU load on the LEDs.

## How it is drawn

- **Gradients**: every ramp is one `EXEC_FILL` (or `EXEC_SPAN`) with the per-channel
  gradient at 16 bpp (`SET_GRAD_RGB` steps, `SET_GRAD` with `RGB`): each RGB565 field of the
  fill colour is stepped on its own and saturated, along the rows or along the run.
- **Rounded cards and buttons**: the corner rows are spans of their own colour (the ramp's
  value at that row, computed as the GPU computes it), the middle one gradient fill; the
  shadow is the same shape, dark, 4 pixels down and right.
- **Coloured by height**: a bar of the equalizer, or the level meter, is the whole ramp clipped
  to its extent by the clip window, so its colour depends on the height on screen; the
  background above a bar is the background's own ramp, clipped the same way. The GPU starts a
  trimmed ramp at the right phase.
- **Lines** (scene 4): every stroke is a GPU line in x/y mode (`EXEC_FILL` with `LINE`, 2 FIFO
  words a connected segment), clipped to its panel by the clip window. The grid is dashed by
  the line pattern (4 on, 4 off, its phase reset at each line so the dashes line up); the
  limit line has opaque dashes (the gaps drawn in a dark red: `opaque`, `bg`); the signals,
  the dial's ring and the value arc are polylines (`blns_gpu_polyline_xy`: each joint drawn
  once, the pattern's phase carried round the arc, 3 on, 1 off); the needle is a closed
  triangle, the panels' frames and the hub rectangle outlines. Points are in 1/16 pixel, so
  the chart scrolls by whole pixels between samples 4 pixels apart.
- **Text**: the 1 bpp console font of `../demo_text`, one `EXEC_EXPAND` per character.
- **Frames**: every frame records its drawing into a command list; the GPU waits for the end
  of the frame being sent (`WAIT_REFR_DONE`), runs the list and passes a fence, while the CPU
  records the next frame. The next frame waits for that fence, not for the panel
  (`wait_fence_frame()` in demo_kit), so the CPU never polls the refresh.
- **Memory**: one 480 × 272 frame at 16 bpp takes all the VRAM but 17 KB, which hold the font
  and the command list. The picture has a single frame buffer, drawn just after each refresh.

The demo needs a Belenos build with `BLNS_CFG` [30] (`GPU_CMD2`); on an older bitstream it
shows a message instead.

## Build and run

```
make            # demo_ui.elf / .ihex
make run        # load over the JTAG DTM (see the Makefile for UART / I2C)
```

The board-level simulation (`-DSIMULATION`) runs the four scenes shortened, then the LEDs
show `0xA5`; it saves every panel frame in `frame_log/`.

```
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NO_TIMEOUT -D NOTRACE" ./run demo_ui -nodump
```

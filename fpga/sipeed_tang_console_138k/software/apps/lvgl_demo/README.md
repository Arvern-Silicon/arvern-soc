# lvgl_demo — LVGL 9.6 on the Tang Console

[LVGL](https://lvgl.io) v9.6 (vendored and pruned in [`../../lib/lvgl`](../../lib/lvgl)) running on the
aRVern core, rendering with LVGL's software renderer and the Belenos GPU (an LVGL draw unit), shown by Belenos at 16 bpp (RGB565):
on the 480 × 272 RGB panel 1:1, or over HDMI (720 × 480p) with the same 480 × 272 frame
scaled by the DPI scaler to the monitor's shape. One image runs on both bitstreams.

One screen: a button counting its clicks, a switch (the LED), a checkbox (the arc's
animation), a slider driving a bar, an arc swinging 0–100 %, a pulsing LED, a spinner, the
CPU load of the last 24 s as a line, and the frame rate and CPU load at the top.

**Pointer** — the Belenos hardware cursor (32 × 32 at 2 bpp, an arrow with a dark outline,
its shape in the last 512 bytes of the VRAM) is LVGL's pointer input device: the read callback
moves it with `blns_cursor_move()`, and the refresh lays it over the frame it sends, so moving
it redraws nothing (LVGL's own cursor, an image object, would redraw the areas it leaves and
enters at every move). A note on the left card says so. Until the first gamepad input, an
**attract mode** drives it: it goes to **Click me** and clicks it (the counter goes up), flips
the switch (the LED), takes the slider's knob and drags it towards the far side (back the next time), comes
back to the button and rests, then starts again. **Controls** — a USB gamepad on either
USB-A port then moves the pointer with the d-pad (2 pixels a read, faster while held, up to
10) and presses with A: click, toggle, drag a slider or the arc. `KEY[1]` restarts the HDMI
link (as the other programs).

## Files

| File | |
|---|---|
| `arvern-belenos/sw/lvgl/lv_belenos.c`, `.h` (in the IP) | the LVGL display driver and GPU draw unit for Belenos, shipped with the IP's driver; depends only on LVGL and the Belenos driver (no board header, addresses or sizes: the handle, the frame's place, size and pitch, the draw buffers are parameters) |
| [`board.c`](board.c), [`board.h`](board.h) | the board: Belenos set-up for the panel or HDMI, VRAM layout, time, gamepads, TTY, traps |
| [`main.c`](main.c) | the demo: display set-up, the pointer (the hardware cursor, the attract mode, the gamepad), the screen, the statistics, the main loop |
| [`lv_conf.h`](lv_conf.h) | LVGL configuration (RGB565 only, a small widget set, Montserrat 14, no log or asserts) |
| [`sim/lvgl_demo.v`](sim/lvgl_demo.v) | the board simulation's stimulus |

## How it draws

LVGL renders in PARTIAL mode into two draw buffers in the VRAM after the 480 × 272 frame
(8.25 KB each, through the VRAM window: the 17 KB the frame leaves, less the cursor's
shape), and the GPU flush copies each finished area into the frame with one GPU `COPY`
while LVGL renders the next one into the other buffer. Next to LVGL's software renderer,
the GPU draw unit of `lv_belenos.c` (`gpu_draw`) takes the draw tasks the GPU renders
exactly as the software renderer does: opaque fills (the plain parts of rounded ones too),
opaque borders, and opaque image or layer blits: from the VRAM, or pushed by the CPU
through the GPU's immediate source when small images (at most 1024 pixels, whole rows)
are in the SRAM or ROM. Text, arcs, anti-aliased edges, shadows and translucency stay on
the CPU (Tang builds have no GPU blending; `lv_belenos.h` gives the full list and why).
The tasks the GPU takes are queued behind a fence; LVGL does not let the CPU draw over
them until the fence has passed.

The frame only ever receives finished areas, so the panel never shows a half-drawn
widget: on the board the animated screen runs at about 40 frames per second without
flicker, with the CPU 100 % busy (the arc, the spinner, the LED's shadow and the text are
anti-aliased: CPU work). The 272 KB of VRAM hold one 16 bpp frame and 17 KB, so DIRECT
(`-DLVGL_FLUSH_DIRECT`: LVGL renders straight into the displayed frame, no draw buffer
and no copy) has no second frame to hide its work: the refresh scans areas while they
are redrawn, e.g. after their background is filled and before the widget on them is
drawn again, and on the board the slider, the level bar and the spinner visibly flicker
while the pointer moves.

In the simulation (cycle-accurate at the board's clock; `LVGL_SIM_LOCKSTEP` runs on the
panel board with the hardware cursor, so every build renders the same 47 frames):

| | GPU flush (default) | DIRECT + GPU draw unit | DIRECT, software only | CPU flush |
|---|---|---|---|---|
| whole screen (first frame) | 275 ms (34 areas) | 72.6 ms | 75.3 ms | 153 ms |
| animated frame (the pointer at rest) | 20.9 ms | 20.8 ms | 21.5 ms | 22.0 ms |
| average frame of the run (attract mode, gamepad) | 29.2 ms | 27.2 ms | 28.1 ms | 29.1 ms |

The animated frame is the mean of frames 35–38 (the arc, the LED and the spinner move,
about 2300 pixels in 4 areas); the GPU copies take about 0.1 ms of it. The GPU flush
pays where an area is taller than its buffer (about 8 lines): LVGL renders it in strips,
each drawing every widget under it again, so the whole screen takes 275 ms instead of
73 ms and dragging the slider (about 15 000 pixels a frame) 41 ms instead of 33 ms. The
images are bit for bit the same in all four builds: every image held for two or more
panel frames, in order. The GPU flush adds one more, a frame half copied (a few areas
new, the rest still the previous frame: no partial widget), where a 41 ms frame spans
three panel frames.

Other builds (`DEFS=...`, `make clean` first; or `SIM_FW_DEFINES=...` for the simulation
image): `-DLVGL_FLUSH_DIRECT` (DIRECT above: faster for large areas, but it flickers),
`-DLVGL_FLUSH_CPU` (a 24-line SRAM buffer copied by the CPU; no GPU drawing, the GPU
cannot read the SRAM), `-DLVGL_DRAW_SW` (no GPU draw unit). Simulation only:
`-DLVGL_SIM_LOCKSTEP` (LVGL's clock steps one refresh period per main-loop pass, so frame
n is the same scene in every build; run the bench with `-D LVGL_LOCKSTEP` to save every
panel frame). The demo shows no image: the GPU draw unit's image blits are measured on
the Belenos LVGL benchmark bench.

Time: `board_ms()` counts real milliseconds from the machine timer in both builds (the
drivers' delays run 500 times faster in the simulation build, but LVGL's animations and
the readouts keep the board's time; `LVGL_SIM_LOCKSTEP` replaces it). LVGL refreshes at most every 20 ms
(`LV_DEF_REFR_PERIOD`).

## Build and run

```sh
make            # lvgl_demo.{elf,ihex} for the board, lvgl_demo_sim.* for the simulation
```

Simulation (`fpga/sipeed_tang_console_138k/sim/rtl_sim/run`): `./run lvgl_demo -nodump`, or
`SIMULATION_DEFINES="-D BOARD_HDMI" ./run lvgl_demo -nodump` for the HDMI board. The
simulation build reports every rendered frame on the TTY (render and flush times, pixels,
areas) and LVGL's memory and the stack after 12 frames. Its attract mode makes one pass with
shorter moves (button 1, the switch off, the slider dragged) and rests on the button; the
stimulus then presses A on the gamepad (the pointer clicks the button: button 2) and Down
(the pointer moves), and passes four frames later with each effect seen once and no trap.

# demo_mode7 — rotation and scaling on Belenos

A flight over a textured ground plane and a full-screen rotozoomer, in the spirit of the
SNES "Mode 7", drawn by the Belenos GPU on the LT24 (320 × 240, 8 bpp). Every screen row is
one `EXEC_TEXSPAN`: a run of pixels that walks the texture with its own start point and a
fixed-point step per pixel.

| Scene | LEDs | What happens | How |
|---|---|---|---|
| 1 flight | 1 | the camera flies over fields, roads, a pond and the aRVern logo, turning and rising; logo sprites stand on the ground | row y below the horizon is at distance h · f / (y − horizon): its texels step by h / (y − horizon) along the camera's right vector; the sprites (every tiled copy within 512 texels) are scaled the same way, each one transparent `EXEC_TEXRECT` (an affine blit clipped to the screen); the sky is one gradient fill; angles have 65536 steps per turn and the height is fixed-point, so the camera moves smoothly every frame |
| 2 rotozoom | 2 | the ground texture turns and zooms over the whole screen | every row steps by k · (cos a, sin a); the next row starts k · (−sin a, cos a) further on |

The logo's circuit traces cycle through palette rotation (`LUT_ROT`). **KEY[1]** skips to
the next scene; **SW[0]** holds the current one; **SW[2]** shows the CPU load on the LEDs.

## Texture and memory

The ground is one 256 × 256 texture: a power of two, so the coordinates wrap and the
texture tiles to the horizon for free. It is built at start-up — grass fields, speckles,
roads that join across the tile edges, a pond, and the aRVern logo painted from its run
tables (`../demo_arvern_logo/logo_data.h`) with GPU spans — straight into the VRAM area of
frame 1 (64 KB). The 64 × 64 logo sprite sits after it, then two command lists.

So the picture has a single frame buffer: each frame is drawn in the gap after a refresh
(about 10 ms at 60 frames per second), and the GPU needs 2–5 ms of it. The CPU records each
frame's commands into a command list (written through the VRAM window, never waiting for the
GPU) while the GPU executes the previous one, and starts it with a single `CALL` at the end of
the frame being sent; the two lists are used in turn.

## Build and run

```
make            # demo_mode7.elf / .ihex
make run        # load over the JTAG DTM (see the Makefile for UART / I2C)
```

The board-level simulation (`-DSIMULATION`) runs both scenes shortened, then the LEDs
show `0xA5`; its frames come from a much shorter refresh gap, so the lower part of the
ground may show the previous frame there (not on the board).

```
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NO_TIMEOUT" ./run demo_mode7 -nodump
```

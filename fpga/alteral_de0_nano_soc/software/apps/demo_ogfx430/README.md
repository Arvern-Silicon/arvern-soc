# demo_ogfx430 — openGFX430 graphic demo on Belenos

## Origin

[Belenos](https://github.com/Arvern-Silicon/arvern-belenos) is a branch of the openGFX430
graphic controller, redesigned around a 32-bit datapath and AHB-Lite. openGFX430 came
with a demo program that shows off each of its colour depths and drawing paths (CPU
gates, GPU fills and copies, palettes, frame-pointer scrolling).

This application is that demo, ported to aRVern and Belenos with its structure and
screens kept: one file per colour depth (`demo_<n>bpp.c`) and the same sequence of steps.
`gfx_controller.c` provides the openGFX430-style helpers on top of the Belenos driver
library, so the demo code reads as it did; the hardware access is what changed (see
*Differences from the openGFX430 version* below). The name says where it comes from and
keeps it apart from future Belenos demos.

## What it does

The demo runs forever, cycling through the five colour depths; the LEDs show the current
one (16, 8, 4, 2 or 1).

| Mode | What it shows |
|---|---|
| 16 bpp | Two animated colour-gradient logos, drawn first by the CPU (a software gate-0 walk), then by the GPU (fills and rotated copies), under the four display orientations |
| 8 bpp | The 256 RGB332 colours, then a diagonal scroll of the refresh frame pointer at four speeds |
| 4 bpp | The 16 CGA colours, then random-colour boxes redrawn by the GPU at four speeds |
| 2 bpp | The four colours of each hardware palette, with every background colour |
| 1 bpp | Background / foreground boxes, then a running pattern at four speeds |

Each mode starts with a short title screen (`16bpp`, `8bpp`, …). The frame buffer is
320 × 240 (landscape); Belenos scans it column by column (`DISP_CFG.CL_SWAP`) into the
portrait ILI9341, so the image appears rotated by 90° on the panel.

## Panel

The demo drives the Terasic LT24 module on `GPIO_0` (pin map in
[`arvern_fpga.v`](../../../rtl/verilog/arvern_fpga.v)).

The knobs are at the top of [`gfx_controller.c`](gfx_controller.c): `LT24_CLK_DIV`
(8080 bus timing), `LCD_MADCTL` (orientation and colour order of the panel) and
`MODE_TIME_MS` (duration of each demo step).

## Build and run

```
make            # demo_ogfx430.elf / .ihex
make run        # load over the JTAG DTM (see the Makefile for UART / I2C)
```

or build it into a bitstream: `synthesis/altera/0_create_bitstream.sh demo_ogfx430`.

The board-level simulation builds the firmware with `-DSIMULATION` (500× shorter delays,
continuous refresh), follows the five modes on an LT24 model and saves the panel image
after every frame as `frame_log/fb_<n>.hex`, converted to `frame_log/fb_<n>.png` at the
end of the run. It is
long (about 180 frames); the board simulation uses Verilator by default (a few minutes;
`VERILOG_SIMULATOR=iverilog` selects Icarus).

```
cd ../../../sim/rtl_sim/run
SIMULATION_DEFINES="-D NO_TIMEOUT" ./run demo_ogfx430 -nodump
```

## Differences from the openGFX430 version

- Register access goes through the Belenos map (`gfx_controller.h`); the GPU helpers
  translate the openGFX430 configuration word (swaps, pixel operation) into Belenos
  `SET_*` / `EXEC_*` commands.
- The CPU drawing of the 16 bpp page used gate 0, a hardware window walk. The DE0 build
  has no Belenos gate (`GATE_NR = 0`), so `gfx_controller.c` walks the same window in
  software (`gate0_cfg/width/addr/data/read`: runs, swaps, one pixel per access) and writes
  through the VRAM window.
- Timing uses the ACLINT driver (`arvern-ips/ahb_aclint/sw`, `mtime`) instead of
  Timer A and the watchdog interrupt; the panel is initialised once at boot instead of at every mode change.
- The panel window is set once by software (CASET 0–239, PASET 0–319) and Belenos'
  hardware window is left off, as required with `CL_SWAP`.

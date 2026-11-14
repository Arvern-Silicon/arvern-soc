<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)"
            srcset="https://raw.githubusercontent.com/Arvern-Silicon/arvern/main/doc/img/aRVern_dark_title.png">
    <img src="https://raw.githubusercontent.com/Arvern-Silicon/arvern/main/doc/img/aRVern_light_title.png" alt="aRVern" width="400">
  </picture>
</p>

# aRVern on the Terasic DE0-Nano-SoC

*The aRVern RISC-V core with the Belenos graphics controller on a Terasic LT24 touch
LCD module, 42 GPIO pins on the GPIO 1 and Arduino headers, and the RISC-V debug
connector: the smallest aRVern SoC with a display.*

The board is the Terasic DE0-Nano-SoC (Intel Cyclone V SE 5CSEMA4U23C6). Everything
the SoC needs is inside the FPGA: the program memory, the data SRAM and the video RAM
are block RAMs, and every peripheral is an IP of
[`arvern-ips`](https://github.com/Arvern-Silicon/arvern-ips) or of
[`arvern-belenos`](https://github.com/Arvern-Silicon/arvern-belenos). The board's
HPS (the Arm cores, with their DDR3, Ethernet, USB and microSD) is not used. The top
level is [`rtl/verilog/arvern_fpga.v`](rtl/verilog/arvern_fpga.v); the bus system,
with the address map and the interrupt wiring, is
[`rtl/verilog/ahb_bus_system.v`](rtl/verilog/ahb_bus_system.v).

The design fills the device (80 % of the logic, 260 of the 270 block RAMs), so the
build is also the check that a core or Belenos change still fits a small FPGA.

---

## The core

| Feature | This build |
|---|---|
| Base ISA | RV32E (16 registers) |
| Extensions | M (one-cycle multiplier, 33-cycle divider), Zca + Zcb + Zcmp + Zcmt, Zba + Zbb + Zbs + Zbc |
| Privilege | Machine mode only (no S/U, no PMP) |
| Counters | none (`time` comes from the ACLINT) |
| Debug | RISC-V Debug 1.0, JTAG transport (`DTM_TYPE = 0`) on GPIO 1 pins 31–40, two Sdtrig triggers |
| Branches | one bubble on a taken branch (`SINGLE_CYCLE_BRANCH = 0`) |
| Reset | synchronous (`ASYNC_RST_EN = 0`) |

The parameters are at the top of [`arvern_fpga.v`](rtl/verilog/arvern_fpga.v). The
firmware is built for the same ISA: every app's Makefile reads the `-march` the RTL
generates, so a core rebuilt with other extensions gets matching code.

## Clocks and reset

| Clock | Frequency | Source | Clocks |
|---|---|---|---|
| `clk_50mhz` | 50 MHz | the oscillator on `FPGA_CLK1_50`, no PLL | the whole SoC: core, buses, memories, peripherals, the LT24 bus |
| `clk_lf` | 5 MHz | `clk_50mhz` / 10 | the ACLINT's `mtime` timebase (one tick = 200 ns) |
| `jtag_tck` | up to 25 MHz | the debug adapter | the DTM's JTAG side |

The reset is `KEY0`. The current build meets timing at 50 MHz with 1.35 ns to spare
(Fmax 53.6 MHz, slow 85 °C model).

## Memory and address map

| Base | Size | Region | Notes |
|---|---|---|---|
| `0x0200_0000` | 64 KB | ACLINT | `mtime`, `mtimecmp`, software interrupt |
| `0x0C00_0000` | 4 MB | PLIC | 47 sources, one hart, Machine context |
| `0x1004_0000` | 64 KB | APB4 peripherals | three completers, 4 KB each (below) |
| `0x1005_0000` | 4 KB | Belenos registers | display, GPU, palette |
| `0x2000_0000` | 48 KB | Program memory | the only executable region; the program image |
| `0x8000_0000` | 48 KB | Data SRAM | data, stack, heap; not executable |
| `0x8200_0000` | 160 KB | Belenos VRAM window | two 320 × 240 frames at 8 bpp (one at 16 bpp), plus 10 KB |

The program memory is the fused SRAM controller of
[`ahb_interconnect_fused`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/ahb_interconnect/doc/ahb_interconnect.md):
the core fetches from one port and reads or writes data on the other, so a program can
read its constant tables from it.

| APB base | Completer | Board connection |
|---|---|---|
| `0x1004_0000` | board I/O (`apb_led_key_sw`) | `LED0`–`LED7`, `KEY1`, `SW0`–`SW3` |
| `0x1004_1000` | [`apb_gpio`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/apb_gpio/doc/apb_gpio.md), 26 pins | GPIO 1 header pins 1–28 |
| `0x1004_2000` | [`apb_gpio`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/apb_gpio/doc/apb_gpio.md), 16 pins | Arduino header: D0–D13, SDA, SCL |

[`software/bsp/de0_board.h`](software/bsp/de0_board.h) holds every base address,
size and interrupt number for the firmware.

## Interrupts

| PLIC source | From |
|---|---|
| 1–26 | GPIO 1 header pins 0–25 |
| 27 | `KEY1` edge |
| 28 | switch edge (`SW0`–`SW3`) |
| 32–47 | Arduino header pins 0–15 |

The PLIC drives the core's machine external interrupt. Belenos has its own line: the
core's platform interrupt 16 (`mip[16]`, `mcause` 16). The ACLINT's timer and software
interrupts go to the core directly.

## Graphics and display

Belenos draws into the VRAM with its GPU (blits, fills, affine blits, gradients,
lines, palette remap, mask planes; the `classic` persona, with the hardware cursor) or
the CPU writes the VRAM window directly. Alpha blending, triangles and run-ahead are
off: the device has no room for them, and 16 bpp frames leave no second buffer.

Its display driver is the parallel DBI one (`DIF_TYPE = 2`): it scans a frame out of
the VRAM through a 512-entry palette and sends it over the 16-bit 8080 bus of the
**Terasic LT24** module (2.4-inch 320 × 240, ILI9341 controller), which plugs directly
onto the GPIO 0 header. The LT24's touch controller is not used, and the module has no
tearing-effect line to the FPGA.

## Board connections

| Connector | Pins | Use |
|---|---|---|
| GPIO 0 | all | the LT24 module (the 8080 data bus, `CS_N`, `RD_N`, `WR_N`, `RS`, `RESET_N`, `LCD_ON`; the touch ADC's chip select held high) |
| GPIO 1 | 1–28 | GPIO pins 0–25 (pins 11/12 are +5 V / GND) |
| GPIO 1 | 31–40 | the debug connector (below) |
| Arduino | D0–D13, SDA, SCL | GPIO pins 0–15 of the second GPIO block |
| `KEY0` / `KEY1` | | reset / a key for the firmware |
| `LED0`–`LED7`, `SW0`–`SW3` | | the board I/O completer |

The full pin maps, with the FPGA balls and the header orientation, are in the comments
of [`arvern_fpga.v`](rtl/verilog/arvern_fpga.v). The Terasic manuals, schematics and
the LT24 / ILI9341 documents are in [`doc/Terasic`](doc/Terasic).

## Block-RAM budget

| Memory | M10K blocks |
|---|---:|
| Belenos VRAM, 160 KB | 160 |
| Program memory, 48 KB | 48 |
| Data SRAM, 48 KB | 48 |
| Palette | 2 |
| GPU command FIFO | 1 |
| Display FIFO | 1 |
| **Total** | **260 of 270** |

The whole SoC uses 80 % of the logic (12,710 ALMs) and 8 DSP blocks.

---

## Building

Quartus Prime Lite (with Cyclone V support) runs natively or in Docker:

```bash
cd synthesis/altera
source ./setup_d.sh                          # Docker: sets QUARTUS_PFX (setup_d.fish for fish)
./0_create_bitstream.sh demo_leds            # the SoC with an app as the program image
./1_program_fpga.sh demo_leds                # load into the FPGA (lost at power-off)
./2_generate_flash_file.sh demo_leds         # the .jic / .cdf for the configuration flash
./3_program_flash.sh demo_leds               # write the flash: the app at every power-up
```

`0_create_bitstream.sh` builds the app's firmware first and makes it the program
memory's initial content; the bitstream lands in `synthesis/altera/bitstreams/` and
the reports in `synthesis/altera/WORK/output_files/`. With Quartus on the `PATH`, leave
`QUARTUS_PFX` empty, or set it to Quartus's `bin/` directory. Both programming scripts
use the board's USB-Blaster II: `1_program_fpga.sh` loads the bitstream's `.svf`
with openFPGALoader on the host, while `3_program_flash.sh` runs `quartus_pgm`, which
needs a host whose Quartus can reach the USB port (Docker on macOS cannot). `2_generate_flash_file.sh` writes a `.cdf` beside the
`.jic`, so both can be opened in the standalone Quartus Programmer on another machine
([`DE0-Nano-SoC_howto_sof_to_jic.pdf`](doc/Terasic/DE0_NANO_SOC/DE0-Nano-SoC_howto_sof_to_jic.pdf)).

## Software

| Directory | Content |
|---|---|
| [`software/bsp`](software/bsp) | `de0_board.h` (the map above), `de0_blns.h` (the features of this Belenos instance, for the driver), the LED / key / switch driver |
| [`software/lib`](software/lib) | `demo_kit` (display and input helpers), the SEGGER RTT library |
| [`software/apps`](software/apps) | the demos and tests below |
| [`software/tools`](software/tools) | host scripts for the demos: snapshots, video capture, profiling, performance |

The IP drivers come from `arvern-ips/<ip>/sw` and `arvern-belenos`, compiled into each
app. The `demo_*` apps are for show on the board; the `tst_*` apps check a function and
report pass or fail:

| App | Content |
|---|---|
| `demo_leds` | an interrupt-driven LED demo with SEGGER RTT output |
| `demo_arvern_logo` | the aRVern logo on Belenos, one scene per controller feature |
| `demo_effects` | classic demo effects: starfield, plasma, shaded torus, volcano, sine scroller |
| `demo_raycaster` | a first-person walk through a textured maze with aRVern logos on the walls |
| `demo_text` | text drawn by the GPU: console, antialiased typography, rotated and zoomed text |
| `demo_mode7` | a Mode-7 flight over a textured ground and a full-screen rotozoomer |
| `demo_ui` | a 16 bpp user interface drawn entirely by the GPU (gradients, clipping, text) |
| `demo_water` | water ripples around the aRVern logo (the 2D wave equation at 8 bpp) |
| `demo_ogfx430` | the openGFX430 graphic demo ported to Belenos |
| `tst_plic_gpio` | a PLIC and GPIO interrupt test on the GPIO 1 header |
| `tst_blns` | colour bars on the LT24, and the GPU features checked on the board: lines, the hardware cursor, mask plane and remap, the immediate source and discard, command lists and the FIFO-room calls |
| `tst_sba_busmash` | saturates the data bus, to stress system bus access from the debugger |
| `tst_vram_starve` | measures whether CPU stores to the VRAM window can starve the GPU (a full-frame fill timed alone, against a store loop, and during a display refresh) |

A prebuilt bitstream of every `demo_*` app, with the JTAG debug transport, is attached to
every release on the [releases page](https://github.com/Arvern-Silicon/arvern-soc/releases):
`de0-nano-soc_<demo>_jtag.zip` (`leds`, `arvern_logo`, `effects`, `mode7`, `ogfx430`,
`raycaster`, `text`, `ui`, `water`), each with the `.sof` to load over JTAG and the `.jic` +
`.cdf` for the configuration flash, programmed as in [debug](debug/README.md).

## Simulation and lint

```bash
cd sim/rtl_sim/run
./run demo_leds -nodump            # any app with a stimulus in sim/rtl_sim/src
./run demo_mode7 -screen           # a window showing the LT24 picture
./run_lint                         # Verilator lint of the whole FPGA top
```

The bench ([`bench/verilog/tb_arvern_fpga.v`](bench/verilog/tb_arvern_fpga.v)) models
the board around the FPGA, with an LT24 screen model
([`vscreen.v`](bench/verilog/vscreen.v)); each app's stimulus in `sim/rtl_sim/src`
drives it and checks the result. [`lint/vc_static`](lint/vc_static/README.md) adds a
sign-off structural lint with Synopsys VC Static.

A simulation writes the CPU trace (`asphalt.log`) and the Belenos GPU trace
(`slipstream.log`) on the same cycle basis; Belenos's `slipstream_profile.py` fuses them
with the firmware ELF into a CPU + GPU profile per function, per frame and per source
line. `./run <demo> -frames A:B -collapse` traces only frames A to B of a demo and folds
its busy-wait loops.

## Debug

The debug transport is chosen at build time by `DTM_TYPE` in
[`arvern_fpga.v`](rtl/verilog/arvern_fpga.v):

| `DTM_TYPE` | Transport | Host side |
|---|---|---|
| 0 (the default build) | JTAG | OpenOCD with any adapter it supports, a J-Link, or [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools) (FT232H) |
| 1 | UART | `arvern-tools` |
| 2 | I2C | `arvern-tools` |
| 3 | cJTAG | a J-Link in its cJTAG mode, through SEGGER Ozone or the J-Link GDB Server; OpenOCD over cJTAG is not validated, `arvern-tools` support is planned |

[`debug/README.md`](debug/README.md) covers how to select or rebuild a transport, the
adapter wiring for each, the OpenOCD and GDB set-up, SEGGER RTT and the IDE integration
for the core's debug link on GPIO 1 pins 31–40: JTAG `TDO`, `TDI`, `TCK`, `TMS` on pins 35–38 (cJTAG on
37/38), the UART and I2C transports on pins 31–34, a ground reference on pin 39 and a
3.3 V reference on pin 40 for the adapter's VTref.

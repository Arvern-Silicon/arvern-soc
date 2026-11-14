# demo_leds — Tang Console 138K LED demo firmware

A light show on the two LEDs of the Tang Mega 138K SoM (LED register bits 0 and
1, on the FPGA's DONE and READY pins): no display, no PMOD, nothing else needed.
The cross-toolchain and ISA are read from the RTL-generated `march_config.sh`, so
the build always matches the core you synthesized.

## What the demo does

**Brightness.** The LED register only switches a LED on or off. Each LED gets a
16-bit first-order sigma-delta modulator, run by the main loop on every pass
(several hundred kilohertz on the board): the LED toggles far too fast to
flicker, and its average duty is the brightness asked for. Brightness is
perceptual, 0–255, mapped to the duty *b*·(*b* + 2) (gamma 2, 255 = fully on).

**Animation.** One animation step per millisecond of `mtime`. Eleven shows of
9 s play in a loop; `KEY[1]` skips to the next one. (The mode button between the PMODs,
SW2, is the FPGA's `RECONFIG_N` pin: it reloads the FPGA from flash and cannot be read.)

| # | Show | What you see |
|---|---|---|
| 0 | BREATHE | both LEDs breathe together |
| 1 | HEARTBEAT | lub on one LED, dub on the other, 70 bpm |
| 2 | PING-PONG | a ball of light bouncing between the two, ever faster |
| 3 | CANDLES | two independent flames, with the odd gust of wind |
| 4 | BOUNCE | a dropped ball: its height on one LED, each impact flashes the other |
| 5 | STORM | lightning strikes in the dark, with afterglow |
| 6 | FIREFLIES | slow random glows, each LED on its own |
| 7 | MORSE | "ARVERN" in Morse code; the other LED marks each word |
| 8 | LIGHTHOUSE | a rotating beam sweeping across both LEDs |
| 9 | POLICE | double strobes, left then right |
| 10 | SUPERNOVA | a pulse that speeds up until it burns steady, then fades out |

The show number is also written to LED register bits [7:4], which have no LED:
the board-level testbench follows the shows with it.

The firmware polls; it uses no interrupt. It is written on the ACLINT driver
(`arvern-ips/ahb_aclint/sw`, for `mtime`) and the board support in
[`../../bsp`](../../bsp) (memory map, LED / key peripheral).

## RTT output and commands

The demo prints the name of each show over [SEGGER RTT](../../lib/segger), and
takes a key the other way: `0`–`9` or `a` jumps to that show.

```
=== aRVern two-LED light show ===
KEY[1]: next show, RTT 0-9 / a: select a show
[show] 0 BREATHE
[show] 1 HEARTBEAT
```

Watch it with the **RTT** tab of `arvern-minidebug`, with
`arvern-gdbserver --rtt-port 19021` and `nc localhost 19021`, or with SEGGER's
`JLinkRTTViewer` / Ozone on a J-Link (see [`../../../debug/`](../../../debug/README.md)).
Channel 0 is `NO_BLOCK_SKIP`: with no debugger attached the text is dropped.

## Prerequisites

- A RISC-V bare-metal toolchain — resolved from `march_config.sh` (falls back to
  `riscv64-unknown-elf-*` if that file is absent). `make` only; no CMake/IDE needed.
- For `run` / `debug`: [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools)
  installed — on `PATH`, or in the `.venv` of a sibling checkout
  ([`../../../../../../arvern-tools`](../../../../../../arvern-tools), resolved automatically).

## Build

```bash
make                 # -> demo_leds.elf (+ demo_leds.ihex, demo_leds.lst, demo_leds.size)
make clean
```

Useful knobs (override on the command line):

| Variable | Default | Meaning |
|---|---|---|
| `TC_OPT` | `-O2` | Optimization; use `-Og` (or `-O0`) for the cleanest source-level debug view |
| `INST_MODE` | `COMP_MODE` | `COMP_MODE` builds for the compressed ISA (`MARCH_COMP`), `STD_MODE` without it (`MARCH_STD`) |

```bash
make TC_OPT=-Og                 # rebuild optimized for debugging
make INST_MODE=STD_MODE         # build without the compressed instructions
```

DWARF debug info (`-g`) is always emitted; it lands in non-allocated `.debug_*`
sections, so the loaded firmware is byte-identical — only the host-side `.elf` grows.

## Load & run (arvern-tools, over the DTM)

The `run` target matches the DTM this board is built with. **The default build is
JTAG (`DTM_TYPE=0`)**, so plain `make run` uses JTAG/FT232H; the other targets are for
a board programmed with the UART (`=1`) or I2C (`=2`) bitstream. How to select or
rebuild a transport is in [`../../../debug/`](../../../debug/README.md). A cJTAG
(`=3`) build has no `make` target: `arvern-tools` cJTAG support is planned, so load and
debug it with a J-Link (SEGGER Ozone or the J-Link GDB Server).

```bash
make run              # = run-jtag: JTAG/FT232H  (DTM_TYPE=0, the default)
make run-uart         # UART, auto-detected serial/USB-ISS/FTDI  (DTM_TYPE=1)
make run-uart-usbiss  # UART, forced USB-ISS (only to disambiguate)
make run-i2c          # I2C  (USB-ISS / FT232H)  (DTM_TYPE=2)
```

The FT232H is auto-selected by USB id; raise the TCK clock for faster loads with
`make run JTAG_FREQ=15000000`. For UART/I2C, override the link when needed:
`make run-uart PORT=/dev/cu.usbserial-XXXX BAUD=921600`, or
`make run-i2c I2C_ADDR=0x30 I2C_SPEED=400k`.

## Debug

- **GUI:** `make debug` launches the `arvern-minidebug` GUI with this ELF pre-loaded
  (same `-jtag` / `-uart` / `-i2c` / `-uart-usbiss` flavors as `run`; `debug` = JTAG).
  Rebuild with `make debug TC_OPT=-Og` first for a clean source view.
- **GDB / IDE (JTAG):** for source-level stepping in CLion/VSCode, bring up a GDB
  server on `localhost:3333` via `arvern-gdbserver` or OpenOCD — see
  [`../../../debug/`](../../../debug/README.md). For the IDE side, see
  [`../../../debug/ide-setup.md`](../../../debug/ide-setup.md) and
  [`arvern-tools/doc/tools/gdbserver.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/tools/gdbserver.md).

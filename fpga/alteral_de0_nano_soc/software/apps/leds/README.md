# leds — DE0-Nano-SoC LED demo firmware

A bare-metal example for the aRVern core on this board: a fully interrupt-driven
light show on the 8 board LEDs, with a small `mylib` (`cprintf`, helpers) to show
a realistic multi-file build. The cross-toolchain and ISA are read from the
RTL-generated `march_config.sh`, so the build always matches the core you
synthesized.

## What the demo does

`main()` configures the peripherals and interrupts, enables them, and then does
nothing but `wfi` forever — **every LED update happens in interrupt context**.
That makes it a small but genuine workout for the core's trap path rather than a
delay-loop blinker.

Three interrupt sources drive it:

| Source | Bit | Role |
|---|---|---|
| Machine timer | `MIP[7]` (MTIP) | the animation clock — each tick draws one frame, then re-arms `mtimecmp` |
| SW change | `MIP[17]` (platform IRQ 1) | `SW[0]` = direction, `SW[2:1]` = speed (4 levels) |
| `KEY[1]` press | `MIP[16]` (platform IRQ 0) | cycle display mode / act as the game button |

The two platform sources are **latched** in this core, so their handlers clear
both the peripheral source and the `MIP` bit, source first; the timer's `MTIP` is
level-based and cleared by the `mtimecmp` write. Between them, the demo exercises
`wfi` wake-up, `mtime`/`mtimecmp`, vectored trap entry, and both latched and
level-sensitive interrupt semantics.

`KEY[1]` cycles six display modes:

| # | Mode | What you see |
|---|---|---|
| 0 | `COUNTER` | binary up/down counter on the LED bar (direction from `SW[0]`) |
| 1 | `SCANNER` | one lit LED bouncing left/right (Larson / "Knight Rider") |
| 2 | `BAR` | a bar growing from one end to full, then collapsing |
| 3 | `SPARKLE` | pseudo-random pattern from an 8-bit maximal-length Galois LFSR |
| 4 | `BREATHE` | software-PWM brightness wave rippling across the bar (gamma-corrected, faster fixed tick) |
| 5 | `GAME` | reaction-time game — see below |

**Game mode.** After a random delay every LED flashes (GO); press `KEY[1]` as
fast as you can. A steady score bar shows your time (more LEDs = faster). A false
start — pressing before GO — or being too slow gives a blinking error. The result
holds for a couple of seconds, then it returns to mode 0.

**Board controls:** `KEY[1]` next mode / react · `SW[0]` direction ·
`SW[2:1]` speed · `KEY[0]` board reset. `SW[3]` is unused.

> Because the demo idles in `wfi` and only wakes on a timer tick, it puts almost
> no load on the data bus. That makes it a poor target for testing Debug Module
> System Bus Access against a *running* hart — use
> [`busmash`](../busmash/README.md) for that.

## RTT output

The demo prints over [SEGGER RTT](../../lib/segger) — startup banner, switch
state, every mode change, and the reaction-game result:

```
=== aRVern leds demo ===
RTT up: 512 bytes, down: 32 bytes
switches 0x0 -> dir up, interval 800
running: KEY[1] cycles mode, SW[0] dir, SW[2:1] speed
[key ] mode -> 1 (SCANNER)
[game] reaction 41234 ticks -> score bar 0x3f
```

Watch it either way:

- **`arvern-minidebug`** -> the **RTT** tab: press *Attach* once the firmware is
  running. The control-block address comes from the loaded ELF's `_SEGGER_RTT`
  symbol, so nothing needs configuring.
- **`arvern-gdbserver --rtt-port 19021`**, then `nc localhost 19021` -- a
  terminal instead of a GUI, and it keeps working while GDB holds the adapter.
- SEGGER's `JLinkRTTViewer` / Ozone, if a J-Link is wired to the debug pins of the
  DTM the bitstream carries (see [`../../../debug/`](../../../debug/README.md)).

Not via OpenOCD: it registers the `rtt setup`/`start` commands only for certain
target types, and `riscv` is not one of them, so `debug/arvern-rtt.cfg` cannot
run as of OpenOCD 0.12. That file documents the reasoning.

Channel 0 is configured `NO_BLOCK_SKIP`: with no debugger attached the text is
simply dropped. It must never block — this is an interrupt-driven light show, and
blocking on a buffer nobody is draining would freeze it.

### Commands (host → target)

The same channel carries input the other way. Send a **digit** to jump straight
to a mode, as an alternative to cycling with `KEY[1]`:

| Send | Mode | | Send | Mode |
|---|---|---|---|---|
| `0` | COUNTER | | `3` | SPARKLE |
| `1` | SCANNER | | `4` | BREATHE |
| `2` | BAR | | `5` | GAME |

```
[rtt ] mode -> 4 (BREATHE)
[rtt ] 'x' ignored -- send 0..5
```

Type it in the minidebug RTT tab's send box and press Enter. The numbering is
the same one the log prints, so `mode -> 4` and sending `4` agree.

Commands are polled from the **main loop**, right after `wfi` returns — never
from an interrupt, because handling one calls `SEGGER_RTT_printf`, which takes
the RTT lock. That means a command is serviced on the next timer tick, so
response time is one animation frame: instant in BREATHE, up to the current
speed setting elsewhere. The actual mode switch runs with interrupts masked, so
the timer ISR can never render a frame against a half-switched mode.

Note this is *not* the `cprintf`/`tty_putc` path in `mylib/`, which targets the
RTL testbench's TTY register and is left untouched.

## Prerequisites

- A RISC-V bare-metal toolchain — resolved from `march_config.sh` (falls back to
  `riscv64-unknown-elf-*` if that file is absent). `make` only; no CMake/IDE needed.
- For `run` / `debug`: [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools)
  installed — on `PATH`, or in the `.venv` of a sibling checkout
  ([`../../../../../../arvern-tools`](../../../../../../arvern-tools), resolved automatically).

## Build

```bash
make                 # -> leds.elf (+ leds.ihex, leds.lst, leds.size)
make clean
```

Useful knobs (override on the command line):

| Variable | Default | Meaning |
|---|---|---|
| `TC_OPT` | `-O2` | Optimization; use `-Og` (or `-O0`) for the cleanest source-level debug view |
| `INST_MODE` | *(std)* | Set `INST_MODE=COMP_MODE` to build for the compressed ISA (`MARCH_COMP`) |

```bash
make TC_OPT=-Og                 # rebuild optimized for debugging
make INST_MODE=COMP_MODE        # build for the compressed ISA
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

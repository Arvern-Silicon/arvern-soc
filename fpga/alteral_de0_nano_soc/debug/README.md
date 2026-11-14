# Host-side debug

Debug-adapter configs, scripts and IDE notes for the aRVern core on this DE0-Nano-SoC
build. These are host-side tooling — nothing here feeds the Quartus flow (that lives
under `../synthesis/altera`).

## Which transport the board carries

The debug transport is picked at build time by `DTM_TYPE` in
[`../rtl/verilog/arvern_fpga.v`](../rtl/verilog/arvern_fpga.v). **The default build is
JTAG (`DTM_TYPE = 0`)**, and most of this page (FT232H wiring, OpenOCD configs,
`run-gdbserver.sh`, `ide-setup.md`) targets that build.

| `DTM_TYPE` | Transport | Host side | Prebuilt release asset |
|---|---|---|---|
| 0 *(default)* | JTAG | OpenOCD with any adapter it supports, a J-Link, or [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools) (FT232H) | `de0-nano-soc_leds_jtag.zip` |
| 1 | UART | `arvern-tools` | rebuild |
| 2 | I2C | `arvern-tools` | rebuild |
| 3 | cJTAG | a J-Link in its cJTAG mode: SEGGER Ozone (select cJTAG as the target interface when connecting) or the J-Link GDB Server. OpenOCD over cJTAG is not validated; `arvern-tools` cJTAG support is planned. | rebuild |

A prebuilt `demo_leds` build for the default JTAG transport is attached to each release on
the [arvern-soc releases page](https://github.com/Arvern-Silicon/arvern-soc/releases); the
repository holds no bitstreams, and the other transports are a rebuild away (below).
`de0-nano-soc_leds_jtag.zip` holds three files of the same name: the `.sof`, the bitstream loaded over JTAG (lost at power-off), and the
`.jic` + `.cdf`, the configuration-flash image and its programming chain (kept across power
cycles). Unzip it into `../synthesis/altera/bitstreams/` and program it:

```bash
cd ../synthesis/altera
./1_program_fpga.sh de0-nano-soc_leds_jtag     # try it: load the .sof
./3_program_flash.sh de0-nano-soc_leds_jtag    # keep it: write the .jic to the flash
```

`3_program_flash.sh` needs a Quartus Programmer with USB access (Windows or Linux); on
macOS, open the `.cdf` in the standalone Quartus Prime Programmer on another machine.

For another transport, set `DTM_TYPE` in `arvern_fpga.v` and rebuild (`./0_create_bitstream.sh demo_leds`, which
writes `bitstreams/demo_leds.sof`, then `./1_program_fpga.sh demo_leds`). The Quartus flow reads
`DTM_TYPE` from the RTL and applies the matching pad settings (pull-ups, or bus-hold for
cJTAG) and the TCK/TCKC timing constraint.

## JTAG (default build)

Two routes reach a GDB server on `localhost:3333`:

- **OpenOCD** (the configs here) — a standard `riscv` target; works with any
  RISC-V-capable OpenOCD (Homebrew `open-ocd` 0.12 has it). No arvern-tools needed.
- **`arvern-gdbserver`** from [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools)
  (expected as a sibling checkout, [`../../../../arvern-tools`](../../../../arvern-tools)) —
  a pure-Python server; also serves `:3333`. See
  [`doc/tools/gdbserver.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/tools/gdbserver.md).

Either way an IDE's *GDB Remote Debug* pointed at `:3333` needs no further change.

### Wiring — Adafruit FT232H → GPIO_1 header

FT232H in MPSSE mode; **I2C-mode switch OFF**. Common ground. There is no TRST: the TAP
resets through TMS.

Pins 31–40 have the layout of the Tang Console 138K SDRAM1 header (J10) pins 10–1, so one
debug cable fits both boards. Pin 40 is driven high (a 3.3 V reference for an adapter's
VTref) and pin 39 low (a ground reference): I/O pins, a few mA, not a supply. Pins 29/30
(+3.3 V / GND) do not match J10's 12/11 (GND / +5 V): a cable shared between the boards
stops at pins 31–40.

| GPIO_1 header pin | FPGA signal | FT232H |
|---|---|---|
| 37 · `GPIO_1[32]` | JTAG_TCK | D0 |
| 38 · `GPIO_1[33]` | JTAG_TMS | D3 |
| 36 · `GPIO_1[31]` | JTAG_TDI | D1 |
| 35 · `GPIO_1[30]` | JTAG_TDO | D2 |
| 12 or 30 | GND | GND |

### Wiring — J-Link (JTAG) → GPIO_1 header

For Ozone or the J-Link GDB Server with JTAG selected as the target interface. VTref
must be connected: without it the J-Link reports no target voltage and does not drive
the pins.

| J-Link 20-pin | Signal | GPIO_1 header pin | FPGA pin |
|---|---|---|---|
| 1 · VTref | target voltage | 40 · `GPIO_1[35]` (3.3 V reference) | AC22 |
| 5 · TDI | JTAG_TDI | 36 · `GPIO_1[31]` | AH18 |
| 7 · TMS | JTAG_TMS | 38 · `GPIO_1[33]` | AE23 |
| 9 · TCK | JTAG_TCK | 37 · `GPIO_1[32]` | AD23 |
| 13 · TDO | JTAG_TDO | 35 · `GPIO_1[30]` | AG21 |
| 4 (or any even pin 4–20) · GND | ground | 30 · GND | — |

nTRST (3), RTCK (11) and RESET (15) stay unconnected; use a fixed TCK speed, not adaptive
clocking.

The board-side pin/pad assignment is set in `../synthesis/altera/scripts/synthesis.tcl`
(weak pull-ups on TMS/TDI/TDO in the JTAG build) and the header map is mirrored
in `../rtl/verilog/arvern_fpga.v`.

### Configs

| File | Purpose | OpenOCD needed |
|---|---|---|
| `arvern-ft232h.cfg` | Scan-chain cross-check: read IDCODE, validate IR, exit. | any build |
| `arvern-ft232h-gdb.cfg` | GDB server on `:3333`, halts on attach / resumes on detach. | RISC-V support |
| `arvern-stress.cfg` | DM stress/soak run entirely inside OpenOCD (TCK sweep, SBA integrity + error paths, running-hart access, triggers, run-control soak). Source it *after* the GDB config: `openocd -f arvern-ft232h-gdb.cfg -f arvern-stress.cfg`. On an RV32E build (the default) its register-file phase reads x0–x15 only. | RISC-V support |
| `arvern-rtt.cfg` | **Does not run** — kept only as a record of why OpenOCD cannot serve RTT for a `riscv` target (see below). | — |

```bash
# Sanity-check the scan chain (expects IDCODE 0x080001F7):
openocd -f arvern-ft232h.cfg

# Run the GDB server (Ctrl-C to stop); point CLion / gdb at localhost:3333:
openocd -f arvern-ft232h-gdb.cfg
```

The GDB config routes memory over the **System Bus (SBA)** (`riscv set_mem_access
sysbus`) because the core is a frozen-hart / abstract-access DM with no program
buffer, and uses the Debug Module's `ndmreset` (`reset_config none`) since no
SRST/TRST pin is wired. Raise `adapter speed` (kHz) once the link is stable; the
bitstream constrains TCK to 25 MHz (`design.sdc`), and faster rates are outside the
timing-closed range.

## cJTAG with a J-Link

Rebuild `demo_leds` with `DTM_TYPE = 3` and program it. The link is two wires plus
reference and ground:

| J-Link 20-pin | Signal | GPIO_1 header pin | FPGA pin |
|---|---|---|---|
| 1 · VTref | target voltage | 40 · `GPIO_1[35]` (3.3 V reference) | AC22 |
| 7 · TMS | TMSC | 38 · `GPIO_1[33]` | AE23 |
| 9 · TCK | TCKC | 37 · `GPIO_1[32]` | AD23 |
| 4 (or any even pin 4–20) · GND | ground | 30 · GND | — |

- TCKC and TMSC use the JTAG TCK and TMS pins. In the cJTAG build both pads use the
  Cyclone V bus-hold circuit (`synthesis.tcl`): TMSC is undriven while TCKC is high, and
  the keeper holds its level, as the OScan1 protocol requires.
- TCKC runs between 500 kHz (the J-Link's cJTAG floor) and 6.25 MHz. The upper limit is
  the DTM's escape detector, which oversamples TMSC on the 50 MHz clock and needs
  `f_clk ≥ 8 × f_TCKC` (`design.sdc`;
  [`arv_dtm.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm.md)).

Then start Ozone and select cJTAG as the target interface when connecting to the J-Link.
The J-Link GDB Server works the same way, with cJTAG selected as its target interface.
OpenOCD over cJTAG is not validated, and `arvern-tools` cJTAG support is planned.

## UART and I2C

Rebuild `demo_leds` with `DTM_TYPE = 1` (UART) or `2` (I2C) and program it. The host
side is `arvern-tools`; adapter wiring and selection are in its
[`doc/adapters.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/adapters.md),
and the `demo_leds` Makefile has `run-uart` / `run-i2c` / `debug-uart` / `debug-i2c` targets.

| GPIO_1 header pin | FPGA signal | Pad |
|---|---|---|
| 32 · `GPIO_1[27]` | UART_TX (board → adapter RX) | push-pull output |
| 31 · `GPIO_1[26]` | UART_RX (adapter TX → board) | weak pull-up (UART build) |
| 33 · `GPIO_1[28]` | I2C_SCL | open-drain, weak pull-up (I2C build) |
| 34 · `GPIO_1[29]` | I2C_SDA | open-drain, weak pull-up (I2C build) |
| 12 or 30 | GND | — |

The I2C DTM answers at 7-bit address `0x30`.

## RTT terminal (bidirectional: printf out, commands in)

Firmware built against [`software/lib/segger`](../software/lib/segger) exposes a
SEGGER RTT terminal — `printf` from the target with no UART, no pins, and no
halting, plus a host→target channel for sending it commands. The host finds the
`"SEGGER RTT"` control block and reads/writes the ring buffers over SBA while the
core runs.

> **OpenOCD cannot do this for aRVern.** It registers the `rtt setup` / `rtt
> start` commands **per target type**, and `riscv` is not one of them — only
> `cortex_m` gets them (verified on 0.12; Homebrew and xpack builds behave
> identically). Only `rtt server`, the bare TCP frontend, is global, and on its
> own it serves nothing. `arvern-rtt.cfg` is kept only as a record of that
> finding; it cannot run. Use one of the three below instead.

| Host | How | Notes |
|---|---|---|
| **`arvern-minidebug`** | the **RTT** tab → *Attach* | Resolves the control block from the loaded ELF's `_SEGGER_RTT` symbol, so nothing needs configuring. |
| **`arvern-gdbserver`** | `--rtt-port 19021`, then `nc localhost 19021` | Works **while GDB holds the adapter** — the only way to see `printf` during an IDE debug session. Scans SRAM for the block (the server never sees your ELF). |
| **SEGGER tools** | `JLinkRTTViewer`, `JLinkRTTClient`, Ozone | Same protocol, same control block — but they drive a J-Link, wired to the debug pins of whichever DTM the bitstream carries (JTAG pins 35–38 and 40 on the default build, cJTAG pins 37, 38 and 40 with `DTM_TYPE = 3`). |

Only one consumer at a time: draining advances the target's read pointer
destructively, so two hosts polling the same block steal bytes from each other.

The control block lives in `.bss` and is filled in at runtime by
`SEGGER_RTT_Init()`, so it only exists **after the firmware has run** — at the
reset vector it is still zeroed. Load, run, then attach. Nothing appearing
usually means the hart is halted, not that RTT is broken.

All of this works only because SBA arbitrates for the data AHB port: a
halted-only SBA cannot service RTT at all, since the whole point is reading
memory of a *running* target.

## Scripts & IDE helpers

| File | Purpose |
|---|---|
| `run-gdbserver.sh` | Single front door to a GDB server on `:3333` over the JTAG build. Default backend is `arvern-gdbserver` (pure-Python, no OpenOCD needed); any server flag passes through, e.g. `./run-gdbserver.sh --jtag-freq 15000000`. `./run-gdbserver.sh --openocd` dispatches to OpenOCD via `arvern-ft232h-gdb.cfg` instead (TCK/port then come from that cfg). |
| `arvern.gdbinit` | Command-line GDB script: connect to `:3333`, `load`, stop at `main`, plus `reload` / `regs` / `trap` helpers. `riscv-none-elf-gdb -x arvern.gdbinit …/demo_leds.elf`. Not for IDEs, which open their own connection (see `ide-setup.md`). |
| `ide-setup.md` | Copy-paste VSCode `launch.json` and CLion *GDB Remote Debug* settings pointing at `:3333`. |

Both servers as configured here halt on attach (OpenOCD through the `gdb-attach` hook in
`arvern-ft232h-gdb.cfg`; stock OpenOCD does not) and advertise the CSRs + a virtual
`priv` register, so `arvern.gdbinit`'s `$mstatus`/`$mcause`/`$priv` helpers work against
either backend.

Full DTM/adapter reference:
[`arv_dtm_jtag.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm_jtag.md)
and [`arv_dtm_cjtag.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm_cjtag.md)
in `arvern-ips` (sibling checkout: [`../../../../arvern-ips/arv_dtm/doc/`](../../../../arvern-ips/arv_dtm/doc/)).

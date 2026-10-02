# Host-side debug

Debug-adapter configs, scripts and IDE notes for the aRVern core on this DE0-Nano-SoC
build. These are host-side tooling — nothing here feeds the Quartus flow (that lives
under `../synthesis/altera`).

## Which transport the board carries

The debug transport is picked at build time by `DTM_TYPE` in
[`../rtl/verilog/arvern_fpga.v`](../rtl/verilog/arvern_fpga.v). **The default build is
JTAG (`DTM_TYPE = 0`)**, and most of this page (FT232H wiring, OpenOCD configs,
`run-gdbserver.sh`, `ide-setup.md`) targets that build.

| `DTM_TYPE` | Transport | Host side | Prebuilt bitstream |
|---|---|---|---|
| 0 *(default)* | JTAG | OpenOCD with any adapter it supports, a J-Link, or [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools) (FT232H) | `leds_jtag.sof` |
| 1 | UART | `arvern-tools` | `leds_uart.sof` |
| 2 | I2C | `arvern-tools` | `leds_i2c.sof` |
| 3 | cJTAG | a J-Link in its cJTAG mode: SEGGER Ozone (select cJTAG as the target interface when connecting) or the J-Link GDB Server. OpenOCD over cJTAG is not validated; `arvern-tools` cJTAG support is planned. | `leds_cjtag.sof` |

Prebuilt bitstreams for every transport (`leds_jtag.sof`, `leds_uart.sof`, `leds_i2c.sof`,
`leds_cjtag.sof`) are attached to each release on the
[arvern-soc releases page](https://github.com/Arvern-Silicon/arvern-soc/releases); the
repository holds no bitstreams. Either download one into `../synthesis/altera/bitstreams/`
and program it:

```bash
cd ../synthesis/altera
./1_program_fpga.sh leds_cjtag          # or leds_uart, leds_i2c, leds_jtag
```

or set `DTM_TYPE` in `arvern_fpga.v` and rebuild (`./0_create_bitstream.sh leds`, which
writes `bitstreams/leds.sof`, then `./1_program_fpga.sh leds`). The Quartus flow reads
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

FT232H in MPSSE mode; **I2C-mode switch OFF**. Common ground. TRST_N is left open
(the JTAG build enables the pin's internal weak pull-up, so the TAP stays out of reset).

| GPIO_1 header pin | FPGA signal | FT232H |
|---|---|---|
| 1 · `GPIO_1[0]` | JTAG_TCK | D0 |
| 2 · `GPIO_1[1]` | JTAG_TMS | D3 |
| 3 · `GPIO_1[2]` | JTAG_TDI | D1 |
| 4 · `GPIO_1[3]` | JTAG_TRST_N | *(not connected)* |
| 5 · `GPIO_1[4]` | JTAG_TDO | D2 |
| 12 or 30 | GND | GND |

The board-side pin/pad assignment is set in `../synthesis/altera/scripts/synthesis.tcl`
(weak pull-ups on TMS/TDI/TRST_N/TDO in the JTAG build) and the header map is mirrored
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

Program `leds_cjtag.sof` from the release assets (or rebuild with `DTM_TYPE = 3`). The link is two wires plus
reference and ground:

| J-Link 20-pin | Signal | GPIO_1 header pin | FPGA pin |
|---|---|---|---|
| 1 · VTref | target voltage | 29 · +3.3 V | — |
| 7 · TMS | TMSC | 14 · `GPIO_1[11]` | AF20 |
| 9 · TCK | TCKC | 13 · `GPIO_1[10]` | AG19 |
| 4 (or any even pin 4–20) · GND | ground | 30 · GND | — |

- Both pads use the Cyclone V bus-hold circuit (`synthesis.tcl`): TMSC is undriven while
  TCKC is high, and the keeper holds its level, as the OScan1 protocol requires.
- TCKC runs between 500 kHz (the J-Link's cJTAG floor) and 6.25 MHz. The upper limit is
  the DTM's escape detector, which oversamples TMSC on the 50 MHz clock and needs
  `f_clk ≥ 8 × f_TCKC` (`design.sdc`;
  [`arv_dtm.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm.md)).

Then start Ozone and select cJTAG as the target interface when connecting to the J-Link.
The J-Link GDB Server works the same way, with cJTAG selected as its target interface.
OpenOCD over cJTAG is not validated, and `arvern-tools` cJTAG support is planned.

## UART and I2C

Program `leds_uart.sof` or `leds_i2c.sof` from the release assets (or rebuild with `DTM_TYPE = 1` / `2`). The host
side is `arvern-tools`; adapter wiring and selection are in its
[`doc/adapters.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/adapters.md),
and the `leds` Makefile has `run-uart` / `run-i2c` / `debug-uart` / `debug-i2c` targets.

| GPIO_1 header pin | FPGA signal | Pad |
|---|---|---|
| 7 · `GPIO_1[6]` | UART_TX (board → adapter RX) | push-pull output |
| 10 · `GPIO_1[9]` | UART_RX (adapter TX → board) | weak pull-up (UART build) |
| 8 · `GPIO_1[7]` | I2C_SCL | open-drain, weak pull-up (I2C build) |
| 9 · `GPIO_1[8]` | I2C_SDA | open-drain, weak pull-up (I2C build) |
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
| **SEGGER tools** | `JLinkRTTViewer`, `JLinkRTTClient`, Ozone | Same protocol, same control block — but they drive a J-Link, wired to the debug pins of whichever DTM the bitstream carries (JTAG pins 1–5 on the default build, cJTAG pins 13/14 with `DTM_TYPE = 3`). |

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
| `arvern.gdbinit` | Command-line GDB script: connect to `:3333`, `load`, stop at `main`, plus `reload` / `regs` / `trap` helpers. `riscv-none-elf-gdb -x arvern.gdbinit …/leds.elf`. Not for IDEs, which open their own connection (see `ide-setup.md`). |
| `ide-setup.md` | Copy-paste VSCode `launch.json` and CLion *GDB Remote Debug* settings pointing at `:3333`. |

Both servers as configured here halt on attach (OpenOCD through the `gdb-attach` hook in
`arvern-ft232h-gdb.cfg`; stock OpenOCD does not) and advertise the CSRs + a virtual
`priv` register, so `arvern.gdbinit`'s `$mstatus`/`$mcause`/`$priv` helpers work against
either backend.

Full DTM/adapter reference:
[`arv_dtm_jtag.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm_jtag.md)
and [`arv_dtm_cjtag.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm_cjtag.md)
in `arvern-ips` (sibling checkout: [`../../../../arvern-ips/arv_dtm/doc/`](../../../../arvern-ips/arv_dtm/doc/)).

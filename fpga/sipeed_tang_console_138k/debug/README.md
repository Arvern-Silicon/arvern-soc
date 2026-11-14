# Host-side debug

Debug-adapter configs, scripts and IDE notes for the aRVern core on this Tang Console
138K build. These are host-side tooling — nothing here feeds the Gowin flow (that lives
under `../synthesis/gowin`). The board's own USB port (BL616) configures the FPGA; the
core's debug link is on the SDRAM1 header (J10), pins 1–10, at the end next to the left
board edge.

## Which transport the board carries

The debug transport is picked at build time by `DTM_TYPE` in
[`../rtl/verilog/arvern_fpga.v`](../rtl/verilog/arvern_fpga.v). **The default build is
JTAG (`DTM_TYPE = 0`)**, and most of this page (FT232H wiring, OpenOCD configs,
`run-gdbserver.sh`, `ide-setup.md`) targets that build.

| `DTM_TYPE` | Transport | Host side |
|---|---|---|
| 0 *(default)* | JTAG | OpenOCD with any adapter it supports, a J-Link, or [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools) (FT232H) |
| 1 | UART | `arvern-tools` |
| 2 | I2C | `arvern-tools` |
| 3 | cJTAG | a J-Link in its cJTAG mode: SEGGER Ozone (select cJTAG as the target interface when connecting) or the J-Link GDB Server. OpenOCD over cJTAG is not validated; `arvern-tools` cJTAG support is planned. |

To change the transport, set `DTM_TYPE` in `arvern_fpga.v` and rebuild
(`./0_create_bitstream.sh demo_leds` in `../synthesis/gowin`, then
`./1_program_fpga.sh demo_leds`). The Gowin flow reads `DTM_TYPE` from the RTL and applies
the matching pad settings (pull-ups, or the pad keeper for cJTAG) and the TCK/TCKC timing
constraint.

## JTAG (default build)

Two routes reach a GDB server on `localhost:3333`:

- **OpenOCD** (the configs here) — a standard `riscv` target; works with any
  RISC-V-capable OpenOCD (Homebrew `open-ocd` 0.12 has it). No arvern-tools needed.
- **`arvern-gdbserver`** from [`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools)
  (expected as a sibling checkout, [`../../../../arvern-tools`](../../../../arvern-tools)) —
  a pure-Python server; also serves `:3333`. See
  [`doc/tools/gdbserver.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/tools/gdbserver.md).

Either way an IDE's *GDB Remote Debug* pointed at `:3333` needs no further change.

### Wiring — SDRAM1 header (J10)

J10 is the 2 × 20 header along the board's bottom edge (seen from the PMOD / SDRAM-header
side, PMODs on the right): pins 1/2 at its left end, next to USB-C J21 and KEY1, odd pins
on the row along the board edge. The drawing is in
[`../rtl/verilog/arvern_fpga.v`](../rtl/verilog/arvern_fpga.v). The input pads have weak
pull-ups. There is no TRST: the TAP resets through TMS. Common ground with the adapter.

Pins 1–10 have the layout of the DE0-Nano-SoC GPIO_1 header pins 40–31, so one debug
cable fits both boards.

| J10 pin | FPGA pin | JTAG / cJTAG build | UART build | I2C build |
|---|---|---|---|---|
| 1 | R19 | 3.3 V reference (VTref) | — | — |
| 2 | P19 | GND reference | — | — |
| 3 | U21 | JTAG_TMS / CJTAG_TMSC | — | — |
| 4 | T21 | JTAG_TCK / CJTAG_TCKC | — | — |
| 5 | R17 | JTAG_TDI | — | — |
| 6 | P16 | JTAG_TDO | — | — |
| 7 | T18 | — | — | I2C_SDA |
| 8 | R18 | — | — | I2C_SCL |
| 9 | W17 | — | UART_TX (board → adapter RX) | — |
| 10 | V17 | — | UART_RX (adapter TX → board) | — |
| 12 | — | GND | GND | GND |

**3.3 V reference.** J10 has no 3.3 V pin: pin 11 is +5 V, and pins 29/30, +3.3 V / GND on
a DE0 GPIO header, are FPGA pins here (the board leaves out the 0 Ω options R30/R32). The
design drives pin 1 high, so a J-Link's VTref there senses 3.3 V, and pin 2 low, a ground
reference beside it. They are I/O pins (a few mA): references, not a supply. **Never connect VTref to the +5 V pin (11)**: the J-Link
would drive 5 V into the FPGA's 3.3 V pins. Pins 11/12 do not match the DE0 header's
29/30 (+3.3 V / GND there): a cable shared between the boards stops at pins 1–10.

FT232H (MPSSE, **I2C-mode switch OFF**): D0 → TCK, D1 → TDI, D2 ← TDO, D3 → TMS.

J-Link (20-pin): 1 VTref → pin 1, 5 TDI, 7 TMS (TMSC in cJTAG), 9 TCK (TCKC in cJTAG),
13 TDO, 4 GND. nTRST (3), RTCK (11) and RESET (15) stay unconnected; use a fixed TCK speed,
not adaptive clocking. VTref must be connected: without it the J-Link reports no target
voltage and does not drive the pins.

The pin and pad settings are in `../synthesis/gowin/scripts/design.cst`, and the pin map
is mirrored in `../rtl/verilog/arvern_fpga.v`.

### Configs

| File | Purpose | OpenOCD needed |
|---|---|---|
| `arvern-ft232h.cfg` | Scan-chain cross-check: read IDCODE, validate IR, exit. | any build |
| `arvern-ft232h-gdb.cfg` | GDB server on `:3333`, halts on attach / resumes on detach. | RISC-V support |
| `arvern-stress.cfg` | DM stress/soak run entirely inside OpenOCD (TCK sweep, SBA integrity + error paths, running-hart access, triggers, run-control soak). Source it *after* the GDB config: `openocd -f arvern-ft232h-gdb.cfg -f arvern-stress.cfg`. On an RV32E build (the default) its register-file phase reads x0–x15 only; the default build has no triggers (`DM_TRIGGER_NR = 0`, set `STRESS_TRIGGERS` otherwise), so its watchpoint checks are skipped. | RISC-V support |
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

Rebuild with `DTM_TYPE = 3`. The link is two wires plus reference and ground:

| J-Link 20-pin | Signal | J10 pin | FPGA pin |
|---|---|---|---|
| 1 · VTref | target voltage | 1 | R19 |
| 7 · TMS | TMSC | 3 | U21 |
| 9 · TCK | TCKC | 4 | T21 |
| 4 (or any even pin 4–20) · GND | ground | 12 | — |

- TCKC and TMSC use the JTAG TCK and TMS pins. In the cJTAG build both pads use the pad
  keeper (`synthesis.tcl`): TMSC is undriven while TCKC is high, and the keeper holds its
  level, as the OScan1 protocol requires.
- TCKC runs between 500 kHz (the J-Link's cJTAG floor) and 3.75 MHz, the rate `design.sdc`
  constrains. The DTM's escape detector oversamples TMSC on the 33.333 MHz system clock and
  needs `f_clk ≥ 8 × f_TCKC`, a bound of 4.17 MHz
  ([`arv_dtm.md`](https://github.com/Arvern-Silicon/arvern-ips/blob/main/arv_dtm/doc/arv_dtm.md)).

Then start Ozone and select cJTAG as the target interface when connecting to the J-Link.
The J-Link GDB Server works the same way, with cJTAG selected as its target interface.
OpenOCD over cJTAG is not validated, and `arvern-tools` cJTAG support is planned.

## UART and I2C

Rebuild with `DTM_TYPE = 1` / `2`. The host
side is `arvern-tools`; adapter wiring and selection are in its
[`doc/adapters.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/adapters.md),
and the `demo_leds` Makefile has `run-uart` / `run-i2c` / `debug-uart` / `debug-i2c` targets.

| J10 pin | FPGA pin | FPGA signal | Pad |
|---|---|---|---|
| 9 | W17 | UART_TX (board → adapter RX) | push-pull output |
| 10 | V17 | UART_RX (adapter TX → board) | weak pull-up |
| 7 | T18 | I2C_SDA | open-drain |
| 8 | R18 | I2C_SCL | open-drain |
| 12 | — | GND | — |

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
| **SEGGER tools** | `JLinkRTTViewer`, `JLinkRTTClient`, Ozone | Same protocol, same control block — but they drive a J-Link, wired to the debug pins of whichever DTM the bitstream carries (J10 pins 1 and 3–6 on the default JTAG build, pins 1, 3 and 4 with cJTAG, `DTM_TYPE = 3`). |

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

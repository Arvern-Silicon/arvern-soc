<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)"
            srcset="https://raw.githubusercontent.com/Arvern-Silicon/arvern/main/doc/img/aRVern_dark_title.png">
    <img src="https://raw.githubusercontent.com/Arvern-Silicon/arvern/main/doc/img/aRVern_light_title.png" alt="aRVern" width="400">
  </picture>
</p>

# chip_example: an ASIC synthesis trial vehicle

*The aRVern core with the AHB interconnect, the ROM / SRAM controllers, the ACLINT
and the Belenos graphics controller at chip level, with realistic I/O, DFT signals and
memory macros, for synthesis and implementation trials.*

> **This is not a functional chip.** It ships no firmware, no functional simulation
> and no verified pin-out, and it is not meant to be taped out or run as is. For a
> working system, use one of the [FPGA examples](../../README.md#examples-in-this-repository).

The top level is [`rtl/verilog/chip_example.v`](rtl/verilog/chip_example.v); the bus
system, with the address map, is
[`rtl/verilog/ahb_bus_system.v`](rtl/verilog/ahb_bus_system.v). The ASIC synthesis
results of this example are the timing reference for the core's architecture: an FPGA
tool's critical path is checked against them before the RTL changes.

---

## The core

The configuration exercises the largest core:

| Feature | This build |
|---|---|
| Base ISA | RV32I (32 registers) |
| Extensions | M (one-cycle multiplier, 33-cycle divider), Zca + Zcb + Zcmp + Zcmt, Zba + Zbb + Zbs + Zbc |
| Privilege | Machine, Supervisor and User modes, 16 PMP entries |
| Counters | Zicntr, one Zihpm counter |
| Custom CSRs | the custom-CSR interface, with registers on chip ports |
| Debug | RISC-V Debug 1.0, JTAG transport, two Sdtrig triggers |
| Branches | zero bubble on a taken branch (`SINGLE_CYCLE_BRANCH = 1`) |
| Reset | asynchronous (`ASYNC_RST_EN = 1`) |

The parameters are at the top of [`chip_example.v`](rtl/verilog/chip_example.v).

## Clocks and DFT

| Clock | Port | Default constraint | Clocks |
|---|---|---|---|
| `free_clk` | `free_clk_i` | 10 ns (100 MHz), `CLOCK_PERIOD` | the core, the buses, the memories, Belenos |
| `clk_lf` | `clk_lf_i` | 32.768 kHz, `CLOCK_LF_PERIOD` | the ACLINT's `mtime` |
| `jtag_tck` | `jtag_tck_i` | 10 MHz, `TCK_PERIOD` | the debug TAP |

The library setup file can override each period. The `clk_lf` crossing inside the
ACLINT is a timed path with a budget, not a false path, so it stays in the same clock
group as `free_clk`; `jtag_tck` is asynchronous to both. `scan_mode_i` is the test
mode: it bypasses the reset synchronisers and the clock-as-data paths (tie it to 0
functionally).

## Memory and address map

| Base | Size | Region |
|---|---|---|
| `0x0200_0000` | 64 KB | ACLINT (MSWI / MTIMER / SSWI) |
| `0x1004_0000` | 128 B | AHB peripheral #0 |
| `0x1004_1000` | 128 B | AHB peripheral #1 |
| `0x1004_2000` | 128 B | AHB peripheral #2 |
| `0x1004_3000` | 4 KB | Belenos registers |
| `0x2000_0000` | 32 KB | ROM |
| `0x8000_0000` | 32 KB | executable SRAM |
| `0x8100_0000` | 32 KB | non-executable SRAM |
| `0x8200_0000` | 128 KB | Belenos VRAM window |

The three AHB peripherals are register blocks with their registers on chip ports
(eight outputs, eight inputs each), so the bus paths end at real I/O. The machine
external interrupt is a chip input; the ACLINT generates the timer and software
interrupts.

The memories are built from an 8 KB single-port macro wrapper, `sram_8kb_wrapper`,
which the library setup supplies (`SRAM_VERILOG_WRAPPER`): it is deliberately not in
the design filelist. [`bench/verilog/sram_8kb_wrapper.v`](bench/verilog/sram_8kb_wrapper.v)
is a behavioural stand-in with the same ports and timing shape (registered read
output, byte write enables), for lint only.

## Belenos

Belenos is built with the GPU's x/y coordinates and clipping, gradients, alpha
blending, triangles and run-ahead, a two-bank colour LUT and a 128 KB VRAM. Its
display driver is `DIF_TYPE = 3`: both MIPI-DBI interfaces (the 8080 parallel bus and
SPI) are chip ports.

## The AHB fabric

The bus system is built in one of three interconnect variants, chosen at build time:

| Variant | Interconnect |
|---|---|
| `FUSED` (the default) | the fused ROM / SRAM controllers: one memory serves the instruction and data ports |
| `HIPERF` | the multi-layer interconnect |
| `GENERIC` | the single-layer interconnect |

---

## Synthesis (Design Compiler)

```bash
cd synthesis/synopsys
cp libraries/setup_lib_example.tcl libraries/setup_lib_default.tcl   # once: fill in your library paths
./run_syn                                   # FUSED fabric, the default library flavor
./run_syn -fabric HIPERF -lib <flavor>      # another fabric or library flavor
./run_syn_d                                 # the same flow in a container
./run_syn_sweep -tag <name>                 # every fabric in turn, reports kept per snapshot
```

As in `arvern-ips`, the target technology is a library *flavor*: a
`libraries/setup_<flavor>.tcl` file that names the libraries, the memory macro and,
optionally, the clock periods. Your filled-in `setup_lib_default.tcl` is gitignored.

`run_syn` writes its reports to `results/` (wiped on every run); `run_syn_sweep`
copies each run's text reports to `results_sweep/<tag>/<fabric>/`, and
[`summarize_timing.py`](synthesis/synopsys/summarize_timing.py) compares two snapshots
(for instance before and after an RTL change). Extra reports cover the Belenos paths,
the SRAM paths and the worst violations.

## Lint

```bash
cd sim/rtl_sim/run && ./run_lint           # Verilator, all three fabric variants
cd lint/vc_static && ./run_vclint          # VC Static (see its README)
```

`run_lint` uses the behavioural SRAM wrapper; `-default`, `-fahb` or `-hiperf` restrict
it to one variant. [`lint/vc_static`](lint/vc_static/README.md) is the sign-off
structural lint with Synopsys VC Static.

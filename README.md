<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)"
            srcset="https://raw.githubusercontent.com/Arvern-Silicon/arvern/main/doc/img/aRVern_dark_title.png">
    <img src="https://raw.githubusercontent.com/Arvern-Silicon/arvern/main/doc/img/aRVern_light_title.png" alt="aRVern" width="500">
  </picture>
</p>

<h1 align="center">arvern-soc</h1>

<p align="center">
  Integration examples for the <strong>aRVern</strong> RISC-V core and its IP library.
</p>

---

## Examples in this repository

| Example | Purpose | Functional? |
|---|---|---|
| [`asic/chip_example`](asic/chip_example) | Synthesis and implementation trials of the core with the AHB interconnect and the ROM / SRAM controllers | **No.** A synthesis trial vehicle, not a working chip |
| [`fpga/alteral_de0_nano_soc`](fpga/alteral_de0_nano_soc) | A complete system on a Terasic DE0-Nano-SoC board, with firmware and external debug | **Yes.** Runs software on the board |

Both examples assemble [`arvern`](https://github.com/Arvern-Silicon/arvern) and
[`arvern-ips`](https://github.com/Arvern-Silicon/arvern-ips), and expect those repositories
checked out **next to** this one. The FPGA debug flow also uses
[`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools), checked out the same way.

```bash
git clone https://github.com/Arvern-Silicon/arvern.git
git clone https://github.com/Arvern-Silicon/arvern-ips.git
git clone https://github.com/Arvern-Silicon/arvern-soc.git
git clone https://github.com/Arvern-Silicon/arvern-tools.git     # FPGA debug only
```

## Release notes

Latest release: **1.0.0**.

What changed in each release, and how to upgrade: [`CHANGELOG.md`](CHANGELOG.md).

## `asic/chip_example`: a synthesis trial vehicle

> **This is not a functional chip.** The chip example exists to run synthesis and
> implementation trials of the aRVern core together with the AHB interconnect and the ROM /
> SRAM controllers, at chip level, with realistic I/O, DFT signals and memory macros. It ships
> no firmware, no functional simulation and no verified pin-out, and it is not meant to be
> taped out or run as is. For a working system, use the FPGA example.

What it provides:

- **The integration**: [`chip_example.v`](asic/chip_example/rtl/verilog/chip_example.v)
  instantiates the core, the AHB bus system, and 32 KB ROM and SRAM blocks. The memories are
  built from a technology-independent `sram_8kb_wrapper`, which your library setup supplies.
- **Synthesis** with Design Compiler (`synthesis/synopsys/`): `run_syn` selects the interconnect
  variant with `-fabric FUSED` (default), `HIPERF` or `GENERIC`. `run_syn_sweep` runs several
  configurations, and `run_syn_d` runs the flow in a container. As in `arvern-ips`, the target
  technology is a library *flavor*. Copy `libraries/setup_lib_example.tcl` to
  `libraries/setup_lib_default.tcl` and fill in your library paths before the first run.
- **Lint** with Verilator (`sim/rtl_sim/run/run_lint`) and VC Static (`lint/vc_static/`).

## `fpga/alteral_de0_nano_soc`: a working system on the DE0-Nano-SoC

A complete aRVern system on the Terasic DE0-Nano-SoC (Cyclone V `5CSEMA4U23C6`), clocked at
50 MHz, with an ACLINT timer, example peripherals and the external-debug link.

- **Firmware** (`software/apps/`): `leds`, an interrupt-driven LED demo with SEGGER RTT output,
  and `busmash`, a test firmware that saturates the data bus to stress system bus access from
  the debugger. The toolchain settings are generated from the RTL configuration, so the build
  always matches the synthesized core.
- **Bitstream flow** (`synthesis/altera/`), using Quartus natively or through the container set
  up by `setup_d.sh`:
  1. `0_create_bitstream.sh <app>` builds the firmware and the bitstream;
  2. `1_program_fpga.sh` programs the board;
  3. `2_generate_flash_file.sh` and `3_program_flash.sh` write the configuration flash.

  Prebuilt `leds` bitstreams for each debug transport are attached to every release on the
  [releases page](https://github.com/Arvern-Silicon/arvern-soc/releases).
- **External debug**: the debug transport is chosen at build time by `DTM_TYPE` in
  [`arvern_fpga.v`](fpga/alteral_de0_nano_soc/rtl/verilog/arvern_fpga.v): `0` JTAG (the
  default build), `1` UART, `2` I2C, `3` cJTAG.
  - JTAG: OpenOCD with any adapter it supports, a J-Link, or `arvern-tools` (FT232H).
  - cJTAG: a J-Link in its cJTAG mode, through SEGGER Ozone (select cJTAG as the target
    interface when connecting) or the J-Link GDB Server. OpenOCD over cJTAG is not
    validated; `arvern-tools` cJTAG support is planned.
  - UART and I2C: `arvern-tools`.

  How to select or rebuild a transport, adapter wiring for each, OpenOCD configurations and
  IDE setup are in [`debug/`](fpga/alteral_de0_nano_soc/debug/README.md).
- **Simulation and lint**: a board-level testbench (`sim/rtl_sim/`), Verilator lint, and
  VC Static lint (`lint/vc_static/`).

## License

BSD 3-Clause — see [`LICENSE`](LICENSE). Third-party components and their licenses are listed in
[`THIRD_PARTY.md`](THIRD_PARTY.md).

---

<p align="center">
  <a href="https://github.com/Arvern-Silicon">github.com/Arvern-Silicon</a>
  &nbsp;·&nbsp;
  <a href="mailto:arvernsilicon@gmail.com">arvernsilicon@gmail.com</a>
</p>

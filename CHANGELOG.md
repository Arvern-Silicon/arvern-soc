# Changelog

All notable changes to the aRVern SoC examples (the `chip_example` ASIC integration and the
DE0-Nano-SoC FPGA build) are listed here. Versions follow
[Semantic Versioning](https://semver.org/) and are released together with the aRVern core and IPs.

## Versions

| Version | Date |
|---|---|
| [1.0.0](#v1.0.0) | Oct 2, 2026 |
| [0.1.0-preview](#v0.1.0-preview) | Jun 24, 2026 |

<a id="v1.0.0"></a>

## 1.0.0

First stable release, built on aRVern core 1.0.0 and the 1.0.0 IP library.

### Added

- **Prebuilt bitstreams** of the `leds` application for each debug transport (JTAG, UART, I2C,
  cJTAG), attached to the release.
- **External debug** on both examples through `arv_dtm`. The FPGA build selects its transport at
  build time (`DTM_TYPE`: JTAG, UART, I2C or cJTAG; JTAG by default), and the Quartus flow
  applies the matching pad settings (pull-ups, or bus-hold for cJTAG) and timing constraints.
- **Host-side debug setup** under `fpga/alteral_de0_nano_soc/debug/`: OpenOCD configurations for
  the JTAG DTM (FT232H adapter), a `run-gdbserver.sh` front end for `arvern-gdbserver` or
  OpenOCD, a Debug Module stress script, IDE setup notes, and the J-Link wiring for cJTAG. RTT
  output is read with `arvern-tools` or the SEGGER tools (OpenOCD's `riscv` target has no RTT
  support).
- **Firmware**: SEGGER RTT integrated into the `leds` application, and a new `busmash` system-bus
  stress application.
- **PMP**: a `PMP_NR` parameter on the FPGA build, set to 4 entries by default.
- **Lint and constraints**: VC Static lint flows for both examples, DFT signals with
  low-frequency clock-crossing constraints, JTAG timing constraints and violation reports.
- **Timing tools**: a timing summary, a sweep runner and a summarizer for the synthesis flows.

### Changed

- Both examples use aRVern core 1.0.0 with external debug.
- The reset generator is the `arv_primitives` reference block `arv_reset_gen`, and the
  low-frequency clock is unified across the design.
- The FPGA program memory is a writable 32 KB memory (`pmem_32kb`) instead of a ROM, so the
  debugger can load firmware.
- The AHB interconnects carry HMASTER tags.

### Removed

- The NMI vector port and the NMI configuration (the core now uses its `marv_nmvec` CSR).
- The `MVENDORID` parameter.

<a id="v0.1.0-preview"></a>

## 0.1.0-preview

Initial hardware baseline preview of the aRVern FPGA and chip integration examples: the
open-source repository baseline, build tooling and a basic verification setup. A pre-release for
early evaluation and integration testing; features, interfaces and register structures were
subject to change before the first stable release.

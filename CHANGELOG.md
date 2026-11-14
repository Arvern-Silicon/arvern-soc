# Changelog

All notable changes to the aRVern SoC examples (the `chip_example` ASIC integration and the
DE0-Nano-SoC FPGA build) are listed here. Versions follow
[Semantic Versioning](https://semver.org/) and are released together with the aRVern core and IPs.

## Versions

| Version | Date |
|---|---|
| [1.1.0](#v1.1.0) | Nov 14, 2026 |
| [1.0.0](#v1.0.0) | Oct 2, 2026 |
| [0.1.0-preview](#v0.1.0-preview) | Jun 24, 2026 |

<a id="v1.1.0"></a>

## 1.1.0

Adds the Belenos graphic controller (0.1.0-preview) to every example and a second FPGA
board, the Sipeed Tang Console 138K, with games, LVGL and demos. Built on aRVern core
1.0.2, the 1.1.0 IP library and Belenos 0.1.0-preview.

### Added

- **Prebuilt bitstreams and an example microSD card**, attached to the release: a
  bitstream of every DE0-Nano-SoC demo (`demo_*`, with the JTAG debug transport; the
  other transports are a rebuild with `DTM_TYPE`), the Tang Console boot menu for the panel and for HDMI, and the content of a microSD card
  with the programs and media for the boot menu
  ([Tang Console building](fpga/sipeed_tang_console_138k/README.md#building)).
- **Sipeed Tang Console 138K** (`fpga/sipeed_tang_console_138k`): a small game console on
  a 480 × 272 RGB panel or on HDMI 720 × 480, with Belenos (`classic` persona, lines and
  hardware cursor), two USB hosts (keyboard, mouse, gamepad), I2S audio to HDMI, the
  speaker or a Pmod I2S2, a microSD card in SPI mode and external debug on J10. Gowin
  flow with a placement sweep, flash programming, a board-level simulation bench and an
  application regression with golden images
  ([README](fpga/sipeed_tang_console_138k/README.md)).
- **Boot menu** (Tang): lists the programs on the microSD card, plays music, and loads the
  chosen program; the card can be read and written from the host through a J-Link while
  the menu runs (`sdlink.py`), and `sd_install.py` prepares a program for the card. The
  HDMI and the panel builds keep their own sound output on a shared card.
- **Applications** (Tang): ports of Commander Keen 4, Wolfenstein 3D (shareware episode),
  Prince of Persia (the game's resources fetched by a script, not redistributed) and the
  Zork trilogy (Z-machine), an audio player (WAV, FLAC, QOA, MP3, MOD and tracker
  formats), a picture viewer, an LVGL v9 demo on the Belenos draw unit with the hardware
  cursor as its pointer, the graphics demos, and tests (USB HID, I2S, microSD, Belenos).
- **Belenos on the DE0-Nano-SoC**: the `classic` persona on the Terasic LT24 module
  (320 × 240, ILI9341 over 8080), with the graphics demos (logo, effects, raycaster,
  Mode 7, water, text, UI), the Belenos smoke test and video-capture tools.
- **Belenos in `chip_example`**: the controller on the AHB interconnect with its VRAM,
  synthesis constraints and a report of the Belenos paths.
- **DE0-Nano-SoC peripherals**: a PLIC (47 sources), GPIO on the GPIO 1 header (26 pins)
  and the Arduino header (16 pins).
- **Shared software**: `demo_kit` (display, input, fonts, music), FatFs, the SEGGER RTT
  library, and host tools for frame snapshots, video and profiling over a J-Link.

### Changed

- **DE0-Nano-SoC core**: M extension with a single-cycle multiplier; S and U modes and the
  PMP removed (M-mode software only).
- **DE0-Nano-SoC memories**: 48 KB program memory and 48 KB data SRAM; Belenos has
  160 KB of VRAM.
- **DE0-Nano-SoC display**: the LT24 module on the GPIO 0 header, driven by Belenos.
- **DE0-Nano-SoC debug header**: JTAG on GPIO 1 pins 31–40, the same J-Link wiring as the
  Tang's J10 so one cable serves both boards.
- Board I/O sits behind an AHB-to-APB bridge, which carries the AHB master ID.

### Upgrading from 1.0.0

1. DE0-Nano-SoC: move the debug adapter to GPIO 1 pins 31–40
   ([debug](fpga/alteral_de0_nano_soc/debug/README.md)).
2. DE0-Nano-SoC firmware: rebuild for M-mode only (no S/U mode, no PMP) and for the new
   memory sizes; the linker scripts in `software/` are up to date.
3. Check out [`arvern-belenos`](https://github.com/Arvern-Silicon/arvern-belenos) next to
   this repository: every example now builds it.

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

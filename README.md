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

Each example has its own README, with the full description: the core configuration,
the clocks, the address map, the interrupts, the memory budget, building, software,
simulation and debug.

| Example | Purpose | Functional? |
|---|---|---|
| [`asic/chip_example`](asic/chip_example/README.md) | Synthesis and implementation trials of the core with the AHB interconnect, the ROM / SRAM controllers and Belenos | **No.** A synthesis trial vehicle, not a working chip |
| [`fpga/alteral_de0_nano_soc`](fpga/alteral_de0_nano_soc/README.md) | A complete system on a Terasic DE0-Nano-SoC board, on an LT24 LCD module, with firmware and external debug | **Yes.** Runs the demos on the board |
| [`fpga/sipeed_tang_console_138k`](fpga/sipeed_tang_console_138k/README.md) | A small game console on a Sipeed Tang Console 138K, on a 480 × 272 RGB panel or on HDMI, with a boot menu loading programs from a microSD card, USB input, audio and external debug | **Yes.** Runs games, LVGL and demos on the board |

The examples assemble [`arvern`](https://github.com/Arvern-Silicon/arvern),
[`arvern-ips`](https://github.com/Arvern-Silicon/arvern-ips) and the Belenos graphic controller
[`arvern-belenos`](https://github.com/Arvern-Silicon/arvern-belenos), and expect those
repositories checked out **next to** this one. The FPGA debug flow also uses
[`arvern-tools`](https://github.com/Arvern-Silicon/arvern-tools), checked out the same way.

```bash
git clone https://github.com/Arvern-Silicon/arvern.git
git clone https://github.com/Arvern-Silicon/arvern-ips.git
git clone https://github.com/Arvern-Silicon/arvern-belenos.git
git clone https://github.com/Arvern-Silicon/arvern-soc.git
git clone https://github.com/Arvern-Silicon/arvern-tools.git     # FPGA debug only
```

## Release notes

Latest release: **1.1.0**, with Belenos 0.1.0-preview.

What changed in each release, and how to upgrade: [`CHANGELOG.md`](CHANGELOG.md).

## The FPGA boards at a glance

| | DE0-Nano-SoC | Tang Console 138K |
|---|---|---|
| FPGA | Intel Cyclone V `5CSEMA4U23C6` | Gowin `GW5AST-LV138PG484A` |
| Tools | Quartus Prime Lite | Gowin EDA |
| `clk_sys` | 50 MHz | 33.854 MHz |
| Core | RV32E, M, Zc (with Zcmp, Zcmt), B, two triggers | RV32E, M, Zca + Zcb, B, no triggers |
| Belenos | `classic` | `classic` |
| Display | LT24 module, 320 × 240 | 480 × 272 RGB panel or HDMI 720 × 480 |
| Program / data / video RAM | 48 KB / 48 KB / 160 KB | 160 KB / 128 KB / 272 KB |
| Audio, microSD, USB | none | I2S (HDMI, speaker, Pmod), microSD, two USB hosts |
| Debug | JTAG, UART, I2C or cJTAG on GPIO 1 | the same, on the SDRAM1 header |

Both run the SoC from on-chip memory only, with every peripheral from `arvern-ips` or
`arvern-belenos`.

## License

BSD 3-Clause — see [`LICENSE`](LICENSE). Third-party components and their licenses are listed in
[`THIRD_PARTY.md`](THIRD_PARTY.md).

---

<p align="center">
  <a href="https://github.com/Arvern-Silicon">github.com/Arvern-Silicon</a>
  &nbsp;·&nbsp;
  <a href="mailto:arvernsilicon@gmail.com">arvernsilicon@gmail.com</a>
</p>

# demo_audio — Tang Console 138K music box

A music box on the board's sound output: the I2S master (`apb_i2s`) feeds
HDMI, the onboard MAX98357A amplifier or the line out of a Digilent Pmod I2S2
on PMOD1, as the board I/O `AUDIO_OUT` register selects (from reset: HDMI in
the HDMI build, the Pmod in the panel build; the boot menu sets it). No
display needed. The cross-toolchain and ISA are read from the
RTL-generated `march_config.sh`, so the build always matches the core you
synthesized.

## What the demo does

**Synthesis.** Four voices, each a phase accumulator reading one 256-entry
wavetable (a fundamental and three overtones) with an exponentially decaying
envelope: notes ring and overlap like a music box. Low notes ring longer, high
notes fade faster, the bass plays a little softer. The CPU computes the mix
sample by sample and writes it to the transmit FIFO, which paces the loop: the
sample counter is the only clock. The stream is 16-bit stereo at 44.08 kHz
(MCLK / LRCK ratio 384), the same sample on both channels.

**Outputs.** The demo starts on the output it finds selected. The Pmod I2S2
plugs into **PMOD1** (J7, the socket between the mode button SW2 and the HDMI
end; line out: the green 3.5 mm jack). Holding `KEY[1]` for 3/4 s moves the
stream to the next output: HDMI → speaker → Pmod (no HDMI in the panel
build). The switch stops the I2S master, writes `AUDIO_OUT`
(`lks_audio_output`) and restarts the stream: the amplifier (`PA_EN`) is on
only for the speaker, the PMOD1 pins carry the Pmod I2S2 only while it is the
output, and its D/A gets the MCLK it needs (16.9 MHz, 384 × fs). The music
carries on where it was.

**Pieces.** Four public-domain pieces play in a loop; a short press on
`KEY[1]` skips to the next one, on release (`KEY[0]` is the reset).

| # | Piece |
|---|---|
| 0 | Ode to Joy (Beethoven), melody and bass |
| 1 | Für Elise, opening (Beethoven) |
| 2 | Prelude in C major BWV 846, opening (Bach) |
| 3 | Arpeggios over the speaker's range |

**LEDs.** LED register bit 0 (a SoM LED) toggles on every melody note. Bit 1
(the other LED) marks the beat: on the speaker it flashes on the first
sixteenth of every beat, on the Pmod it stays lit and goes dark on that
sixteenth. Bits [7:4] hold the piece number, bit 3 is set after a transmit
underrun (the CPU did not keep up) and bit 2 is the output (1 = Pmod); they
have no LED, the board-level testbench follows them.

The firmware polls; it uses no interrupt. It is written on the I2S and GPIO
drivers (`arvern-ips/apb_i2s/sw`, `arvern-ips/apb_gpio/sw`) and the board support in [`../../bsp`](../../bsp)
(memory map, LED / key / speaker-enable peripheral).

## Build

```bash
make                 # -> demo_audio.elf (+ .ihex, .lst, .size) and demo_audio_sim.*
make clean
```

`TC_OPT` and `INST_MODE` work as in the other apps (see
[`../demo_leds/README.md`](../demo_leds/README.md)).

## Load & run

```bash
make run              # = run-jtag: JTAG/FT232H  (DTM_TYPE=0, the default)
```

The other transports (`run-uart`, `run-uart-usbiss`, `run-i2c`) and the debug
targets are those of [`../demo_leds/README.md`](../demo_leds/README.md). To
boot the demo from the bitstream instead:
`../../../synthesis/gowin/0_create_bitstream.sh demo_audio`.

## Simulation

```bash
cd ../../../sim/rtl_sim/run && ./run demo_audio -nodump
```

The simulation flavour (`demo_audio_sim.elf`, `-DSIMULATION`) runs at 65.1 kHz
(ratio 256) with the tempo eight times faster and the long press sixteen times
shorter. The stimulus
([`src/demo_audio.v`](../../../sim/rtl_sim/src/demo_audio.v)) decodes the
amplifier pins and the Pmod's D/A pins in the panel build and checks the
stream on the output from reset, the Pmod (both channels equal, sound within
the mix limit, the speaker silent, MCLK running and `PA_EN` low), the notes
of the first piece without an underrun, that a short `KEY[1]` press starts
the next piece, that a long one moves the stream to the speaker (`PA_EN`
high, the Pmod silent) and that a second one brings it back.

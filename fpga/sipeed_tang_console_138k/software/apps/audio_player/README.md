# audio_player — Tang Console 138K music player

Plays the WAV, QOA, FLAC and MP3 files and the ProTracker, Scream Tracker 3 and FastTracker 2 modules of
the microSD card's `/MEDIA/MUSIC` folder, in name order and in a loop, on the board's sound output: HDMI, the onboard speaker
or a Digilent Pmod I2S2 (PMOD1), as the boot menu set it (from reset: HDMI in
the HDMI build, the Pmod in the panel build), with a live spectrum of the
music on the screen. The cross-toolchain and ISA are read from the
RTL-generated `march_config.sh`, so the build always matches the core you
synthesized.

## Using it

1. Put `.WAV`, `.QOA`, `.FLA` (FLAC), `.MP3`, `.MOD`, `.S3M` or `.XM` files in `/MEDIA/MUSIC` on a FAT12/16/32 card,
   with 8.3 names (`TRACK01.WAV`, ...); the name order is the playing
   order. Other files, and files the player cannot decode, are skipped.
2. For the Pmod output, plug the Pmod I2S2 into **PMOD1** (J7, the socket
   between the mode button SW2 and the HDMI end) and listen on its line out
   (green 3.5 mm jack).
3. Load the bitstream or the ELF (below). Playback starts on its own.

| Gamepad | Keyboard | Board | Action |
|---|---|---|---|
| right | → | `KEY[1]` short press | next track |
| left | ← | | previous track; after a track's first 3 s, its start |
| L / R | − / = | | volume 0 to 8 (3 dB a step; 8: the samples as they are) |
| A, Start | Space, Enter | | pause / resume |
| X | F3 | `KEY[1]` held 3/4 s | the next sound output: HDMI → speaker → Pmod I2S2 (no HDMI in the panel build) |
| | | `KEY[0]` | reset: the playlist restarts |

The gamepad or keyboard plugs into either USB port, mapped as in the boot
menu; the help line at the bottom of the screen follows what is connected.
The volume starts at the boot menu's (`/APPS/LAST.TXT`), else at 8.

**Playable files.** Mono or stereo, 1 kHz to 96 kHz, in four formats
([`codec.c`](codec.c), [`flac.c`](flac.c)), and tracker modules
([`trk.c`](trk.c), [`mod.c`](mod.c), [`s3m.c`](s3m.c), [`xm.c`](xm.c)):

| Format | File | Size of a minute of 44.1 kHz stereo | Made with |
|---|---|---|---|
| PCM, 8 or 16 bits | `.WAV` (format 1, also `WAVE_FORMAT_EXTENSIBLE` with a PCM sub-format) | 10.6 MB | any audio tool |
| IMA ADPCM, 4 bits | `.WAV` (format 0x11, blocks of up to 4136 bytes) | 2.7 MB | e.g. `ffmpeg -i in.flac -c:a adpcm_ima_wav OUT.WAV` |
| QOA ("Quite OK Audio"), 3.2 bits | `.QOA` | 2.1 MB | the format's reference converter, `qoaconv in.wav OUT.QOA` |
| FLAC, lossless, 8 to 24 bits | `.FLA` (8.3 name) | about 6 MB (depends on the music) | any FLAC encoder, e.g. `flac -o OUT.FLA in.wav`; blocks of up to 4608 samples |
| MP3 (MPEG-1 Layer III), 32 to 320 kbit/s | `.MP3` | about 1 MB at 128 kbit/s | any MP3 encoder (e.g. LAME); 32, 44.1 or 48 kHz (not the MPEG-2 rates) |
| ProTracker module, 4 to 8 channels | `.MOD` | the module: often 20 to 250 KB for a whole song | a tracker (ProTracker, OpenMPT, MilkyTracker, ...); up to about 240 KB |
| Scream Tracker 3 module, up to 16 channels | `.S3M` | the module: often 50 to 600 KB | Scream Tracker 3, OpenMPT, Schism Tracker, ...; up to about 240 KB, samples played at 8 bits |
| FastTracker 2 module, up to 32 channels | `.XM` | the module: often 20 KB (chiptunes) to 3 MB | FastTracker 2, MilkyTracker, OpenMPT, ...; up to about 240 KB, samples played at 8 bits |

FLAC is the original, bit for bit, at about 60 % of the size; QOA sounds
close to it at a fifth; MP3 at 128 kbit/s takes a tenth; IMA ADPCM, at a
quarter, adds audible hiss on quiet passages. A WAV's `LIST INFO`, a FLAC's
Vorbis comment (`TITLE`, `ARTIST`), an MP3's ID3v2 tag (`TIT2`, `TPE1`)
give the title and artist; other metadata (pictures, ...) is skipped by a
seek. 24-bit FLAC plays as 16 bits.

**Screen.** 480 x 272 on the panel or the HDMI monitor, in the boot menu's
glass style: the track number, its title and artist (the WAV file's
`LIST INFO` `INAM` and `IART`, else the file name) and its format; the
spectrum, 32 bars from 40 Hz to 16 kHz over 66 dB with peak caps that hold,
then fall, and a reflection (taken before the volume); the progress, the
time (of the music played: it stops with a pause), the CPU load ("Paused"
while paused), the sound output and the volume; the help line. Errors are
shown in place of the bars.

**LEDs.** LED register bit 0 (a SoM LED) toggles every quarter second of
playback; bit 1 (the other LED) is lit on the speaker output. On an error (no
card, no `/MEDIA/MUSIC` folder, no WAV file) the two LEDs blink in turn and
the player retries every 2 s, so a card can be inserted later. Bits [7:4]
hold the track number, bit 3 is set when the ring ran dry during the track,
bits [2:1] are the output (0 HDMI, 1 speaker, 2 Pmod); they have no LED, the board-level testbench
follows them.

## How it works

- **File system.** FatFs on the microSD slot ([`../../lib/fatfs`](../../lib/fatfs)):
  the disk layer drives the `apb_spi` controller and its SD-card layer at
  16.9 MHz (hclk / 2).
- **Clocks.** The I2S master runs at a fixed 44.08 kHz (MCLK / LRCK ratio
  384; the Pmod's D/A needs MCLK, 16.9 MHz). Every file plays at its own
  rate through a resampler: a Q16 phase step (file rate / 44.08 kHz) and
  linear interpolation between neighbouring frames. A rate within 0.1 % of
  44.08 kHz (44.1 kHz) is sent as it is, 0.8 cent low: the interrupt only
  copies.
- **Decoders** ([`codec.c`](codec.c)). Each turns the bytes of a read into
  stereo frames of the ring, whole units only (a PCM frame, an ADPCM block,
  a QOA frame), the rest kept for the next read; each frame is published to
  the interrupt as soon as it is decoded, so a track starts at once. A read
  is sized so its frames fit the ring's room. IMA ADPCM: each block starts
  with each channel's sample and step index, then 4-bit codes (stereo:
  4 bytes of the left, then 4 of the right). QOA: frames of up to 256
  slices of 20 samples a channel, a 4-tap sign-sign LMS predictor and
  3-bit residuals scaled by one of 16 factors a slice. Both match their
  reference decoders sample for sample (the QOA reference code; the macOS
  decoder for ADPCM).
- **QOA in assembly** ([`qoa_slice.S`](qoa_slice.S)). A whole slice is
  decoded in place in the ring: its residuals are unpacked into the
  channel's halves of 20 frames, and the slice runs with the predictor's
  whole state (4 history samples, 4 weights) in the 12 registers an RV32E
  leaf function has, the history renamed by each step of a 4-step loop
  instead of moved, each weight updated with one scratch register
  (`w -= s; w += s ^ d`, s the history's sign), the 16-bit clamp out of
  line. About 31 instructions a sample; with the unpacking, 45 for the
  whole decoder, against about 70 in C (the core's
  `-O2`, RV32E). A file's last slice, and a slice crossing the ring's end,
  take the C path.

| Format | Decoding, cycles a sample (simulation, the interrupt's share included) |
|---|---|
| PCM, 16 bits | 18 (stereo) to 30 (mono) |
| IMA ADPCM | 51 |
| QOA | 55 |
| FLAC | 107 (a synthetic tone, the costliest case; music needs fewer) |
| MP3, 128 to 192 kbit/s stereo | about 513 a stereo frame on the board (410 to 430 instructions on spike): decoding about 67 % of the CPU, the whole load (the interrupt, the spectrum and the drawing too) about 80 % |
| MOD, 4 channels | about 130 a stereo frame (an estimate: 106 instructions on spike) |
| S3M, 7 / 12 channels | about 180 / 390 a stereo frame (estimates: 147 / 323 instructions on spike) |
| XM, 10 channels (a chiptune) | about 215 a stereo frame (an estimate: 179 instructions on spike) |

The status block's `dec_ticks` and `dec_frames` give the current track's
decoding time (mtime ticks) and frames, `isr_ticks`, `fft_ticks` and
`draw_ticks` the time since reset in the interrupt, the spectrum and the
drawing: read twice over a J-Link, they give the share of each.
- **FLAC** ([`flac.c`](flac.c)). The stream is pulled through a bit reader
  from the card (a frame never has to fit in memory): a frame's header
  (checked by its CRC-8; a false sync is skipped), one subframe a channel
  (CONSTANT, VERBATIM, FIXED orders 0-4 or LPC up to order 32, wasted bits)
  into two 4608-sample buffers, the stereo decorrelation (left/side,
  side/right, mid/side), then 16-bit frames into the ring. An LPC sum takes
  64 bits when the sample size, the coefficients' precision and the order
  could overflow 32 (the reference decoder's rule); up to order 12 the
  32-bit sum is unrolled. The Rice-coded residuals, most of the work, are
  [`flac_rice.S`](flac_rice.S): the bit reader's state in registers, the
  unary part by one count of leading zeros (Zbb `clz`), the low bits by two
  shifts, about 28 instructions a sample; a sample near the buffer's end,
  after a run of zeros longer than the cache or with a parameter over 24
  takes the generic reader. The decoder matches the reference decoder
  sample for sample on music encoded by the reference encoder at every
  preset, block sizes from 192 to 4608, LPC orders up to 32 (64-bit sums),
  8, 16 and 24 bits, with tags and an embedded picture, and on files that
  exercise every coding feature; a stream with blocks over 4608 samples is
  refused. Its cost follows the encoding: about 69 instructions a sample
  for `flac -8` music, about 171 for LPC order 32 with 15-bit coefficients
  (`--lax`, outside the format's subset).
- **MP3** ([`mp3.c`](mp3.c)). MPEG-1 Layer III in fixed point (Q24
  spectrum, high-word products): the stream pulled through the FLAC
  reader's callbacks (an ID3v2 tag read for the title and artist, its
  pictures skipped by a seek; a Xing / Info frame gives a VBR file's
  length), the bit reservoir, scale factors, the Huffman-coded spectrum
  (a 32-bit bit cache, the decoding trees of [`mp3_tab.h`](mp3_tab.h)),
  requantisation, mid/side and intensity stereo (intensity stereo and
  mixed blocks are implemented, but no test file uses them yet), short-block reordering,
  alias reduction, the IMDCT (an 18-point DCT-IV by two 9-point DCT-IIIs;
  silent subbands only overlap), and the polyphase synthesis (a generated
  32-point DCT, [`mp3_dct32.h`](mp3_dct32.h), and the 512-tap window, each
  tap at a constant offset; for stereo, both channels in one pass in
  [`mp3_win.S`](mp3_win.S), each coefficient loaded once). Its working
  memory (33 KB) is the FLAC
  decoder's buffers. It matches macOS's decoder (`afconvert`) within one
  LSB (80 dB and more) on a 128 kbit/s LAME-encoded song and on tones at
  32, 44.1 and 48 kHz, stereo and mono. About 410 instructions a stereo
  frame at 128 kbit/s on spike (430 at 192); on the board about 67 % of
  the CPU at 44.1 kHz.
- **Modules** ([`trk.c`](trk.c)). A module is loaded whole, patterns and
  samples each where there is room: the FLAC decoder's buffers, the program
  memory after the program image, the VRAM's second frame (about 240 KB in
  all; a larger module is skipped). Its sequencer ends the song where it
  would come back to a row it played; a dry run at load gives its length.
  The voices are mixed at 44.1 kHz with linear interpolation. A voice has a
  volume and one of 17 pannings; for each block of frames the voices at the
  same panning share a group (up to 8): each adds sample times volume into
  the group's sums ([`trk_mix.S`](trk_mix.S): 18 instructions a voice and
  frame, the same results as the C mixer), and the group sums are panned
  once a frame (a ninth panning takes the C mixer with its own two gains).
  - **ProTracker** ([`mod.c`](mod.c)): 31 samples; 4 channels, or 6 or 8,
    "xxCH". The ProTracker effects (arpeggio, slides, tone portamento,
    vibrato, tremolo, sample offset, volume slides, position jump, pattern
    break, pattern loop and delay, retrigger, note cut and delay, finetune,
    panning, speed and tempo). The channels are panned left, right, right,
    left, softened to 3/4 and 1/4. About 106 instructions a frame for 4
    channels.
  - **Scream Tracker 3** ([`s3m.c`](s3m.c)): up to 16 sample channels
    (AdLib channels are ignored), 8- or 16-bit samples, signed or unsigned,
    kept at 8 bits. The ST3 commands A to X but panbrello (speed, order
    jump, pattern break, volume slides and fine slides, portamentos fine and
    extra fine, tone portamento, vibrato and fine vibrato, tremor, arpeggio,
    sample offset, retrigger with volume change, tremolo, the S commands,
    tempo and tempo slides, global volume, panning), with ST3's shared
    parameter memory and the header's master volume; a mono module plays
    every channel on both sides. About 147 instructions a frame for 7
    channels, 323 for 12 with nine pannings.
  - **FastTracker 2** ([`xm.c`](xm.c)): up to 32 channels, 128 instruments
    of up to 16 samples, 8- or 16-bit delta-coded samples kept at 8 bits,
    forward and ping-pong loops (unrolled at load), linear or Amiga
    frequencies. Instruments: a sample per note, volume and panning
    envelopes with sustain and loop, fadeout, auto-vibrato. The effects 0-F
    (8xx full-range panning), G H K L P R T X1 X2, and the volume column
    (volume, slides, fine slides, vibrato, panning, panning slides, tone
    portamento), each effect with its own parameter memory. About 179
    instructions a frame for a 10-channel chiptune.
- **Interrupt.** The I2S `TX_LOW` interrupt (PLIC source 17, transmit FIFO
  at or below 4 of its 16 words: 12 frames an interrupt) refills the FIFO from a ring of 8192
  source frames (32 KB), about 0.19 s at 44.1 kHz. A FIFO lasts only
  0.37 ms, less than one card read, so the main loop never feeds it
  directly: it reads the file into the ring in 4 KB, sector-aligned pieces
  and polls the key. An empty ring sends silence (and is counted).
- **Spectrum** ([`spectrum.c`](spectrum.c)). The interrupt keeps the last
  1024 output samples (left + right halved, before the volume); 30 times a
  second the main loop takes the last 512 through a Hann window. The input
  being real, the 512 samples go through a 256-point complex FFT (even
  samples real, odd ones imaginary) and the 512-point spectrum's bins are
  unpacked from it: half the work of a 512-point FFT, the same result. The
  FFT is radix 2 in Q15, each stage rounded and halved so nothing overflows
  (twiddles from a rotation in Q30 at start-up); its butterflies are
  [`spectrum_fft.S`](spectrum_fft.S), each stage its own loop so that its
  offsets are in the instructions (37 instructions a butterfly, the same
  results as the C version). Each log-spaced band then takes its loudest
  bin, a band narrower than a bin the power interpolated at its edge, as a
  level over 66 dB from an integer log2. About 63,000 instructions a
  spectrum.
- **Drawing** ([`screen.c`](screen.c), on [`demo_kit`](../../lib/demo_kit)).
  One frame drawn in place: the background, panels and text by the CPU
  (8 x 16 font), only when they change; each bar by the GPU, just after a
  refresh starts: a fill of its empty part and a copy of its lit part from
  a sprite of the full bar in the spare VRAM, then a fill for its peak cap.
  A frame is drawn only once the ring holds a quarter of its frames (or the
  file is read to its end), so a track starts at once.
- **CPU load.** The share of time spent in the interrupt, in converting the
  samples and in the spectrum and drawing, over the last second (card reads
  and refresh waits not counted).
- **Output switch.** With the I2S interrupt masked and the master stopped,
  the board I/O `AUDIO_OUT` register moves the stream (`lks_audio_output`):
  the other outputs fall idle, the amplifier is on only for the speaker and
  the PMOD1 pins carry the Pmod I2S2 only while it is the output.

The status block `player` (a symbol in the ELF) holds counters for a
debugger: state (0 mount, 1 scan, 2 play, 3 error), error code, tracks,
current track and its format, output samples played, samples the ring could
not feed, the slowest file read (in output samples), files played.

The firmware is written on the I2S, GPIO, SPI (+ SD layer) and PLIC drivers
of `arvern-ips` and the board support in [`../../bsp`](../../bsp).

## Build

```bash
make                 # -> audio_player.elf (+ .ihex, .lst, .size), audio_player_sim.*, sim/sd_image.*
make clean
```

`TC_OPT` and `INST_MODE` work as in the other apps (see
[`../demo_leds/README.md`](../demo_leds/README.md)).

## Load & run

```bash
make run              # = run-jtag: JTAG/FT232H  (DTM_TYPE=0, the default)
```

The other transports and the debug targets are those of
[`../demo_leds/README.md`](../demo_leds/README.md). To boot the player from
the bitstream instead: `../../../synthesis/gowin/0_create_bitstream.sh audio_player`.

## Simulation

```bash
cd ../../../sim/rtl_sim/run && ./run audio_player -nodump
```

`make` writes the simulated card, [`sim/gen_card.py`](sim/gen_card.py): a
2 MB image (an MBR, one FAT12 partition, pure Python) whose `/MEDIA/MUSIC`
holds an unplayable 24-bit WAV, a text file and ten short tones (22.05 kHz
mono 16-bit with a `LIST` chunk, 44.1 kHz stereo 16-bit, 11.025 kHz mono
8-bit with its data at offset 4096, 22.05 kHz stereo IMA ADPCM with a short
last block, 44.1 kHz stereo QOA, 44.1 kHz stereo FLAC with mid/side, LPC
and FIXED subframes and a title, a ProTracker, a Scream Tracker 3 and a
FastTracker 2 module each playing a sine, a 44.1 kHz stereo MP3), the
ADPCM, QOA, FLAC, MOD, S3M, XM and MP3 ones written by its own generator
(the MP3 one: the analysis filterbank, an MDCT, one gain a granule, a
single Huffman table, no psychoacoustics). The simulation flavour
(`audio_player_sim.elf`, `-DSIMULATION`) runs the I2S at 66.1 kHz (ratio
256) with the long press sixteen times shorter. The stimulus
([`src/audio_player.v`](../../../sim/rtl_sim/src/audio_player.v)) decodes the
Pmod and amplifier pins and checks each tone's frequency (from the first to
the last rising zero crossing of a window) and level after decoding and
resampling, stereo, the skipped files, a short `KEY[1]` press skipping a
track, the playlist wrapping, a long press moving the stream to the speaker,
and that the ring never runs dry. The bench's panel model shows the screen:
every fourth frame lands in `frame_log/fb_<n>.png` (upside down, as the
panel model saves it).

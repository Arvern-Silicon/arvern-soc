# Third-Party Software

aRVern-SoC's own source in this repository — the SoC RTL, FPGA projects, firmware,
and tooling — is licensed under the **BSD 3-Clause** license (see [LICENSE](LICENSE)).

This repository additionally **vendors** the third-party components listed below.
They are **not** aRVern code, retain their own upstream license, and are included
under `fpga/*/software/lib/`. Vendored firmware libraries *are* linked into example
firmware images, so their license terms travel with any binary built from them —
check the terms below before redistributing a build.

## SEGGER RTT (Real Time Transfer)

- **Location:** `fpga/alteral_de0_nano_soc/software/lib/segger/`
- **Upstream:** https://github.com/SEGGERMicro/RTT
- **Version:** **RTT V8.58.0** (2026-06-03), branch `main`, commit
  `4d8feab3150f86f37a9d323ddc88d6cdf5673072`. The sources carry no version macro;
  the release number comes from `SEGGER.RTT.pdsc` in the same repo, so pin the
  commit as well when refreshing. Every vendored file except
  `Config/SEGGER_RTT_Conf.h` is byte-identical to that commit — verify with a
  re-download and `cmp` rather than by eye.
- **License:** **1-clause BSD-style**, SEGGER's own — full text in
  `fpga/alteral_de0_nano_soc/software/lib/segger/LICENSE.md`, and repeated in the
  header of every source file. Redistribution in source and binary form is
  permitted provided the copyright notice, condition, and disclaimer are retained.
- **Copyright:** SEGGER Microcontroller GmbH.
- **Upstream layout is preserved verbatim** (`RTT/`, `Config/`, `Syscalls/`,
  `Examples/`) so refreshing it is a re-download rather than a merge. Do not
  reformat or re-indent these files: SEGGER "strongly recommends to not make any
  changes to or modify the source code in order to stay compatible with the
  SystemView and RTT protocol, and J-Link" — and the on-target control block
  layout *is* the wire protocol that J-Link, Ozone, and OpenOCD parse.
- **Local configuration** belongs in `Config/SEGGER_RTT_Conf.h`, which upstream
  ships intentionally empty for exactly this purpose; every tunable has a default
  in `RTT/SEGGER_RTT_ConfDefaults.h`. Keeping local settings there rather than
  editing the defaults keeps the vendored tree pristine.
- **RISC-V note:** `SEGGER_RTT_ConfDefaults.h` already provides a RISC-V critical
  section (`csrci mstatus, 8` to clear `MIE`, restored on unlock). It clobbers
  only `a1`/`x11`, so it is valid on RV32E as well as RV32I.

## IBM Plex Sans (rendered text)

- **Location:** `fpga/alteral_de0_nano_soc/software/apps/demo_arvern_logo/logo_data.h`
  (the `txt_*` bitmaps), `fpga/alteral_de0_nano_soc/software/apps/demo_effects/font_data.h`
  (the scroller font) and `fpga/alteral_de0_nano_soc/software/apps/demo_text/text_data.h`
  (the text demo's fonts and title).
- **What is included:** no font file. `demo_arvern_logo/tools/logo2c.py` renders the logo
  demo's few text lines from IBM Plex Sans SemiBold, and `demo_effects/tools/font2c.py` the
  scroller's capitals, digits and punctuation from IBM Plex Sans Bold, and
  `demo_text/tools/text2c.py` the text demo's fonts (Medium, SemiBold, Bold) and title, all
  at build time, stored as 1, 2 or 4-bit bitmaps.
- **Upstream:** https://github.com/IBM/plex
- **License:** **SIL Open Font License 1.1**. The OFL covers the font software; images
  rendered with the font, like these bitmaps, are not font software and are free to use.
  The font itself is not redistributed here.
- **Copyright:** IBM Corp.

## MP3 decoding tables (audio player)

- **Location:** `fpga/sipeed_tang_console_138k/software/apps/audio_player/mp3_tab.h`.
- **What is included:** data tables only. From **minimp3**: the Layer III Huffman decoding
  trees (`huff_tab`, `huff_tab32`, `huff_tab33`, `huff_index`, `huff_linbits`) and the
  scale-factor band widths (`sfb_long`, `sfb_short`). From **PDMP3**: the synthesis window
  D[] of ISO/IEC 11172-3 (`synth_d`, converted to Q30; cross-checked against minimp3's own
  copy of the window). The other tables of `mp3_tab.h` and the unrolled DCT of
  `mp3_dct32.h` are computed from the standard's formulas. The decoder (`mp3.c`) is
  aRVern's own code; its fast 18-point DCT-IV follows minimp3's factorization (two 9-point
  DCT-IIIs and twiddles).
- **Upstream:** https://github.com/lieff/minimp3 (commit
  `ea99364f61c14656440e8d77e9c233ccf3124633`) and https://github.com/technosaurus/PDMP3
  (commit `2a206a3ada78ec4d13434847f05f50097c60c36a`).
- **License:** minimp3: **CC0 1.0** (public domain dedication); PDMP3: **The Unlicense**
  (public domain dedication). Neither imposes conditions.

### Note for license scanners

An automated scan of this repository will report both `BSD-3-Clause` (aRVern's own
code) and SEGGER's 1-clause BSD-style license. That is expected. The two are
compatible; the SEGGER terms require only that the notice be retained.

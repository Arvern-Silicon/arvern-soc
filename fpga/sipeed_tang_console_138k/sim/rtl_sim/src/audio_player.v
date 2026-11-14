//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    audio_player
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : audio_player.v
// Module Description : arvern FPGA test stimulus for the WAV player
//                      (software/apps/audio_player).
//
//   The microSD slot holds the FAT12 image of the app's sim/gen_card.py
//   (sd_card_img.v): /MEDIA/MUSIC with an unplayable 24-bit WAV (track 0),
//   a text file, and five tone files: track 1 22050 Hz mono 16-bit 1000 Hz,
//   track 2 44100 Hz stereo 16-bit left 500 Hz / right 1500 Hz, track 3
//   11025 Hz mono 8-bit 2000 Hz, track 4 22050 Hz stereo IMA ADPCM left
//   750 Hz / right 2500 Hz, track 5 44100 Hz stereo QOA left 600 Hz / right
//   3000 Hz, track 6 44100 Hz stereo FLAC left 800 Hz / right 2000 Hz, track
//   7 a ProTracker module, a 1036 Hz sine on its left channel, track 8 a
//   Scream Tracker 3 module, a 1251.5 Hz sine on its right channel, track 9
//   a FastTracker 2 module, a 1173.4 Hz sine panned right, track 10 a
//   44100 Hz stereo MP3, left 900 Hz / right 2200 Hz. The firmware
//   decodes and resamples them to the I2S
//   rate (66.1 kHz in simulation) on the board's sound output, from reset
//   the panel build's Pmod I2S2; the LED register carries the track [7:4],
//   a ring-underrun flag [3] and the output [2:1] (1 speaker, 2 Pmod). This
//   stimulus decodes the Pmod's D/A pins and the amplifier
//   pins (I2S, 32-bit slots, 16-bit samples MSB first) and:
//     1) track 1 on the Pmod: the tone's frequency and level, both channels
//        equal (the broken file and the text file skipped);
//     2) track 2: the two channels' frequencies and levels;
//     3) a short KEY[1] press in track 2 starts track 3;
//     4) track 3 (8-bit): frequency and level;
//     5) tracks 4 (ADPCM), 5 (QOA) and 6 (FLAC): both channels' frequencies
//        and levels; track 7 (MOD): the tone's frequency on both sides, its
//        level, and the softened panning (the right side a third of the left);
//        track 8 (S3M): the same, the channel panned right (3 / 13 of 16);
//        track 9 (XM): the same, the sample panned to 12 of 16; track 10
//        (MP3): both channels' frequencies and levels;
//     6) the playlist wraps to track 1; a long KEY[1] press moves the stream
//        to the speaker;
//   and checks the ring never runs dry (LED[3]). The bench's RGB panel
//   model shows the player's screen: every fourth frame is saved
//   (frame_log/fb_<n>.hex, 480 x 272 RGB565, made PNG by the run script).
// vscreen: no_input   (the stimulus drives KEY)
// defines: -D TIMEOUT_MS=1700
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

`define SD_IMAGE "../../../software/apps/audio_player/sim/sd_image.mem"

localparam integer FS = 66121;            // simulation output rate (33.854 MHz / 512)

integer    ierrors, cnt;
integer    pfr, pc;                      // Pmod D/A frames, BCLK edges in the slot
reg        plr_q;
reg [15:0] psl, psr;
integer    nfr, nc, nloud;               // amplifier frames
reg        nlr_q;
reg [15:0] nsl;
reg        win;                          // measurement window on the Pmod stream
integer    wfr, wxl, wxr, wpl, wpr, wdiff;
integer    wl0, wl1, wr0, wr1;           // frames of the first and last rising crossings
reg        sl_neg, sr_neg;
reg        dry, armed;                  // ring watch, armed once track 1 plays

wire [3:0] track = LED[7:4];

integer     frame_nr = 0;
reg [255:0] fname;

always @(panel.frame_done) if ($time > 0)
    begin
       if (frame_nr % 4 == 0)
         begin
            $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
            $writememh(fname, panel.fb);
         end
       frame_nr = frame_nr + 1;
    end

// Bench interface of the card model
wire [3:0] spi_pad = {2'b11, SD_DAT0, SD_CMD};
reg        t_none;
reg  [2:0] t_sel;
reg        sd_dat0_oe;
reg        sd_dat0_v;

assign SD_DAT0 = sd_dat0_oe ? sd_dat0_v : 1'bz;

task dev_drive;
   input [3:0] oe;
   input [3:0] v;
   begin
      sd_dat0_oe = oe[1];
      sd_dat0_v  = v[1];
   end
endtask

`include "../src/sd_card_img.v"

always @(negedge SD_DAT3) sd_cs_assert;
always @(posedge SD_DAT3) sd_cs_release;
always @(posedge SD_CLK)  sd_sample;
always @(negedge SD_CLK)  sd_launch;

// TTY echo
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0])
    $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);

// Pmod I2S2 D/A: LRCK PMOD1[2], SCLK PMOD1[4], SDIN PMOD1[6]; the MSB of a
// slot on the second rising SCLK edge after the LRCK change
always @(posedge PMOD1[4])
  begin
     if (PMOD1[2] !== plr_q) pc = 0;
     else                    pc = pc + 1;
     if ((pc >= 1) && (pc <= 16))
       begin
          if (PMOD1[2]) psr = {psr[14:0], PMOD1[6]};
          else          psl = {psl[14:0], PMOD1[6]};
       end
     if (PMOD1[2] && (pc == 16))
       begin
          pfr = pfr + 1;
          if (win)
            begin
               wfr = wfr + 1;
               if (sl_neg && !psl[15]) begin if (wxl == 0) wl0 = wfr; wl1 = wfr; wxl = wxl + 1; end
               if (sr_neg && !psr[15]) begin if (wxr == 0) wr0 = wfr; wr1 = wfr; wxr = wxr + 1; end
               if ($signed(psl) > wpl) wpl = $signed(psl);
               if ($signed(psr) > wpr) wpr = $signed(psr);
               if (psl !== psr) wdiff = wdiff + 1;
            end
          sl_neg = psl[15];
          sr_neg = psr[15];
       end
     plr_q = PMOD1[2];
  end

// Onboard amplifier
always @(posedge I2S_BCLK)
  begin
     if (I2S_LRCK !== nlr_q) nc = 0;
     else                    nc = nc + 1;
     if ((nc >= 1) && (nc <= 16) && !I2S_LRCK) nsl = {nsl[14:0], I2S_DOUT};
     if (I2S_LRCK && (nc == 16))
       begin
          nfr = nfr + 1;
          if ($signed(nsl) > 2000 || $signed(nsl) < -2000) nloud = nloud + 1;
       end
     nlr_q = I2S_LRCK;
  end

always @(posedge dut.clk_sys)
  if (armed && (LED[3] === 1'b1) && !dry)
    begin
       dry = 1'b1;
       $display("  FAIL: the ring ran dry in track %0d", track);
       ierrors = ierrors + 1;
    end

task wait_ms;
   input integer n;
   begin
      repeat (n * 33333) @(posedge dut.clk_sys);
   end
endtask

// Wait for a track, at most ms milliseconds
task wait_track;
   input [3:0]   t;
   input integer ms;
   integer       k;
   begin
      for (k = 0; (k < ms) && (track !== t); k = k + 1)
         wait_ms(1);
      if (track !== t) begin $display("  FAIL: track %0d not reached (track %0d)", t, track); ierrors = ierrors + 1; end
   end
endtask

// Measure n Pmod frames
task measure;
   input integer n;
   begin
      wfr = 0; wxl = 0; wxr = 0; wpl = 0; wpr = 0; wdiff = 0;
      win = 1'b1;
      wait (wfr >= n);
      win = 1'b0;
   end
endtask

// A measured channel: frequency (the cycles between the first and the last
// rising crossing over the frames between them) within 4 %, peak within
// [lo, hi]
task check_tone;
   input [8*5:1] what;
   input integer x, x0, x1, peak, f, lo, hi;
   integer       fm;
   begin
      fm = (x >= 2) ? ((x - 1) * FS) / (x1 - x0) : 0;
      if ((fm * 100 < f * 96) || (fm * 100 > f * 104))
        begin $display("  FAIL: %0s %0d Hz (%0d Hz expected)", what, fm, f); ierrors = ierrors + 1; end
      else if ((peak < lo) || (peak > hi))
        begin $display("  FAIL: %0s peak %0d (%0d..%0d expected)", what, peak, lo, hi); ierrors = ierrors + 1; end
      else
        $display("  ok:   %0s %0d Hz, peak %0d", what, fm, peak);
   end
endtask

task press_key1;
   input integer n;
   begin
      KEY[1] = 1'b0;
      wait_ms(n);
      KEY[1] = 1'b1;
   end
endtask

initial
   begin
      ierrors = 0; pfr = 0; pc = 0; nfr = 0; nc = 0; nloud = 0; win = 1'b0; dry = 1'b0; armed = 1'b0;
      plr_q = 1'b1; nlr_q = 1'b1; psl = 16'h0; psr = 16'h0; nsl = 16'h0; sl_neg = 1'b0; sr_neg = 1'b0;
      t_none = 1'b0; t_sel = 3'd0; sd_dat0_oe = 1'b0; sd_dat0_v = 1'b1;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA AUDIO PLAYER TEST (WAV files from the SD)       |");
      $display(" ====================================================================");
      $display("");

      force dut.ahb_bus_system_inst.apb_board_io_inst.sync_deb_key1.debounce_threshold = 19'd15;

      // 1) Track 1: 22050 Hz mono 16-bit, 1000 Hz at 8000
      wait_track(4'd1, 60);
      armed = 1'b1;
      wait_ms(8);
      cnt = ierrors;
      measure(2000);
      check_tone("1 L  ", wxl, wl0, wl1, wpl, 1000, 7200, 8200);
      if (wdiff != 0) begin $display("  FAIL: track 1: %0d frames with different channels", wdiff); ierrors = ierrors + 1; end
      if (LED[2:1] !== 2'd2) begin $display("  FAIL: output LED[2:1] = %0d (2, Pmod, expected)", LED[2:1]); ierrors = ierrors + 1; end
      if (nfr != 0)        begin $display("  FAIL: %0d frames on the speaker", nfr); ierrors = ierrors + 1; end
      if (ierrors == cnt)  $display("  PASS: track 1 on the Pmod (the broken WAV and the text file skipped)");

      // 2) Track 2: 44100 Hz stereo, left 500 Hz at 6000, right 1500 Hz at 12000
      wait_track(4'd2, 80);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("2 L  ", wxl, wl0, wl1, wpl,  500, 5400,  6200);
      check_tone("2 R  ", wxr, wr0, wr1, wpr, 1500, 10800, 12200);
      if (ierrors == cnt)  $display("  PASS: track 2, stereo");

      // 3) A short press: track 3 before track 2's end (150 ms)
      cnt = ierrors;
      press_key1(15);
      wait_track(4'd3, 30);
      if (ierrors == cnt)  $display("  PASS: a short KEY[1] press skips to track 3");

      // 4) Track 3: 11025 Hz mono 8-bit, 2000 Hz at 100 / 128 of full scale
      wait_ms(6);
      cnt = ierrors;
      measure(2000);
      check_tone("3 L  ", wxl, wl0, wl1, wpl, 2000, 22000, 25700);
      if (wdiff != 0) begin $display("  FAIL: track 3: %0d frames with different channels", wdiff); ierrors = ierrors + 1; end
      if (ierrors == cnt)  $display("  PASS: track 3, 8-bit");

      // 5) Track 4: 22050 Hz stereo IMA ADPCM, left 750 Hz at 10000, right
      //    2500 Hz at 5000; track 5: 44100 Hz stereo QOA, left 600 Hz at 9000,
      //    right 3000 Hz at 4000
      wait_track(4'd4, 80);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("4 L  ", wxl, wl0, wl1, wpl,  750, 9000, 10600);
      check_tone("4 R  ", wxr, wr0, wr1, wpr, 2500, 4500,  5400);
      if (ierrors == cnt)  $display("  PASS: track 4, IMA ADPCM stereo");
      wait_track(4'd5, 200);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("5 L  ", wxl, wl0, wl1, wpl,  600, 8100,  9600);
      check_tone("5 R  ", wxr, wr0, wr1, wpr, 3000, 3600,  4400);
      if (ierrors == cnt)  $display("  PASS: track 5, QOA stereo");
      // track 6: 44100 Hz stereo FLAC, left 800 Hz at 7000, right 2000 Hz at 11000
      wait_track(4'd6, 200);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("6 L  ", wxl, wl0, wl1, wpl,  800, 6800,  7200);
      check_tone("6 R  ", wxr, wr0, wr1, wpr, 2000, 10700, 11300);
      if (ierrors == cnt)  $display("  PASS: track 6, FLAC stereo");
      // track 7: the module's sine on channel 0, panned left: 3/4 left, 1/4
      // right; 127 x 256 x 64 (volume) x 3 >> 9 = 12192 left, a third right
      wait_track(4'd7, 200);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("7 L  ", wxl, wl0, wl1, wpl, 1036, 11700, 12300);
      check_tone("7 R  ", wxr, wr0, wr1, wpr, 1036, 3900,  4150);
      if (ierrors == cnt)  $display("  PASS: track 7, ProTracker module");
      // track 8: the S3M's 16-bit sine (its upper byte, 127) on channel 0,
      // panned right (13 / 16), master volume 48 (of 64): 127 x 256 x 48 x
      // 13 >> 11 = 9906 right, 3 / 16 left = 2286
      wait_track(4'd8, 200);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("8 L  ", wxl, wl0, wl1, wpl, 1251, 2150,  2420);
      check_tone("8 R  ", wxr, wr0, wr1, wpr, 1251, 9450,  10150);
      if (ierrors == cnt)  $display("  PASS: track 8, Scream Tracker 3 module");
      // track 9: the XM's 16-bit sine (127) at D-5, sample panning 192 (12 of
      // 16), volume 64, envelope 64: 127 x 256 x 64 x 12 >> 11 = 12192
      // right, 4064 left
      wait_track(4'd9, 200);
      wait_ms(8);
      cnt = ierrors;
      measure(1600);
      check_tone("9 L  ", wxl, wl0, wl1, wpl, 1173, 3850,  4300);
      check_tone("9 R  ", wxr, wr0, wr1, wpr, 1173, 11700, 12500);
      if (ierrors == cnt)  $display("  PASS: track 9, FastTracker 2 module");
      // track 10: MP3 (gen_card.py's encoder, 320 kbit/s), left 900 Hz at
      // 0.30, right 2200 Hz at 0.20: 10297 and 6885 decoded at 44.1 kHz;
      // measured past the codec's start (a granule and the decoder's delay,
      // about 25 ms of near silence)
      wait_track(4'd10, 250);
      wait_ms(45);
      cnt = ierrors;
      measure(1600);
      check_tone("10 L ", wxl, wl0, wl1, wpl,  900, 9700,  10800);
      check_tone("10 R ", wxr, wr0, wr1, wpr, 2200, 6400,  7200);
      if (ierrors == cnt)  $display("  PASS: track 10, MP3 stereo");

      // 6) The playlist wraps; a long press: the speaker
      wait_track(4'd1, 200);
      cnt = ierrors;
      press_key1(75);
      wait_ms(4);
      nfr = 0; nloud = 0;
      wait_ms(15);
      if (LED[2:1] !== 2'd1) begin $display("  FAIL: long press: output LED[2:1] = %0d (1, speaker, expected)", LED[2:1]); ierrors = ierrors + 1; end
      if (nfr < 800)         begin $display("  FAIL: %0d speaker frames in 15 ms", nfr); ierrors = ierrors + 1; end
      if (nloud < 100)       begin $display("  FAIL: little sound on the speaker (%0d frames above 2000)", nloud); ierrors = ierrors + 1; end
      if (PA_EN !== 1'b1)    begin $display("  FAIL: PA_EN low on the speaker"); ierrors = ierrors + 1; end
      if (ierrors == cnt)    $display("  PASS: the playlist wraps to track 1; a long KEY[1] press moves it to the speaker");

      $display("");
      if (ierrors == 0)
         $display("|                AUDIO PLAYER TEST PASSED                |");
      else
         $display("|           AUDIO PLAYER TEST FAILED (%0d error(s))       |", ierrors);
      $display("");

      error = error + ierrors;
      stimulus_done = 1;
   end

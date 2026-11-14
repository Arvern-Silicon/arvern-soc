//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    demo_audio
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : demo_audio.v
// Module Description : arvern FPGA test stimulus for the music-box demo on
//                      the onboard speaker or a Pmod I2S2
//                      (software/apps/demo_audio).
//
//   The firmware synthesises four voices into the I2S master, which drives
//   the onboard amplifier or the Pmod I2S2 on PMOD1; the LED register carries
//   the piece number [7:4], an underrun flag [3], the output [2] (1 = Pmod)
//   and a note toggle [0]. This stimulus decodes the amplifier pins and the
//   Pmod's D/A pins (I2S, 32-bit slots, 16-bit samples MSB first) and:
//     1) checks the stream on the output from reset (the panel build's
//        Pmod, the board I/O AUDIO_OUT default, shown on LED[2]): frames keep
//        coming, both channels identical, sound present and within the mix
//        limit, the other output silent; the Pmod with MCLK running and
//        PA_EN low, the speaker with PA_EN high;
//     2) checks the first piece plays notes (LED[0] toggles) without an
//        underrun (LED[3] low);
//     3) presses KEY[1] briefly and checks the next piece starts, the stream
//        going on;
//     4) holds KEY[1] and checks the stream moves to the other output, the
//        piece unchanged;
//     5) holds KEY[1] again and checks the stream is back on the first one.
// vscreen: no_input   (the stimulus drives KEY)
//----------------------------------------------------------------------------

integer    ierrors;
integer    nfr;                          // frames decoded on the amplifier pins
integer    c;                            // BCLK rising edges since the last LRCK change
integer    nlr_diff, nloud, peak;
integer    toggles, cnt;
reg        lr_q, led0_q, pa_low;
reg [15:0] sl, sr;
integer    pfr, pc, plr_diff, ploud, ppeak, pmclk;  // the same on the Pmod's D/A pins
reg        plr_q, pmclk_q;
reg [15:0] psl, psr;
integer    nfr_mark, pfr_mark, nloud_mark, ploud_mark, pmclk_mark;
integer    lr_mark;
reg        out0;                         // the output from reset: 1 Pmod

wire [3:0] piece = LED[7:4];

// I2S decoder: the MSB of a slot is sampled on the second rising BCLK edge
// after the LRCK change (one-bit delay)
always @(posedge I2S_BCLK)
  begin
     if (I2S_LRCK !== lr_q) c = 0;
     else                   c = c + 1;
     if ((c >= 1) && (c <= 16))
       begin
          if (I2S_LRCK) sr = {sr[14:0], I2S_DOUT};
          else          sl = {sl[14:0], I2S_DOUT};
       end
     if (I2S_LRCK && (c == 16))
       begin
          nfr = nfr + 1;
          if (sl !== sr) nlr_diff = nlr_diff + 1;
          if ($signed(sl) > 2000 || $signed(sl) < -2000) nloud = nloud + 1;
          if ($signed(sl) > peak)  peak =  $signed(sl);
          if (-$signed(sl) > peak) peak = -$signed(sl);
       end
     if (PA_EN !== 1'b1) pa_low = 1'b1;
     lr_q = I2S_LRCK;
  end

// Pmod I2S2 D/A: MCLK PMOD1[0], LRCK PMOD1[2], SCLK PMOD1[4], SDIN PMOD1[6]
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
          if (psl !== psr) plr_diff = plr_diff + 1;
          if ($signed(psl) > 2000 || $signed(psl) < -2000) ploud = ploud + 1;
          if ($signed(psl) > ppeak)  ppeak =  $signed(psl);
          if (-$signed(psl) > ppeak) ppeak = -$signed(psl);
       end
     plr_q = PMOD1[2];
  end

always @(posedge dut.clk_sys)
  begin
     if (LED[0] !== led0_q) toggles = toggles + 1;
     led0_q = LED[0];
     if (PMOD1[0] !== pmclk_q) pmclk = pmclk + 1;
     pmclk_q = PMOD1[0];
  end

// Simulated milliseconds (clk_sys at 33.333 MHz)
task wait_ms;
   input integer n;
   begin
      repeat (n * 33333) @(posedge dut.clk_sys);
   end
endtask

task mark;
   begin
      nfr_mark = nfr; pfr_mark = pfr; nloud_mark = nloud; ploud_mark = ploud; pmclk_mark = pmclk;
      lr_mark  = nlr_diff + plr_diff; pa_low = 1'b0;
   end
endtask

// The stream on one output since the last mark: at least min_fr frames and
// min_loud loud ones, both channels equal, the other output silent, MCLK and
// PA_EN as that output needs, LED[2] showing it, no underrun
task check_output;
   input        pm;
   input integer min_fr, min_loud;
   integer      fr, loud, fr_other;
   begin
      fr       = pm ? pfr - pfr_mark     : nfr - nfr_mark;
      loud     = pm ? ploud - ploud_mark : nloud - nloud_mark;
      fr_other = pm ? nfr - nfr_mark     : pfr - pfr_mark;
      if (LED[2] !== pm)                  begin $display("  FAIL: output LED[2] = %b (%0s expected)", LED[2], pm ? "Pmod" : "speaker"); ierrors = ierrors + 1; end
      if (fr < min_fr)                    begin $display("  FAIL: %0d frames on the %0s", fr, pm ? "Pmod" : "speaker"); ierrors = ierrors + 1; end
      if (loud < min_loud)                begin $display("  FAIL: little sound on the %0s (%0d frames above 2000)", pm ? "Pmod" : "speaker", loud); ierrors = ierrors + 1; end
      if (nlr_diff + plr_diff != lr_mark) begin $display("  FAIL: frames with different channels"); ierrors = ierrors + 1; end
      if (fr_other != 0)                  begin $display("  FAIL: %0d frames on the %0s while the %0s plays", fr_other, pm ? "speaker" : "Pmod", pm ? "Pmod" : "speaker"); ierrors = ierrors + 1; end
      if (pm  && (pmclk - pmclk_mark < 1000)) begin $display("  FAIL: Pmod MCLK not running"); ierrors = ierrors + 1; end
      if (!pm && (pmclk != pmclk_mark))   begin $display("  FAIL: Pmod MCLK running while the speaker plays"); ierrors = ierrors + 1; end
      if (PA_EN !== !pm)                  begin $display("  FAIL: PA_EN = %b", PA_EN); ierrors = ierrors + 1; end
      if (!pm && pa_low)                  begin $display("  FAIL: PA_EN low while the speaker plays"); ierrors = ierrors + 1; end
      if (LED[3] !== 1'b0)                begin $display("  FAIL: transmit underrun"); ierrors = ierrors + 1; end
   end
endtask

// KEY[1] pressed for n ms (the firmware polls it once a sixteenth; a press of
// 3/4 s / 16 in simulation, 47 ms, switches the output)
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
      ierrors = 0; nfr = 0; c = 0; nlr_diff = 0; nloud = 0; peak = 0; toggles = 0;
      lr_q = 1'b1; led0_q = 1'b0; pa_low = 1'b0; sl = 16'h0; sr = 16'h0;
      pfr = 0; pc = 0; plr_diff = 0; ploud = 0; ppeak = 0; pmclk = 0;
      plr_q = 1'b1; pmclk_q = 1'b1; psl = 16'h0; psr = 16'h0;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|  arvern FPGA AUDIO DEMO TEST (music box, speaker and Pmod I2S2)    |");
      $display(" ====================================================================");
      $display("");

      force dut.ahb_bus_system_inst.apb_board_io_inst.sync_deb_key1.debounce_threshold = 19'd15;

      // 1-2) The first piece, on the output from reset
      mark;
      wait ((nfr > 0) || (pfr > 0));
      out0 = (pfr > 0);
      toggles = 0;
      wait_ms(130);
      check_output(out0, 8000, 1000);
      if ((out0 ? ppeak : peak) > 28000) begin $display("  FAIL: peak %0d beyond the mix limit", out0 ? ppeak : peak); ierrors = ierrors + 1; end
      if (piece !== 4'd0)      begin $display("  FAIL: piece %0d (expected 0)", piece); ierrors = ierrors + 1; end
      if (toggles < 2)         begin $display("  FAIL: %0d melody notes", toggles); ierrors = ierrors + 1; end
      if (ierrors == 0)
        $display("  PASS: piece 0 on the %0s: %0d frames, peak %0d, %0d notes, both channels equal, no underrun",
                 out0 ? "Pmod" : "speaker", out0 ? pfr : nfr, out0 ? ppeak : peak, toggles);

      // 3) A short press: the next piece, started on release
      press_key1(20);
      mark;
      wait_ms(30);
      if (piece !== 4'd1)          begin $display("  FAIL: KEY[1]: piece %0d (expected 1)", piece); ierrors = ierrors + 1; end
      else if ((out0 ? pfr - pfr_mark : nfr - nfr_mark) < 1500) begin $display("  FAIL: stream stalled after KEY[1]"); ierrors = ierrors + 1; end
      else if (LED[3] !== 1'b0)    begin $display("  FAIL: transmit underrun after KEY[1]"); ierrors = ierrors + 1; end
      else                         $display("  PASS: a short KEY[1] press starts piece 1, the stream going on");

      // 4) A long press: the other output
      cnt = ierrors;
      press_key1(75);
      wait_ms(5);
      mark;
      wait_ms(30);
      check_output(!out0, 1500, 100);
      if (piece !== 4'd1)      begin $display("  FAIL: the long press changed the piece (%0d)", piece); ierrors = ierrors + 1; end
      if (ierrors == cnt)
        $display("  PASS: a long KEY[1] press moves piece 1 to the %0s", out0 ? "speaker" : "Pmod");

      // 5) Another long press: back to the first output
      cnt = ierrors;
      press_key1(75);
      wait_ms(5);
      mark;
      wait_ms(30);
      check_output(out0, 1500, 100);
      if (ierrors == cnt)
        $display("  PASS: a second long press brings the stream back to the %0s", out0 ? "Pmod" : "speaker");

      $display("");
      if (ierrors == 0)
         $display("|                 AUDIO DEMO TEST PASSED                 |");
      else
         $display("|            AUDIO DEMO TEST FAILED (%0d error(s))        |", ierrors);
      $display("");

      error = error + ierrors;
      stimulus_done = 1;
   end

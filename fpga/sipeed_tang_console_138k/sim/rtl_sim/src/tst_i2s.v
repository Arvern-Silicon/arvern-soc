//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tst_i2s
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tst_i2s.v
// Module Description : arvern FPGA test stimulus for the I2S master and the
//                      sound outputs (software/apps/tst_i2s).
//
//   The firmware paces the test with the LED register (0x11 speaker, 0x12
//   Pmod I2S2, 0x13 HDMI, 0x14 none, 0xA5 pass, 0xE0 | step fail), each step
//   selecting the board I/O AUDIO_OUT. This stimulus:
//     1) decodes the onboard amplifier pins (I2S, 32-bit slots, 16-bit
//        samples MSB first) and checks the firmware's words arrive there in
//        order with PA_EN high, while no PMOD1 pin is driven;
//     2) loops the Pmod I2S2 D/A SDIN (PMOD1 IO6, GPIO 14) back to its A/D
//        SDOUT (IO7, GPIO 15): the firmware compares what it receives; checks
//        the amplifier pins idle (BCLK low, LRCK high, data low, PA_EN low)
//        and MCLK, LRCK and SCLK running on both Pmod rows;
//     3) decodes the HDMI audio input (dut.i2s_hdmi_*) and checks the words
//        arrive there in order, the amplifier and the Pmod idle;
//     4) checks every output idle while the stream plays to none;
//     5) checks the firmware's verdict.
// vscreen: no_input
//----------------------------------------------------------------------------

localparam integer NWORDS = 12;

integer    ierrors, cnt;
integer    nfr;                          // frames decoded on the amplifier pins
integer    c;                            // BCLK rising edges since the last LRCK change
integer    k, first;
reg        lr_q;
reg [15:0] sl, sr;
reg [31:0] fr [0:255];
integer    amp_bclk_tg, pmod_mclk_tg, pmod_mclk2_tg, pmod_sclk_tg, pmod_lrck_tg, hdmi_bclk_tg;
reg        amp_bclk_q, pmod_mclk_q, pmod_mclk2_q, pmod_sclk_q, pmod_lrck_q, hdmi_bclk_q;
integer    hfr, hc;                      // frames decoded on the HDMI input, its BCLK count
reg        hlr_q;
reg [15:0] hsl, hsr;
reg [31:0] hframe [0:255];
reg        pmod_driven, pa_low_in_speaker;

// External loopback: D/A SDIN -> A/D SDOUT
assign PMOD1[7] = PMOD1[6];

function [31:0] pattern;
   input integer i;
   pattern = {16'h8765 - 16'h0203 * i[15:0], 16'h1234 + 16'h0101 * i[15:0]};
endfunction

// I2S decoder on the amplifier pins: the MSB of a slot is sampled on the
// second rising BCLK edge after the LRCK change (one-bit delay)
always @(posedge I2S_BCLK)
  begin
     if (I2S_LRCK !== lr_q) c = 0;
     else                   c = c + 1;
     if ((c >= 1) && (c <= 16))
       begin
          if (I2S_LRCK) sr = {sr[14:0], I2S_DOUT};
          else          sl = {sl[14:0], I2S_DOUT};
       end
     if (I2S_LRCK && (c == 16) && (nfr < 256))
       begin
          fr[nfr] = {sr, sl};
          nfr     = nfr + 1;
       end
     lr_q = I2S_LRCK;
  end

// The same decoder on the HDMI audio input
always @(posedge dut.i2s_hdmi_bclk)
  begin
     if (dut.i2s_hdmi_lrck !== hlr_q) hc = 0;
     else                             hc = hc + 1;
     if ((hc >= 1) && (hc <= 16))
       begin
          if (dut.i2s_hdmi_lrck) hsr = {hsr[14:0], dut.i2s_hdmi_sd};
          else                   hsl = {hsl[14:0], dut.i2s_hdmi_sd};
       end
     if (dut.i2s_hdmi_lrck && (hc == 16) && (hfr < 256))
       begin
          hframe[hfr] = {hsr, hsl};
          hfr         = hfr + 1;
       end
     hlr_q = dut.i2s_hdmi_lrck;
  end

// Pin activity per step; the per-cycle checks while AUDIO_OUT holds the
// step's output (the firmware switches it next to the LED value)
wire [1:0] aout = dut.ahb_bus_system_inst.audio_out;
always @(posedge dut.clk_sys)
  begin
     if (I2S_BCLK  !== amp_bclk_q)   amp_bclk_tg   = amp_bclk_tg   + 1;
     if (PMOD1[0]  !== pmod_mclk_q)  pmod_mclk_tg  = pmod_mclk_tg  + 1;
     if (PMOD1[1]  !== pmod_mclk2_q) pmod_mclk2_tg = pmod_mclk2_tg + 1;
     if (PMOD1[4]  !== pmod_sclk_q)  pmod_sclk_tg  = pmod_sclk_tg  + 1;
     if (PMOD1[2]  !== pmod_lrck_q)  pmod_lrck_tg  = pmod_lrck_tg  + 1;
     if (dut.i2s_hdmi_bclk !== hdmi_bclk_q) hdmi_bclk_tg = hdmi_bclk_tg + 1;
     hdmi_bclk_q  = dut.i2s_hdmi_bclk;
     amp_bclk_q   = I2S_BCLK;
     pmod_mclk_q  = PMOD1[0];
     pmod_mclk2_q = PMOD1[1];
     pmod_sclk_q  = PMOD1[4];
     pmod_lrck_q  = PMOD1[2];
     if ((LED === 8'h11) && (aout == 2'd1) && (dut.gpio_pmod_oe[15:8] !== 8'h00)) pmod_driven = 1'b1;
     if ((LED === 8'h11) && (aout == 2'd1) && (I2S_BCLK === 1'b1) && (PA_EN !== 1'b1)) pa_low_in_speaker = 1'b1;
     if ((LED === 8'h12) && (aout == 2'd2) && ((I2S_BCLK !== 1'b0) || (I2S_LRCK !== 1'b1) || (I2S_DOUT !== 1'b0) || (PA_EN !== 1'b0)))
       begin
          $display("  FAIL: amplifier pins not idle while the Pmod owns the stream %t", $time);
          ierrors = ierrors + 1;
       end
  end

task wait_led;
   input [7:0] v;
   begin
      wait ((LED === v) || (LED[7:4] === 4'hE));
      @(negedge dut.clk_sys);
   end
endtask

task clear_counts;
   begin
      amp_bclk_tg = 0; pmod_mclk_tg = 0; pmod_mclk2_tg = 0; pmod_sclk_tg = 0; pmod_lrck_tg = 0; hdmi_bclk_tg = 0;
   end
endtask

initial
   begin
      ierrors = 0; nfr = 0; c = 0; lr_q = 1'b1; sl = 16'h0; sr = 16'h0;
      hfr = 0; hc = 0; hlr_q = 1'b1; hsl = 16'h0; hsr = 16'h0; hdmi_bclk_q = 1'b0;
      pmod_driven = 1'b0; pa_low_in_speaker = 1'b0;
      amp_bclk_q = 1'b0; pmod_mclk_q = 1'b1; pmod_mclk2_q = 1'b1; pmod_sclk_q = 1'b1; pmod_lrck_q = 1'b1;
      clear_counts;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|   arvern FPGA I2S TEST (apb_i2s: speaker, Pmod I2S2, HDMI input)   |");
      $display(" ====================================================================");
      $display("");

      // 1) Speaker
      wait_led(8'h11);
      clear_counts;
      wait_led(8'h12);
      first = -1;
      for (k = 0; (k < nfr) && (first < 0); k = k + 1)
        if (fr[k] !== 32'h0) first = k;
      if (first < 0)
        begin $display("  FAIL: no frame on the amplifier pins (%0d decoded)", nfr); ierrors = ierrors + 1; end
      else
        begin
           for (k = 0; k < NWORDS; k = k + 1)
             if ((first + k >= nfr) || (fr[first + k] !== pattern(k)))
               begin
                  $display("  FAIL: amplifier frame %0d: 0x%08h, expected 0x%08h", k, fr[first + k], pattern(k));
                  ierrors = ierrors + 1;
                  k = NWORDS;
               end
           if (ierrors == 0) $display("  PASS: %0d words on the amplifier pins, in order (I2S)", NWORDS);
        end
      if (pmod_driven)       begin $display("  FAIL: a PMOD1 pin driven while the speaker owns the stream"); ierrors = ierrors + 1; end
      if (hdmi_bclk_tg != 0) begin $display("  FAIL: the HDMI input's BCLK ran while the speaker owns the stream"); ierrors = ierrors + 1; end
      if (pa_low_in_speaker) begin $display("  FAIL: BCLK running with PA_EN low"); ierrors = ierrors + 1; end

      // 2) Pmod I2S2 (the firmware checks the loopback data)
      clear_counts;
      wait_led(8'h13);
      if (amp_bclk_tg != 0) begin $display("  FAIL: amplifier BCLK toggled %0d times while the Pmod owns the stream", amp_bclk_tg); ierrors = ierrors + 1; end
      if (hdmi_bclk_tg != 0) begin $display("  FAIL: the HDMI input's BCLK ran while the Pmod owns the stream"); ierrors = ierrors + 1; end
      if ((pmod_mclk_tg < 100) || (pmod_mclk2_tg != pmod_mclk_tg) || (pmod_sclk_tg < 20) || (pmod_lrck_tg < 2))
        begin
           $display("  FAIL: Pmod clocks: MCLK %0d / %0d, SCLK %0d, LRCK %0d toggles", pmod_mclk_tg, pmod_mclk2_tg, pmod_sclk_tg, pmod_lrck_tg);
           ierrors = ierrors + 1;
        end
      else
        $display("  PASS: Pmod MCLK (both rows), SCLK and LRCK running, amplifier idle");

      // 3) HDMI audio input
      clear_counts;
      cnt = ierrors;
      wait_led(8'h14);
      first = -1;
      for (k = 0; (k < hfr) && (first < 0); k = k + 1)
        if (hframe[k] !== 32'h0) first = k;
      if (first < 0)
        begin $display("  FAIL: no frame on the HDMI input (%0d decoded)", hfr); ierrors = ierrors + 1; end
      else
        for (k = 0; k < NWORDS; k = k + 1)
          if ((first + k >= hfr) || (hframe[first + k] !== pattern(k)))
            begin
               $display("  FAIL: HDMI frame %0d: 0x%08h, expected 0x%08h", k, hframe[first + k], pattern(k));
               ierrors = ierrors + 1;
               k = NWORDS;
            end
      if ((amp_bclk_tg != 0) || (pmod_sclk_tg != 0) || (pmod_mclk_tg != 0) || (PA_EN !== 1'b0))
        begin
           $display("  FAIL: speaker (%0d) or Pmod (SCLK %0d, MCLK %0d) active, PA_EN %b, while HDMI owns the stream",
                    amp_bclk_tg, pmod_sclk_tg, pmod_mclk_tg, PA_EN);
           ierrors = ierrors + 1;
        end
      if (ierrors == cnt) $display("  PASS: %0d words on the HDMI audio input, in order; speaker and Pmod idle", NWORDS);

      // 4) None
      clear_counts;
      cnt = ierrors;
      wait_led(8'hA5);
      if ((amp_bclk_tg != 0) || (pmod_sclk_tg != 0) || (pmod_mclk_tg != 0) || (hdmi_bclk_tg != 0))
        begin
           $display("  FAIL: an output active with AUDIO_OUT none (speaker %0d, Pmod %0d, HDMI %0d)", amp_bclk_tg, pmod_sclk_tg, hdmi_bclk_tg);
           ierrors = ierrors + 1;
        end
      if (ierrors == cnt) $display("  PASS: every output idle with AUDIO_OUT none");

      // 5) Verdict
      repeat (50) @(posedge SYS_CLK);
      if (LED !== 8'hA5)
        begin
           $display("  FAIL: firmware reported failure at step %0d (LED=0x%02h)", LED[3:0], LED);
           ierrors = ierrors + 1;
        end
      else
        $display("  PASS: firmware checks (TX_LOW on PLIC source 17, PA_EN per output, Pmod loopback data)");

      $display("");
      if (ierrors == 0)
         $display("|                    I2S TEST PASSED                     |");
      else
         $display("|               I2S TEST FAILED (%0d error(s))            |", ierrors);
      $display("");

      error = error + ierrors;
      stimulus_done = 1;
   end

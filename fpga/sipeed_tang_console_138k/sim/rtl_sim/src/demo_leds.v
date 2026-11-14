//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    demo_leds
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : demo_leds.v
// Module Description : arvern FPGA test stimulus for the two-LED light show.
//
//   The firmware writes the show number to LED register bits [7:4] and drives
//   the two SoM LEDs (LED_PIN) with sigma-delta brightness. This stimulus:
//     1) checks show 0 (BREATHE): both LEDs are lit, toggle, and stay
//        identical (same brightness, same modulator phase);
//     2) checks the shows advance on their own, in order;
//     3) presses KEY[1] and checks the show advances at once;
//     4) follows the loop until it wraps back to show 0, checking that every
//        show drives the LEDs.
// vscreen: no_input   (the stimulus drives KEY)
//----------------------------------------------------------------------------

localparam integer NUM_SHOWS = 11;

integer errors;
integer cyc;
integer k;
integer toggles0, toggles1;
reg     differ;
reg     [1:0] pin_q;
reg     [3:0] show_q;
reg     [NUM_SHOWS-1:0] seen;            // shows whose LEDs toggled

wire    [3:0] show_nr = LED[7:4];

// LED activity per show
always @(posedge SYS_CLK) begin
   if (LED_PIN[0] !== pin_q[0]) toggles0 = toggles0 + 1;
   if (LED_PIN[1] !== pin_q[1]) toggles1 = toggles1 + 1;
   if ((LED_PIN !== pin_q) && (show_nr < NUM_SHOWS)) seen[show_nr] = 1'b1;
   pin_q = LED_PIN;
end

// Press KEY[1] (active low): held until the show changes (debouncer + the
// next animation step), at most 20000 cycles, then released
task press_key1;
   reg [3:0] from;
   begin
      from   = show_nr;
      KEY[1] = 1'b0;
      for (cyc = 0; (cyc < 20000) && (show_nr === from); cyc = cyc + 1)
         @(posedge SYS_CLK);
      KEY[1] = 1'b1;
      repeat (2000) @(posedge SYS_CLK);
   end
endtask

// Wait for the next show change (bounded); returns the new show
task wait_show_change(output [3:0] nr);
   reg [3:0] from;
   begin
      from = show_nr;
      for (cyc = 0; (cyc < 4000000) && (show_nr === from); cyc = cyc + 1)
         @(posedge SYS_CLK);
      nr = show_nr;
      if (show_nr === from) begin
         $display("  FAIL: show %0d never ended", from);
         errors = errors + 1;
      end
   end
endtask

initial
   begin
      errors   = 0;
      toggles0 = 0;
      toggles1 = 0;
      seen     = {NUM_SHOWS{1'b0}};
      pin_q    = 2'b00;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|            arvern FPGA TWO-LED LIGHT SHOW TEST                      |");
      $display(" ====================================================================");
      $display("");

      force dut.ahb_bus_system_inst.apb_board_io_inst.sync_deb_key1.debounce_threshold = 19'd15;

      //----------------------------------------------------------------
      // 1) Show 0 (BREATHE): both LEDs toggle, and identically
      //----------------------------------------------------------------
      wait (show_nr === 4'd0);
      repeat (2000) @(posedge SYS_CLK);              // the first animation steps
      toggles0 = 0; toggles1 = 0; differ = 1'b0;
      for (k = 0; k < 20000; k = k + 1) begin
         @(posedge SYS_CLK);
         if (LED_PIN[0] !== LED_PIN[1]) differ = 1'b1;
      end
      if ((toggles0 > 10) && (toggles1 > 10) && !differ)
         $display("  PASS: BREATHE drives both LEDs in step (%0d / %0d toggles)", toggles0, toggles1);
      else begin
         $display("  FAIL: BREATHE: %0d / %0d toggles, LEDs %s", toggles0, toggles1, differ ? "differ" : "match");
         errors = errors + 1;
      end

      //----------------------------------------------------------------
      // 2) The shows advance on their own
      //----------------------------------------------------------------
      wait_show_change(show_q);
      if (show_q === 4'd1) $display("  PASS: show 0 -> 1 on its own");
      else begin $display("  FAIL: show 0 -> %0d (expected 1)", show_q); errors = errors + 1; end

      //----------------------------------------------------------------
      // 3) KEY[1] skips to the next show at once
      //----------------------------------------------------------------
      repeat (3000) @(posedge SYS_CLK);
      press_key1;
      if (show_nr === 4'd2) $display("  PASS: KEY[1] skips show 1 -> 2");
      else begin $display("  FAIL: KEY[1]: show %0d (expected 2)", show_nr); errors = errors + 1; end

      //----------------------------------------------------------------
      // 4) Every show in order, until the loop wraps to show 0
      //----------------------------------------------------------------
      seen = {NUM_SHOWS{1'b0}};
      for (k = 3; k <= NUM_SHOWS; k = k + 1) begin
         wait_show_change(show_q);
         if (show_q !== (k % NUM_SHOWS)) begin
            $display("  FAIL: show %0d followed by %0d", k - 1, show_q);
            errors = errors + 1;
         end
      end
      for (k = 2; k < NUM_SHOWS; k = k + 1)
         if (!seen[k]) begin
            $display("  FAIL: show %0d left the LEDs still", k);
            errors = errors + 1;
         end
      if (errors == 0) $display("  PASS: shows 2-10 played in order, each driving the LEDs, loop wrapped to 0");

      $display("");
      if (errors == 0)
         $display("|                  LED DEMO TEST PASSED                  |");
      else
         $display("|             LED DEMO TEST FAILED (%0d error(s))         |", errors);
      $display("");

      error = error + errors;
      stimulus_done = 1;
   end

// Log every show change
always @(show_nr)
   if (show_nr !== show_q)
      $display("  [%0t] show %0d", $time, show_nr);

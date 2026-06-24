//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    leds
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : leds.v
// Module Description : arvern FPGA test stimulus for the IRQ-driven LED demo.
//
//   The firmware is fully interrupt-driven:
//     - timer IRQ  : animation clock (advances the current mode's frame)
//     - SW  IRQ    : SW[0]=direction, SW[2:1]=speed
//     - KEY IRQ    : KEY[1] press cycles the display mode
//   Modes: 0=counter, 1=scanner(one-hot), 2=bar, 3=sparkle.
//
//   This stimulus shortens the relevant debouncers (force), then:
//     1) checks the counter direction follows SW[0]   (timer + SW IRQ)
//     2) presses KEY[1] and checks the mode advances to the one-hot scanner
//        (KEY IRQ)
//     3) cycles through the remaining modes and confirms the LEDs keep
//        animating (no hang / no IRQ storm)
//----------------------------------------------------------------------------

integer dir;
integer errors;
integer k;
reg     onehot_ok;
reg     go_seen;
reg [7:0] led_a;

// Sample the LED counting direction from two consecutive LED changes.
task sample_dir(output integer d);
   reg [7:0] a, b;
   begin
      @(LED); a = LED;
      @(LED); b = LED;
      case ((b - a) & 8'hFF)
         8'h01:   d =  1;
         8'hFF:   d = -1;
         default: d =  0;
      endcase
   end
endtask

// Press KEY[1] (active-low): falling edge = press -> one mode advance.
task press_key1;
   begin
      KEY[1] = 1'b0;                            // press
      repeat (60) @(posedge FPGA_CLK1_50);      // debounce + trap + settle
      KEY[1] = 1'b1;                            // release
      repeat (60) @(posedge FPGA_CLK1_50);
   end
endtask

initial
   begin
      errors = 0;
      @(posedge FPGA_CLK1_50);
      @(posedge dut.reset_n);

      $display("");
      $display(" ====================================================================");
      $display("|              arvern FPGA LED DEMO TEST (timer + SW + KEY IRQs)      |");
      $display(" ====================================================================");
      $display("");

      // Shorten every debouncer this test drives (real threshold ~10ms).
      force dut.ahb_bus_system_inst.ahb_led_key_sw_inst.sync_deb_sw0.debounce_threshold  = 19'd15;
      force dut.ahb_bus_system_inst.ahb_led_key_sw_inst.sync_deb_key1.debounce_threshold = 19'd15;

      //----------------------------------------------------------------
      // Mode 0 (counter): direction follows SW[0]   [timer + SW IRQ]
      //----------------------------------------------------------------
      repeat (16) @(LED);
      sample_dir(dir);
      if (dir == 1) $display("  PASS: counter direction is UP");
      else begin $display("  FAIL: counter expected UP, got %0d", dir); errors = errors + 1; end

      $display("  -- driving SW[0] = 1  (expect DOWN)");
      SW[0] = 1'b1;
      repeat (16) @(LED);
      sample_dir(dir);
      if (dir == -1) $display("  PASS: SW[0]=1 -> counter DOWN (SW IRQ)");
      else begin $display("  FAIL: SW[0]=1 expected DOWN, got %0d", dir); errors = errors + 1; end

      $display("  -- driving SW[0] = 0  (expect UP)");
      SW[0] = 1'b0;
      repeat (16) @(LED);
      sample_dir(dir);
      if (dir == 1) $display("  PASS: SW[0]=0 -> counter UP (SW IRQ)");
      else begin $display("  FAIL: SW[0]=0 expected UP, got %0d", dir); errors = errors + 1; end

      //----------------------------------------------------------------
      // KEY[1] press -> mode 1 (scanner): LED becomes one-hot  [KEY IRQ]
      //----------------------------------------------------------------
      $display("  -- pressing KEY[1]  (expect mode -> scanner, one-hot LED)");
      press_key1;
      onehot_ok = 1'b1;
      for (k = 0; k < 8; k = k + 1) begin
         @(LED);
         if (!(LED !== 8'h00 && ((LED & (LED - 8'h01)) === 8'h00))) onehot_ok = 1'b0;
      end
      if (onehot_ok) $display("  PASS: scanner mode active (LED one-hot, KEY IRQ)");
      else begin $display("  FAIL: scanner mode LED not one-hot"); errors = errors + 1; end

      //----------------------------------------------------------------
      // Cycle through the remaining modes; confirm LEDs keep animating
      //----------------------------------------------------------------
      for (k = 0; k < 3; k = k + 1) begin
         press_key1;                            // scanner->bar->sparkle->breathe
         @(LED); led_a = LED;
         repeat (3) @(LED);                      // expect further changes
         if (LED !== led_a)
            $display("  PASS: mode cycle %0d -> LEDs still animating", k + 1);
         else begin $display("  FAIL: mode cycle %0d -> LEDs stalled", k + 1); errors = errors + 1; end
      end

      //----------------------------------------------------------------
      // Mode 5 (reaction game): enter, wait for GO, react, auto-return
      //----------------------------------------------------------------
      $display("  -- pressing KEY[1] -> reaction game");
      press_key1;                               // breathe -> game (starts WAIT)

      // Wait (bounded) for the GO flash: all LEDs on.
      go_seen = 1'b0;
      for (k = 0; k < 8000 && !go_seen; k = k + 1) begin
         @(posedge FPGA_CLK1_50);
         if (LED === 8'hFF) go_seen = 1'b1;
      end
      if (go_seen) $display("  PASS: game GO flash (all LEDs on)");
      else begin $display("  FAIL: game GO flash not seen"); errors = errors + 1; end

      // React with KEY[1], then let the score hold expire and counter resume.
      $display("  -- reacting (KEY[1])");
      press_key1;
      repeat (4000) @(posedge FPGA_CLK1_50);    // score hold + return to counter
      sample_dir(dir);
      if (dir == 1 || dir == -1)
         $display("  PASS: game auto-returned to counter (LED stepping)");
      else begin $display("  FAIL: game did not return to counter (dir=%0d)", dir); errors = errors + 1; end

      //----------------------------------------------------------------
      // Re-enter the game and test a FALSE START (press during WAIT)
      //----------------------------------------------------------------
      $display("  -- re-entering game, then false-starting (press before GO)");
      repeat (5) press_key1;                    // counter -> ... -> game (WAIT)
      press_key1;                               // press during WAIT = false start
      repeat (4000) @(posedge FPGA_CLK1_50);    // error hold + return to counter
      sample_dir(dir);
      if (dir == 1 || dir == -1)
         $display("  PASS: false start handled, returned to counter");
      else begin $display("  FAIL: false start did not return to counter (dir=%0d)", dir); errors = errors + 1; end

      $display("");
      if (errors == 0)
         $display("|                  LED DEMO TEST PASSED                  |");
      else
         $display("|             LED DEMO TEST FAILED (%0d error(s))         |", errors);
      $display("");

      stimulus_done = 1;
   end

// Print every LED value as the firmware animates, so the patterns are visible.
reg [7:0] prev_led;
initial prev_led = 8'hxx;
always @(LED)
   if (LED !== prev_led)
      begin
         $display("  [%0t] LED = 0x%02h (%08b)", $time, LED, LED);
         prev_led = LED;
      end

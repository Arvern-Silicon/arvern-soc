//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tst_plic_gpio
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tst_plic_gpio.v
// Module Description : arvern FPGA test stimulus for the PLIC and the GPIO
//                      (firmware: software/apps/tst_plic_gpio).
//
//   The firmware shows 0x10 | phase on the LEDs while it waits for an input,
//   and {count[3:0], pin[3:0]} on GPIO pins 15:8 (GPIO_1[15:8]) after every
//   GPIO interrupt. The stimulus drives GPIO_1[7:0] and KEY[1], logs every
//   change of GPIO_1[15:8], and checks:
//     1) one rising edge on each of pins 0..7: eight interrupts, in pin order
//     2) pin 2 (priority 2) then pin 5 (priority 6) pending together: pin 5
//        serviced first
//     3) pin 1 under a threshold above its priority: no interrupt until the
//        firmware lowers the threshold (LEDs 0x13 -> 0x14), then exactly one
//     5) KEY[1] press through PLIC source 27
//     6) the Arduino GPIO drives each ARDUINO_IO pin high once (its own edge
//        interrupts through PLIC source 32+n); all low again at the end
//   and the firmware verdict on the LEDs (0xA5 pass, 0xE0 | step failure).
// vscreen: no_input   (the stimulus drives the GPIO pins and KEY)
//----------------------------------------------------------------------------

integer      perrors;
reg   [15:0] ard_seen;      // Arduino pins seen driven high

initial ard_seen = 16'h0;
always @(ARDUINO_IO)                          // phase 6 only (LEDs 0x16)
   if (LED === 8'h16)
      ard_seen = ard_seen | (ARDUINO_IO & dut.gpio_ard_oe);
integer      pin;
integer      n0;
reg          abort;
reg          ok;

// Log of the GPIO output byte (GPIO_1[15:8]), one entry per change
reg    [7:0] mirror_log [0:63];
integer      mirror_n;
reg    [7:0] mirror_prev;
reg          mirror_on;

initial
   begin
      mirror_n    = 0;
      mirror_prev = 8'h00;
      mirror_on   = 1'b0;
   end

// Logging starts at the phase-1 handshake (LED 0x11): the set-up writes before it are not interrupts
always @(posedge FPGA_CLK1_50)
   if (~mirror_on && (dut.periph0_led === 8'h11))
      begin
         mirror_on   = 1'b1;
         mirror_prev = GPIO_1[15:8];
      end
   else if (mirror_on && (dut.gpio_oe[15:8] === 8'hFF) && (GPIO_1[15:8] !== mirror_prev))
      begin
         $display("  [%0t] GPIO[15:8] = 0x%02h (count %0d, pin %0d)", $time, GPIO_1[15:8], GPIO_1[15:12], GPIO_1[11:8]);
         if (mirror_n < 64) mirror_log[mirror_n] = GPIO_1[15:8];
         mirror_n    = mirror_n + 1;
         mirror_prev = GPIO_1[15:8];
      end

// Wait for an LED code; stops early on a firmware failure code
task wait_led;
   input  [7:0] code;
   input integer cycles;
   output       found;
   integer      n;
   begin
      found = 1'b0;
      for (n = 0; (n < cycles) && !found && (LED[7:4] !== 4'hE); n = n + 1)
         begin
            @(posedge FPGA_CLK1_50);
            if (LED === code) found = 1'b1;
         end
      if (!found)
         begin
            if (LED[7:4] === 4'hE)
               $display("  FAIL: firmware reported failure at step %0d (LED=0x%02h)", LED[3:0], LED);
            else
               $display("  FAIL: LED code 0x%02h not reached (LED=0x%02h)", code, LED);
            perrors = perrors + 1;
            abort   = 1'b1;
         end
   end
endtask

// Wait until the output byte has changed n times
task wait_mirror;
   input integer n;
   input integer cycles;
   output        found;
   integer       k;
   begin
      found = (mirror_n >= n);
      for (k = 0; (k < cycles) && !found; k = k + 1)
         begin
            @(posedge FPGA_CLK1_50);
            if (mirror_n >= n) found = 1'b1;
         end
   end
endtask

// Rising then falling edge on one input pin, each level held 20 cycles
task pulse_pin;
   input integer p;
   begin
      gpio_tb_val[p] = 1'b1;
      repeat (20) @(posedge FPGA_CLK1_50);
      gpio_tb_val[p] = 1'b0;
      repeat (20) @(posedge FPGA_CLK1_50);
   end
endtask

task check_mirror;
   input integer idx;
   input   [7:0] exp;
   begin
      if (mirror_log[idx] !== exp)
         begin
            $display("  FAIL: GPIO[15:8] change %0d = 0x%02h, expected 0x%02h", idx + 1, mirror_log[idx], exp);
            perrors = perrors + 1;
         end
   end
endtask

initial
   begin
      perrors = 0;
      abort   = 1'b0;
      @(posedge FPGA_CLK1_50);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|           arvern FPGA PLIC / GPIO TEST (GPIO_1 + KEY)             |");
      $display(" ====================================================================");
      $display("");

      // Shorten the KEY[1] debouncer (real threshold ~10ms)
      force dut.ahb_bus_system_inst.apb_led_key_sw_inst.sync_deb_key1.debounce_threshold = 19'd15;

      // Input pins 0..7 driven low by the bench
      gpio_tb_val = 26'h0000000;
      gpio_tb_en  = 26'h00000FF;

      //----------------------------------------------------------------
      // 1) One rising edge per input pin, in pin order
      //----------------------------------------------------------------
      wait_led(8'h11, 200000, ok);
      if (!abort)
         begin
            $display("  -- phase 1: one edge on each of pins 0..7");
            for (pin = 0; (pin < 8) && !abort; pin = pin + 1)
               begin
                  pulse_pin(pin);
                  wait_mirror(pin + 1, 5000, ok);
                  if (!ok)
                     begin
                        $display("  FAIL: no interrupt for pin %0d", pin);
                        perrors = perrors + 1;
                        abort   = 1'b1;
                     end
                  else
                     check_mirror(pin, {pin[3:0] + 4'd1, pin[3:0]});
               end
            if (!abort && (perrors == 0))
               $display("  PASS: eight GPIO interrupts through the PLIC, in pin order");
         end

      //----------------------------------------------------------------
      // 2) Priority: low-priority pin 2 raised first, then pin 5
      //----------------------------------------------------------------
      if (!abort)
         begin
            wait_led(8'h12, 100000, ok);
            if (!abort)
               begin
                  $display("  -- phase 2: pin 2 (priority 2) then pin 5 (priority 6)");
                  n0 = mirror_n;
                  gpio_tb_val[2] = 1'b1;
                  repeat (20) @(posedge FPGA_CLK1_50);
                  gpio_tb_val[5] = 1'b1;
                  wait_mirror(n0 + 2, 10000, ok);
                  gpio_tb_val[2] = 1'b0;
                  gpio_tb_val[5] = 1'b0;
                  if (!ok)
                     begin
                        $display("  FAIL: priority phase took %0d interrupts, expected 2", mirror_n - n0);
                        perrors = perrors + 1;
                        abort   = 1'b1;
                     end
                  else
                     begin
                        n0 = perrors;
                        check_mirror(8, 8'h95);
                        check_mirror(9, 8'hA2);
                        if (perrors == n0)
                           $display("  PASS: higher-priority pin 5 claimed before pin 2");
                     end
               end
         end

      //----------------------------------------------------------------
      // 3) Threshold: pin 1 must not interrupt until the threshold drops
      //----------------------------------------------------------------
      if (!abort)
         begin
            wait_led(8'h13, 100000, ok);
            if (!abort)
               begin
                  $display("  -- phase 3: pin 1 under a threshold above its priority");
                  n0 = mirror_n;
                  pulse_pin(1);
                  wait_led(8'h14, 100000, ok);
                  if (!abort)
                     begin
                        if (mirror_n != n0)
                           begin
                              $display("  FAIL: pin 1 interrupted while masked by the threshold");
                              perrors = perrors + 1;
                           end
                        else
                           $display("  PASS: pin 1 held off by the threshold");
                        wait_mirror(n0 + 1, 10000, ok);
                        if (!ok)
                           begin
                              $display("  FAIL: pin 1 did not interrupt after the threshold was lowered");
                              perrors = perrors + 1;
                              abort   = 1'b1;
                           end
                        else
                           begin
                              n0 = perrors;
                              check_mirror(10, 8'hB1);
                              if (perrors == n0)
                                 $display("  PASS: pin 1 interrupted once the threshold was lowered");
                           end
                     end
               end
         end

      //----------------------------------------------------------------
      // 5) KEY[1] through PLIC source 27
      //----------------------------------------------------------------
      if (!abort)
         begin
            wait_led(8'h15, 100000, ok);
            if (!abort)
               begin
                  $display("  -- phase 5: KEY[1] press through the PLIC");
                  KEY[1] = 1'b0;
                  repeat (60) @(posedge FPGA_CLK1_50);
                  KEY[1] = 1'b1;
               end
         end

      //----------------------------------------------------------------
      // Firmware verdict and final state
      //----------------------------------------------------------------
      if (!abort)
         begin
            wait_led(8'hA5, 100000, ok);
            if (!abort)
               $display("  PASS: firmware checks (claim order, threshold, KEY source, final pending state)");
         end

      repeat (50) @(posedge FPGA_CLK1_50);
      if (mirror_n != 11)
         begin
            $display("  FAIL: %0d GPIO interrupts, expected 11", mirror_n);
            perrors = perrors + 1;
         end
      if ((ard_seen !== 16'hFFFF) || (ARDUINO_IO !== 16'h0000) || (dut.gpio_ard_oe !== 16'hFFFF))
        begin
           $display("  FAIL: Arduino pins seen high 0x%04h (expected 0xffff), now 0x%04h, oe 0x%04h",
                    ard_seen, ARDUINO_IO, dut.gpio_ard_oe);
           perrors = perrors + 1;
        end
      else
        $display("  PASS: Arduino GPIO drove every pin, each through PLIC source 32+n");
      if (dut.gpio_oe !== 26'h000FF00)
         begin
            $display("  FAIL: GPIO output enables 0x%07h, expected 0x000FF00", dut.gpio_oe);
            perrors = perrors + 1;
         end
      if (dut.plic_irq_m_external !== 1'b0 || dut.plic_irq_s_external !== 1'b0)
         begin
            $display("  FAIL: PLIC external interrupt still asserted (M %b, S %b)", dut.plic_irq_m_external, dut.plic_irq_s_external);
            perrors = perrors + 1;
         end

      $display("");
      if (perrors == 0)
         $display("|                 PLIC / GPIO TEST PASSED                 |");
      else
         $display("|            PLIC / GPIO TEST FAILED (%0d error(s))        |", perrors);
      $display("");

      error = error + perrors;
      stimulus_done = 1;
   end

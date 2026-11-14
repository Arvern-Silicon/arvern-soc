//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tst_usb_hid
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tst_usb_hid.v
// Module Description : arvern FPGA test stimulus for the two USB host ports,
//                      apb_usb_host on USB-A J15 and J16
//                      (firmware: software/apps/tst_usb_hid).
//
//   The bench's device models (usb_dev0 on J15, usb_dev1 on J16) play a boot
//   keyboard and a DragonRise-style SNES gamepad, paced by the step the
//   firmware shows on the LEDs:
//
//     0x10  attach the keyboard on J15 and the gamepad on J16
//     0x11  keyboard report: left shift + "a"; gamepad report: right + A
//     0x12  keyboard: all keys released; gamepad: idle
//     0x13  detach the keyboard
//     0xA5  firmware pass (0xE0 | step: a J15 check failed, 0xD0 | step: J16)
//
//   Both ports enumerate in hardware (debounce, two bus resets, SET_ADDRESS,
//   descriptors, SET_CONFIGURATION, SET_PROTOCOL, SET_IDLE): about 0.26 s of
//   bus time, both ports in parallel. The models check the hosts on every
//   transaction; their error counts must stay 0 (the bench adds them to the
//   verdict).
//----------------------------------------------------------------------------

// Reports, byte 0 in bits [7:0]
localparam [63:0] KBD_SHIFT_A = 64'h0000_0000_0004_0002;   // modifiers 0x02 (left shift), key 0x04 ("a")
localparam [63:0] KBD_RELEASE = 64'h0000_0000_0000_0000;
localparam [63:0] PAD_RIGHT_A = 64'h0000_2F80_8000_7FFF;   // X FF (right), byte 5 bit 5 (A)
localparam [63:0] PAD_IDLE    = 64'h0000_0F80_8000_7F7F;   // 7F 7F 00 80 80 0F 00 00

integer     uerrors;
reg         fw_fail;

// Wait for LED = v, or a firmware failure code (fw_fail set)
task wait_led;
   input [7:0] v;
   begin
      wait ((LED === v) || (LED[7:4] === 4'hE) || (LED[7:4] === 4'hD));
      fw_fail = (LED !== v);
      @(negedge dut.clk_sys);
   end
endtask

initial
   begin : usb_test
      uerrors = 0;
      fw_fail = 1'b0;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|     arvern FPGA USB HOST TEST (2 x apb_usb_host, PLIC 29 and 30)   |");
      $display(" ====================================================================");
      $display("");

      // Step 0x10: both ports enabled; attach the devices
      wait_led(8'h10);
      if (!fw_fail)
        begin
           $display("  [%0t] attach: boot keyboard on J15, gamepad on J16", $time);
           usb_dev0.attach(usb_dev0.P_KBD);
           usb_dev1.attach(usb_dev1.P_GAMEPAD);

           // Step 0x11: both ports in RUN; one report each
           wait_led(8'h11);
        end
      if (!fw_fail)
        begin
           $display("  [%0t] reports: left shift + a on J15, right + A on J16", $time);
           usb_dev0.push_report(8, KBD_SHIFT_A);
           usb_dev1.push_report(8, PAD_RIGHT_A);

           // Step 0x12: the press decoded; release everything
           wait_led(8'h12);
        end
      if (!fw_fail)
        begin
           $display("  [%0t] reports: keys released on J15, pad idle on J16", $time);
           usb_dev0.push_report(8, KBD_RELEASE);
           usb_dev1.push_report(8, PAD_IDLE);

           // Step 0x13: the release decoded; unplug the keyboard
           wait_led(8'h13);
        end
      if (!fw_fail)
        begin
           // Between two host packets: a host still driving after the
           // device has gone would trip the model's detached-drive check
           wait (dut.usb0_oe === 1'b0);
           $display("  [%0t] detach: keyboard from J15", $time);
           usb_dev0.detach;

           wait_led(8'hA5);
        end
      repeat (50) @(posedge SYS_CLK);

      if (fw_fail)
        begin
           $display("  FAIL: firmware reported a failure on port %0d at step 0x%01h (LED=0x%02h)",
                    (LED[7:4] === 4'hD) ? 1 : 0, LED[3:0], LED);
           uerrors = uerrors + 1;
        end
      else
        $display("  PASS: firmware checks on both ports (enumeration, DEV_ID, boot protocol, key and pad reports, detach, interrupts)");

      if (usb_dev1.configured !== 1'b1)
        begin
           $display("  FAIL: the J16 gamepad is not configured at the end of the test");
           uerrors = uerrors + 1;
        end
      if (usb_dev0_attached !== 1'b0)
        begin
           $display("  FAIL: the J15 keyboard is still attached");
           uerrors = uerrors + 1;
        end

      usb_dev0.dump_summary;
      usb_dev1.dump_summary;
      if ((usb_dev0_err != 0) || (usb_dev1_err != 0))
        begin
           $display("  FAIL: device model checks: %0d error(s) on J15, %0d on J16", usb_dev0_err, usb_dev1_err);
           uerrors = uerrors + 1;
        end
      else
        $display("  PASS: device model checks on both ports (0 errors)");

      $display("");
      if (uerrors == 0)
         $display("|                  USB HOST TEST PASSED                  |");
      else
         $display("|             USB HOST TEST FAILED (%0d error(s))         |", uerrors);
      $display("");

      error = error + uerrors;
      stimulus_done = 1;
   end

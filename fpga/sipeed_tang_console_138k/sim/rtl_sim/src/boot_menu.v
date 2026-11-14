//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    boot_menu
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : boot_menu.v
// Module Description : arvern FPGA test stimulus for the boot menu
//                      (software/apps/boot_menu).
//
//   The microSD slot holds the FAT12 image of the app's sim/gen_card.py
//   (sd_card_img.v): /APPS/APPA (a test program smaller than the menu's ROM
//   image), /APPS/APPB (a larger one), /APPS/EMPTY (no APP.BIN), /APPS/GROUP
//   (a folder holding the program APPC) and /APPS/HUGE (an APP.BIN over the
//   size limit). The menu shows 0x80 | the
//   selection on the LED register, 0x40 while loading, 0x20 | an error code;
//   a test program shows its id (0xA1, 0xB2) once it has checked the sum of
//   its image in the program memory, 0xEE if the sum is wrong. Driving KEY[1]
//   alone (a press: the next entry, held: start) and the card's presence,
//   the stimulus checks:
//     0) with the slot empty at reset the menu reports no card (error 1);
//        once the card is inserted it reads it;
//     1) the menu lists APPA, APPB, HUGE (EMPTY skipped), then the folder
//        GROUP, APPA selected,
//        its music on the build's default sound output only (HDMI: the
//        Belenos I2S input; panel: the Pmod I2S2 on PMOD1), the onboard
//        amplifier and the other output idle;
//     2) two presses select APPB, then HUGE;
//     3) starting HUGE is refused (error 5), the menu comes back on HUGE;
//     4) a press selects GROUP; held, it opens the folder (the way back, then
//        APPC, selected); a press wraps to the way back, held it returns to
//        the list with GROUP selected;
//     5) a press wraps to APPA, another selects APPB; the card removed while
//        the list is shown: the menu notices (error 1), the card put back:
//        the list again, APPB still selected;
//     6) starting APPB loads it (the part over the menu staged, the rest
//        straight into place) and the program runs with a correct image.
// vscreen: no_input   (the stimulus drives KEY)
// defines: -D TIMEOUT_MS=1000
//----------------------------------------------------------------------------

`define SD_IMAGE "../../../software/apps/boot_menu/sim/sd_image.mem"

integer    ierrors, cnt, k;

// I2S bit clock edges on each sound output
integer    bclk_hdmi = 0, bclk_spk = 0, bclk_pmod = 0;
always @(posedge dut.i2s_hdmi_bclk) bclk_hdmi = bclk_hdmi + 1;
always @(posedge I2S_BCLK)          bclk_spk  = bclk_spk + 1;
always @(posedge PMOD1[4])          bclk_pmod = bclk_pmod + 1;   // Pmod I2S2 D/A SCLK (GPIO 12)

task check_sound;
   integer h0, s0, p0;
   begin
      h0 = bclk_hdmi; s0 = bclk_spk; p0 = bclk_pmod;
      wait_us(500);
      if (TB_HDMI ? (bclk_hdmi - h0 < 500) : (bclk_pmod - p0 < 500)) begin
         $display("  FAIL: no I2S stream on the %0s (%0d BCLK edges in 0.5 ms)", TB_HDMI ? "HDMI input" : "Pmod",
                  TB_HDMI ? bclk_hdmi - h0 : bclk_pmod - p0);
         ierrors = ierrors + 1;
      end
      if ((bclk_spk != s0) || (TB_HDMI ? (bclk_pmod != p0) : (bclk_hdmi != h0))) begin
         $display("  FAIL: the stream also on another output (speaker %0d, HDMI %0d, Pmod %0d edges)",
                  bclk_spk - s0, bclk_hdmi - h0, bclk_pmod - p0);
         ierrors = ierrors + 1;
      end
      if (dut.ahb_bus_system_inst.periph0_pa_en_o !== 1'b0) begin
         $display("  FAIL: the speaker amplifier is on");
         ierrors = ierrors + 1;
      end
   end
endtask

wire [7:0] led = LED;

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

// The card out (it loses its power: a new initialisation is needed) or in,
// between two selections of the card
task card;
   input in;
   begin
      wait (SD_DAT3 === 1'b1);
      if (!in) sd_reset_card;
      t_none = !in;
   end
endtask

// TTY echo
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0])
    $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);

task wait_us;
   input integer n;
   begin
      repeat (n * 33) @(posedge dut.clk_sys);
   end
endtask

// Wait for an LED value, at most ms milliseconds
task wait_led;
   input   [7:0] v;
   input integer ms;
   input [8*40:1] what;
   begin
      for (k = 0; (k < ms * 10) && (led !== v); k = k + 1)
         wait_us(100);
      if (led !== v) begin
         $display("  FAIL: %0s: LED 0x%02x (0x%02x expected)", what, led, v);
         ierrors = ierrors + 1;
      end else
         $display("  ok:   %0s (LED 0x%02x)", what, v);
   end
endtask

// KEY[1]: a press (0.8 ms: longer than the menu's card check, a sector read)
// or a hold (1.6 ms; the menu starts at 1.2 ms)
task key1;
   input integer us;
   begin
      KEY[1] = 1'b0;
      wait_us(us);
      KEY[1] = 1'b1;
      wait_us(200);
   end
endtask

initial
   begin
      ierrors = 0;
      t_none = 1'b1; t_sel = 3'd0; sd_dat0_oe = 1'b0; sd_dat0_v = 1'b1;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA BOOT MENU TEST (programs from the SD card)      |");
      $display(" ====================================================================");
      $display("");

      force dut.ahb_bus_system_inst.apb_board_io_inst.sync_deb_key1.debounce_threshold = 19'd15;

      // 0) No card at reset, then the card inserted
      cnt = ierrors;
      wait_led(8'h21, 200, "no card at reset (error 1)");
      card(1'b1);
      if (ierrors == cnt) $display("  PASS: an empty slot is reported");

      // 1) The menu, APPA selected
      cnt = ierrors;
      wait_led(8'h80, 200, "card inserted: menu shown, APPA selected");
      if (ierrors == cnt) $display("  PASS: the menu lists the card's programs");
      cnt = ierrors;
      check_sound;
      if (ierrors == cnt) $display("  PASS: the music on the %0s only", TB_HDMI ? "HDMI audio input" : "Pmod I2S2");

      // 2) Two presses: APPB, HUGE
      cnt = ierrors;
      key1(800);
      wait_led(8'h81, 5, "a press selects APPB");
      key1(800);
      wait_led(8'h82, 5, "a press selects HUGE (EMPTY skipped)");
      if (ierrors == cnt) $display("  PASS: KEY[1] presses move the selection");

      // 3) Starting HUGE: refused, back to the menu on HUGE
      cnt = ierrors;
      key1(1600);
      wait_led(8'h25, 5, "HUGE refused (error 5: too large)");
      wait_led(8'h82, 60, "the menu comes back, HUGE selected");
      if (ierrors == cnt) $display("  PASS: an image over the limit is refused");

      // 4) The folder: in, the way back, out
      cnt = ierrors;
      key1(800);
      wait_led(8'h83, 5, "a press selects the folder GROUP");
      key1(1600);
      wait_led(8'h81, 60, "held: the folder, APPC selected");
      wait_us(3000);                    // the release seen before the next press
      key1(800);
      wait_led(8'h80, 5, "a press wraps to the way back");
      key1(1600);
      wait_led(8'h83, 60, "held: the list again, GROUP selected");
      wait_us(3000);
      if (ierrors == cnt) $display("  PASS: a folder of programs opens and closes");

      // 5) Wrap to APPA, then APPB
      cnt = ierrors;
      key1(800);
      wait_led(8'h80, 5, "a press wraps to APPA");
      key1(800);
      wait_led(8'h81, 5, "a press selects APPB");
      if (ierrors == cnt) $display("  PASS: the selection wraps");
      cnt = ierrors;
      card(1'b0);
      wait_led(8'h21, 200, "card removed: the menu notices (error 1)");
      card(1'b1);
      wait_led(8'h81, 200, "card put back: the list, APPB selected");
      if (ierrors == cnt) $display("  PASS: a card removed and put back is read again");

      // 6) Start APPB: loaded, started, its image correct
      cnt = ierrors;
      key1(1600);
      wait_led(8'h40, 5, "loading APPB");
      wait_led(8'hB2, 200, "APPB runs with a correct image");
      wait_us(500);
      if (led !== 8'hB2) begin $display("  FAIL: LED 0x%02x after the start", led); ierrors = ierrors + 1; end
      if (ierrors == cnt) $display("  PASS: a program larger than the menu loads and runs");

      $display("");
      if (ierrors == 0)
         $display("|                 BOOT MENU TEST PASSED                  |");
      else
         $display("|            BOOT MENU TEST FAILED (%0d error(s))        |", ierrors);
      $display("");

      error = error + ierrors;
      stimulus_done = 1;
   end

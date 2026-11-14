//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tst_sd
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tst_sd.v
// Module Description : arvern FPGA test stimulus for the microSD slot through
//                      the apb_spi driver (software/apps/tst_sd).
//
//   The SD card model of the apb_spi bench (sd_card.v: SPI mode, SDHC, 16
//   blocks) on the slot pins: DAT3 selects it, CMD in, DAT0 out. The firmware
//   initialises the card, reads and writes single and multiple blocks and
//   compares; this stimulus checks its verdict on the LEDs (0xA5 pass,
//   0xE0 | step fail) and the blocks the card holds after the writes.
// vscreen: no_input
//----------------------------------------------------------------------------

integer    ierrors;
integer    k;

// Bench interface of sd_card.v: the resolved pads, the chip select of the
// transaction, the card's data-out driver
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

`include "sd_card.v"

always @(negedge SD_DAT3) sd_cs_assert;
always @(posedge SD_DAT3) sd_cs_release;
always @(posedge SD_CLK)  sd_sample;
always @(negedge SD_CLK)  sd_launch;

// Byte i of the card after the firmware's writes: blocks 5-7 hold the
// firmware's pattern, the others the model's initial content
function [7:0] sd_expect;
   input integer i;
   integer       b;
   begin
      b = i >> 9;
      if ((b >= 5) && (b <= 7)) sd_expect = (i * 3 + b * 29 + 8'h5A) & 8'hFF;
      else                      sd_expect = (i * 7 + b * 13) & 8'hFF;
   end
endfunction

task wait_led;
   input [7:0] v;
   begin
      wait ((LED === v) || (LED[7:4] === 4'hE));
      @(negedge dut.clk_sys);
   end
endtask

initial
   begin
      ierrors = 0; t_none = 1'b0; t_sel = 3'd0; sd_dat0_oe = 1'b0; sd_dat0_v = 1'b1;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|   arvern FPGA microSD DRIVER TEST (apb_spi_sd: init, read, write)  |");
      $display(" ====================================================================");
      $display("");

      wait_led(8'hA5);
      repeat (50) @(posedge SYS_CLK);
      if (LED !== 8'hA5)
        begin
           $display("  FAIL: firmware reported failure at step %0d (LED=0x%02h)", LED[3:0], LED);
           ierrors = ierrors + 1;
        end
      else
        $display("  PASS: firmware checks (init, single / multiple block reads and writes)");

      for (k = 0; (k < SD_BLKS * 512) && (ierrors < 4); k = k + 1)
        if (sd_mem[k] !== sd_expect(k))
          begin
             $display("  FAIL: card byte %0d (block %0d) 0x%02h, expected 0x%02h", k, k >> 9, sd_mem[k], sd_expect(k));
             ierrors = ierrors + 1;
          end
      if (ierrors == 0) $display("  PASS: card content after the writes (%0d commands)", sd_ncmd);

      $display("");
      if (ierrors == 0)
         $display("|               microSD DRIVER TEST PASSED               |");
      else
         $display("|          microSD DRIVER TEST FAILED (%0d error(s))      |", ierrors);
      $display("");

      error = error + ierrors;
      stimulus_done = 1;
   end

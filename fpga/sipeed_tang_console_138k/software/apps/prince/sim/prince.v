//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    prince
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : prince.v
// Module Description : arvern FPGA test stimulus for the Prince of Persia
//                      port (software/apps/prince).
//
//   The microSD slot holds the game's packs in /APPS/PRINCE (sd_card_img.v,
//   the FAT12 image of sim/gen_sdimage.py). The simulation build runs the
//   game's clock 500 times faster (the board's timer), reports every 50th frame ("prince:
//   present <n>") and each level it starts ("prince: level <n> at present
//   <p>"). A gamepad on USB-A J15 (usb_dev0, DragonRise-style reports):
//   Start (Enter) every 20 frames until level 1 starts, then Right held (the
//   Kid runs). It passes when level 1 has run PRINCE_LEVEL_PRESENTS frames
//   with no trap or exit reported. Every fifth panel frame is saved
//   (frame_log/fb_<n>.hex, 480 x 272 RGB565 -> PNG).
// vscreen: no_input
// defines: -D TIMEOUT_MS=12000
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

`define SD_IMAGE "../../../software/apps/prince/sim/sd_image.mem"

localparam integer PRINCE_LEVEL_PRESENTS = 100;  // presents in level 1

// Gamepad reports (8 bytes, byte 0 in bits [7:0]): byte 0 X, byte 1 Y (7F
// centre), byte 5 bit 5 A, byte 6 bit 4 Select, bit 5 Start
localparam [63:0] PAD_IDLE  = 64'h0000_0F80_8000_7F7F,
                  PAD_START = 64'h0020_0F80_8000_7F7F,
                  PAD_RIGHT = 64'h0000_0F80_8000_7FFF;

integer     frame_nr;
reg [255:0] fname;

// The TTY: echoed; its lines checked
reg [8*80-1:0] tty_line;
integer        tty_n, presents, faults, level_at, n_pos;
initial begin tty_line = 0; tty_n = 0; presents = 0; faults = 0; level_at = -1; end
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0]) begin
     $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);
     if (dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0] == 8'h0A) begin
        if ((tty_line[8*80-1 -: 8*12] == "prince: trap") || (tty_line[8*80-1 -: 8*12] == "prince: exit") ||
            (tty_line[8*80-1 -: 8*18] == "prince: misaligned") || (tty_line[8*80-1 -: 8*10] == "prince: no")) begin
           faults = faults + 1;
           $display("  FAIL: reported by the firmware");
        end else if ((tty_line[8*80-1 -: 8*16] == "prince: level 1 ") && (level_at < 0)) begin
           level_at = presents;
        end else if (tty_line[8*80-1 -: 8*16] == "prince: present ") begin
           presents = presents + 50;
           if ((level_at >= 0) && (presents >= level_at + PRINCE_LEVEL_PRESENTS) && !stimulus_done) begin
              if (faults == 0)
                $display("  PASS: title, start, level 1: %0d frames in the level, no fault", presents - level_at);
              else
                error = error + 1;
              stimulus_done = 1;
           end
        end
        tty_line = 0;
        tty_n    = 0;
     end else if (tty_n < 80) begin
        tty_line[8*(79 - tty_n) +: 8] = dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0];
        tty_n = tty_n + 1;
     end
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

// The gamepad: a report until the host has taken it, held for some panel frames
task pad_hold;
   input [63:0] rpt;
   input integer hold;
   integer n0, k;
   begin
      n0 = usb_dev0.int_ack_cnt;
      usb_dev0.push_report(8, rpt);
      wait (usb_dev0.int_ack_cnt != n0);
      for (k = 0; k < hold; k = k + 1) @(panel.frame_done);
      $display("  pad %h at %0d presents", rpt, presents);
   end
endtask

task pad_tap;
   input [63:0] rpt;
   input integer hold;
   begin
      pad_hold(rpt, hold);
      pad_hold(PAD_IDLE, 0);
   end
endtask

initial
   begin
      @(posedge dut.hresetn);
      repeat (60) @(panel.frame_done);
      while (level_at < 0) begin                // Start until level 1 begins
         pad_tap(PAD_START, 2);
         repeat (20) @(panel.frame_done);
      end
      repeat (10) @(panel.frame_done);
      pad_hold(PAD_RIGHT, 40);                  // the Kid runs right
      pad_hold(PAD_IDLE, 0);
   end

// Panel frames
always @(panel.frame_done) if ($time > 0)
  begin
     if (frame_nr % 5 == 0)
       begin
          $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
          $writememh(fname, panel.fb);
       end
     $display("  panel frame %0d at %0t, SD commands %0d", frame_nr, $time, sd_ncmd);
     frame_nr = frame_nr + 1;
  end

initial
   begin
      frame_nr = 0; t_none = 1'b0; t_sel = 3'd0; sd_dat0_oe = 1'b0; sd_dat0_v = 1'b1;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);
      usb_dev0.attach(usb_dev0.P_GAMEPAD);
      $display("");
      $display(" Prince of Persia port: the game's packs on the microSD card model");
      $display("");
   end

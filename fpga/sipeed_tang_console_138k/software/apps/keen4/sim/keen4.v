//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    keen4
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : keen4.v
// Module Description : arvern FPGA test stimulus for the Commander Keen 4
//                      port (software/apps/keen4).
//
//   The microSD slot holds the game's data in /APPS/KEEN4 (sd_card_img.v,
//   the FAT12 image of sim/gen_sdimage.py, made from tools/fetch_data.py's
//   files). The simulation build runs the game clock 500 times faster and
//   reports every tenth frame it presents on the TTY ("keen4: present <n>").
//   ("keen4: map <n>" when a map loads). A gamepad on USB-A J15 (usb_dev0,
//   DragonRise-style reports): during the first demo Start (the main menu),
//   A (New Game), A (the difficulty, again until the world map, map 0, loads); then Right
//   held (Keen walks). The stimulus echoes the TTY, saves every fifth panel
//   frame (frame_log/fb_<n>.hex, 480 x 272 RGB565 -> PNG) and passes when the
//   world map has run KEEN_MAP_PRESENTS frames with no trap, misaligned
//   access or exit reported.
// vscreen: no_input
// defines: -D TIMEOUT_MS=12000
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

`define SD_IMAGE "../../../software/apps/keen4/sim/sd_image.mem"

localparam integer KEEN_MENU_AT      = 40;   // presents before Start
localparam integer KEEN_MAP_PRESENTS = 60;   // presents on the world map

// Gamepad reports (8 bytes, byte 0 in bits [7:0]): byte 0 X, byte 1 Y (7F
// centre), byte 5 bit 5 A, byte 6 bit 5 Start
localparam [63:0] PAD_IDLE  = 64'h0000_0F80_8000_7F7F,
                  PAD_A     = 64'h0000_2F80_8000_7F7F,
                  PAD_START = 64'h0020_0F80_8000_7F7F,
                  PAD_RIGHT = 64'h0000_0F80_8000_7FFF;

integer     frame_nr;
reg [255:0] fname;

// The TTY: echoed; its lines checked
reg [8*80-1:0] tty_line;
integer        tty_n, presents, faults, map_at;
initial begin tty_line = 0; tty_n = 0; presents = 0; faults = 0; map_at = -1; end
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0]) begin
     $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);
     if (dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0] == 8'h0A) begin
        if ((tty_line[8*80-1 -: 8*11] == "keen4: trap") || (tty_line[8*80-1 -: 8*11] == "keen4: exit") ||
            (tty_line[8*80-1 -: 8*17] == "keen4: misaligned")) begin
           faults = faults + 1;
           $display("  FAIL: reported by the firmware");
        end else if ((tty_line[8*80-1 -: 8*12] == "keen4: map 0") && (tty_line[8*68-1 -: 8] == 8'h00)) begin
           map_at = presents;
        end else if (tty_line[8*80-1 -: 8*15] == "keen4: present ") begin
           presents = presents + 10;
           if ((map_at >= 0) && (presents >= map_at + KEEN_MAP_PRESENTS) && !stimulus_done) begin
              if (faults == 0)
                $display("  PASS: title, demo, menu, new game: %0d frames on the world map, no fault", presents - map_at);
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

// The gamepad: a report until the host has taken it, and two panel frames more
task pad_tap;
   input [63:0] rpt;
   input integer hold;
   integer n0, k;
   begin
      n0 = usb_dev0.int_ack_cnt;
      usb_dev0.push_report(8, rpt);
      wait (usb_dev0.int_ack_cnt != n0);
      for (k = 0; k < hold; k = k + 1) @(panel.frame_done);
      usb_dev0.push_report(8, PAD_IDLE);
      $display("  pad %h at %0d presents", rpt, presents);
   end
endtask

initial
   begin
      @(posedge dut.hresetn);
      wait (presents >= KEEN_MENU_AT);
      pad_tap(PAD_START, 6);
      repeat (40) @(panel.frame_done);          // the menu draws only on input
      pad_tap(PAD_A, 6);
      repeat (40) @(panel.frame_done);
      while (map_at < 0) begin                  // the difficulty: A until the map loads
         pad_tap(PAD_A, 6);
         repeat (60) @(panel.frame_done);
      end
      wait (map_at >= 0);
      wait (presents >= map_at + 20);
      pad_tap(PAD_RIGHT, 30);
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
      $display(" Commander Keen 4 port: shareware data on the microSD card model");
      $display("");
   end

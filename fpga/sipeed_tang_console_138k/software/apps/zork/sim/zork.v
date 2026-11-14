//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    zork
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : zork.v
// Module Description : arvern FPGA test stimulus for the Zork port
//                      (software/apps/zork).
//
//   The microSD slot holds the three story files in /APPS/ZORK (sd_card_img.v,
//   the FAT12 image of sim/gen_sdimage.py). The simulation build echoes the
//   game's text on the TTY and reports each gamepad command ("zork: command
//   <words>"). A gamepad on USB-A J15 (usb_dev0, DragonRise-style reports):
//   A (Zork I in the menu); a boot keyboard on J16 (usb_dev1) types "Open
//   mailbox" and Enter (the game must answer "Opening the small mailbox");
//   then the gamepad plays Up (north), Right (east), A (look), B
//   (inventory). It passes when the keyboard's command was answered and the
//   four gamepad commands read, with no trap or error reported. Every fifth panel frame is saved
//   (frame_log/fb_<n>.hex, 480 x 272 RGB565 -> PNG).
// vscreen: no_input
// defines: -D TIMEOUT_MS=12000
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

`define SD_IMAGE "../../../software/apps/zork/sim/sd_image.mem"

localparam integer ZORK_COMMANDS = 4;

// Gamepad reports (8 bytes, byte 0 in bits [7:0]): byte 0 X, byte 1 Y (7F
// centre), byte 5 bit 5 A, byte 6 bit 4 Select, bit 5 Start
localparam [63:0] PAD_IDLE  = 64'h0000_0F80_8000_7F7F,
                  PAD_A     = 64'h0000_2F80_8000_7F7F,
                  PAD_B     = 64'h0000_4F80_8000_7F7F,
                  PAD_UP    = 64'h0000_0F80_8000_007F,
                  PAD_RIGHT = 64'h0000_0F80_8000_7FFF;

integer     frame_nr;
reg [255:0] fname;

// The TTY: echoed; its lines checked
reg [8*80-1:0] tty_line;
integer        tty_n, presents, faults, commands, loaded, kbd_ok;
initial begin tty_line = 0; tty_n = 0; presents = 0; faults = 0; commands = 0; loaded = 0; kbd_ok = 0; end
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0]) begin
     $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);
     if (dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0] == 8'h0A) begin
        if ((tty_line[8*80-1 -: 8*10] == "zork: trap") || (tty_line[8*80-1 -: 8*11] == "zork: error") ||
            (tty_line[8*80-1 -: 8*16] == "zork: misaligned") || (tty_line[8*80-1 -: 8*8] == "zork: no")) begin
           faults = faults + 1;
           $display("  FAIL: reported by the firmware");
        end else if (tty_line[8*80-1 -: 8*15] == "zork: ZORK1.Z3 ") begin
           loaded = 1;
        end else if (tty_line[8*80-1 -: 8*14] == "zork: command ") begin
           commands = commands + 1;
        end else if (tty_line[8*80-1 -: 8*25] == "Opening the small mailbox") begin
           kbd_ok = 1;
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

// The keyboard: a key down (boot report: modifiers in byte 0, key in byte 2), then up
task key_tap;
   input [7:0] usage;
   input [7:0] mods;
   integer n0;
   begin
      n0 = usb_dev1.int_ack_cnt;
      usb_dev1.push_report(8, {40'h0, usage, 8'h00, mods});
      wait (usb_dev1.int_ack_cnt != n0);
      n0 = usb_dev1.int_ack_cnt;
      usb_dev1.push_report(8, 64'h0);
      wait (usb_dev1.int_ack_cnt != n0);
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
      repeat (20) @(panel.frame_done);
      pad_tap(PAD_A, 2);                        // the menu: Zork I
      wait (loaded);
      repeat (20) @(panel.frame_done);
      // "Open mailbox" + Enter on the keyboard (O shifted)
      key_tap(8'h12, 8'h02); key_tap(8'h13, 0); key_tap(8'h08, 0); key_tap(8'h11, 0);
      key_tap(8'h2C, 0);
      key_tap(8'h10, 0); key_tap(8'h04, 0); key_tap(8'h0C, 0); key_tap(8'h0F, 0);
      key_tap(8'h05, 0); key_tap(8'h12, 0); key_tap(8'h1B, 0);
      key_tap(8'h28, 0);
      repeat (20) @(panel.frame_done);
      pad_tap(PAD_UP, 2);    repeat (20) @(panel.frame_done);
      pad_tap(PAD_RIGHT, 2); repeat (20) @(panel.frame_done);
      pad_tap(PAD_A, 2);     repeat (20) @(panel.frame_done);
      pad_tap(PAD_B, 2);     repeat (40) @(panel.frame_done);
      if ((faults == 0) && (commands >= ZORK_COMMANDS) && kbd_ok)
        $display("  PASS: menu, Zork I, a typed command, %0d gamepad commands, no fault", commands);
      else begin
        $display("  FAIL: %0d commands, keyboard %0d, %0d faults", commands, kbd_ok, faults);
        error = error + 1;
      end
      stimulus_done = 1;
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
      usb_dev1.attach(usb_dev1.P_KBD);
      $display("");
      $display(" Zork port: the story files on the microSD card model");
      $display("");
   end

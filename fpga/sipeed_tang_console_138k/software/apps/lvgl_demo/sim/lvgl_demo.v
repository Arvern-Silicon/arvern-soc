//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    lvgl_demo
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : lvgl_demo.v
// Module Description : arvern FPGA test stimulus for the LVGL demo
//                      (software/apps/lvgl_demo).
//
//   The simulation build reports every frame LVGL renders on the TTY
//   ("lvgl_demo: frame <n> ..."), the pointer's effects ("lvgl_demo: button
//   <n>", "lvgl_demo: switch on|off", "lvgl_demo: slider <v>"), the end of
//   its attract mode ("lvgl_demo: attract done"), a gamepad move of the
//   pointer ("lvgl_demo: pointer <x>,<y>") and any trap. The pointer is the
//   Belenos hardware cursor. A gamepad sits on USB-A J15 (usb_dev0,
//   DragonRise-style reports), untouched at first: the attract mode clicks
//   the button (button 1), turns the switch off and drags the slider, then
//   rests on the button. Then the gamepad: A (the pointer clicks the button:
//   button 2) and Down (the pointer moves), each held until LVGL has
//   rendered two frames. It passes LVGL_TAIL frames later with all of them
//   seen once and no trap reported; it fails after LVGL_FRAMES frames.
//   Every fifth panel frame is saved (frame_log/fb_<n>.hex, 480 x 272 RGB565
//   -> PNG), every one with -D LVGL_LOCKSTEP (the firmware's
//   LVGL_SIM_LOCKSTEP comparison runs). The HDMI variant (BOARD_HDMI) runs the same checks; its frame
//   capture is not checked.
// vscreen: no_input
// defines: -D TIMEOUT_MS=6000
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

localparam integer LVGL_FRAMES = 120;          // frames LVGL renders at most
localparam integer LVGL_TAIL   = 4;            // ... after the gamepad's moves
`ifdef LVGL_LOCKSTEP
localparam integer SAVE_EVERY  = 1;            // panel frames saved
`else
localparam integer SAVE_EVERY  = 5;
`endif

// Gamepad reports (8 bytes, byte 0 in bits [7:0]): byte 0 X, byte 1 Y (7F
// centre), byte 5 bit 5 A
localparam [63:0] PAD_IDLE = 64'h0000_0F80_8000_7F7F,
                  PAD_A    = 64'h0000_2F80_8000_7F7F,
                  PAD_DOWN = 64'h0000_0F80_8000_FF7F;

integer     frame_nr;
reg [255:0] fname;

// The TTY: echoed; its lines checked
reg [8*80-1:0] tty_line;
integer        tty_n, frames, faults, clicked, switched, slid, attracted, pointed, done_at;
initial begin
   tty_line = 0; tty_n = 0; frames = 0; faults = 0; clicked = 0; switched = 0;
   slid = 0; attracted = 0; pointed = 0; done_at = -1;
end
always @(posedge dut.clk_sys)
  if (dut.ahb_bus_system_inst.apb_board_io_inst.tty_data_wr[0]) begin
     $write("%c", dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0]);
     if (dut.ahb_bus_system_inst.apb_board_io_inst.pwdata_i[7:0] == 8'h0A) begin
        if ((tty_line[8*80-1 -: 8*15] == "lvgl_demo: trap") || (tty_line[8*80-1 -: 8*14] == "lvgl_demo: no ")) begin
           faults = faults + 1;
           $display("  FAIL: reported by the firmware");
        end else if (tty_line[8*80-1 -: 8*17] == "lvgl_demo: button") begin
           clicked = clicked + 1;
        end else if (tty_line[8*80-1 -: 8*21] == "lvgl_demo: switch off") begin
           switched = switched + 1;
        end else if (tty_line[8*80-1 -: 8*17] == "lvgl_demo: slider") begin
           slid = slid + 1;
        end else if (tty_line[8*80-1 -: 8*23] == "lvgl_demo: attract done") begin
           attracted = attracted + 1;
        end else if (tty_line[8*80-1 -: 8*18] == "lvgl_demo: pointer") begin
           pointed = pointed + 1;
        end else if (tty_line[8*80-1 -: 8*17] == "lvgl_demo: frame ") begin
           frames = frames + 1;
           if (((done_at >= 0 && frames >= done_at + LVGL_TAIL) || (frames >= LVGL_FRAMES)) && !stimulus_done) begin
              if ((done_at >= 0) && (faults == 0) && (clicked == 2) && (switched == 1) && (slid == 1) &&
                  (attracted == 1) && (pointed == 1))
                $display("  PASS: %0d frames rendered, attract mode (button, switch, slider) and gamepad pointer (button, move) worked, no fault", frames);
              else begin
                $display("  FAIL: %0d frames, %0d faults, %0d clicks, %0d switch changes, %0d slider drags, %0d attract passes, %0d pointer moves",
                         frames, faults, clicked, switched, slid, attracted, pointed);
                error = error + 1;
              end
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

// The gamepad: a report until the host has taken it, held until LVGL has
// rendered two more frames
task pad_press;
   input [63:0] rpt;
   integer n0, f0;
   begin
      n0 = usb_dev0.int_ack_cnt;
      usb_dev0.push_report(8, rpt);
      wait (usb_dev0.int_ack_cnt != n0);
      f0 = frames;
      wait (frames >= f0 + 2);
      n0 = usb_dev0.int_ack_cnt;
      usb_dev0.push_report(8, PAD_IDLE);
      wait (usb_dev0.int_ack_cnt != n0);
      $display("  pad %h at frame %0d", rpt, frames);
   end
endtask

initial
   begin
      @(posedge dut.hresetn);
      wait (attracted == 1);                    // the attract mode rests on the button
      pad_press(PAD_A);                         // the pointer clicks it
      pad_press(PAD_DOWN);                      // the pointer moves down
      done_at = frames;
   end

// Panel frames
always @(panel.frame_done) if ($time > 0)
  begin
     if (frame_nr % SAVE_EVERY == 0)
       begin
          $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
          $writememh(fname, panel.fb);
       end
     $display("  panel frame %0d at %0t", frame_nr, $time);
     frame_nr = frame_nr + 1;
  end

initial
   begin
      frame_nr = 0;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);
      usb_dev0.attach(usb_dev0.P_GAMEPAD);
      $display("");
      $display(" LVGL demo: at most %0d rendered frames, the gamepad on J15", LVGL_FRAMES);
      $display("");
   end

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    picture_viewer
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : picture_viewer.v
// Module Description : arvern FPGA test stimulus for the picture viewer
//                      (software/apps/picture_viewer).
//
//   The microSD slot holds the FAT12 image of the app's sim/gen_card.py
//   (sd_card_img.v): /MEDIA/PICTURES with seven pictures of four coloured
//   quadrants (a baseline and a progressive JPEG, an RGB and an interlaced
//   palette PNG with a transparent quadrant, a two-frame animated GIF, a
//   BMP, a QOI), a JPEG cut short, then five pictures larger than the
//   screen (a baseline and a progressive JPEG, a PNG, a GIF, a BMP). The
//   firmware shows them in name order, fitted to the 480 x 272 screen at 16
//   bits a pixel (a small picture grows by a whole factor, a large one
//   shrinks); the LEDs carry the picture's number [6:0] once it is
//   drawn, [7] when it could not be. This stimulus, for each picture, waits
//   for its number and two panel frames, then checks the middle of each
//   quadrant on the panel model's image (turned by 180 degrees: the panel is
//   mounted upside down) against its colour, RGB565, within the dither and
//   (JPEG) the compression's error; the GIF's top half must show both its
//   frames' colours over 200 ms; the cut JPEG must give the error flag; a
//   large picture must fill the screen's width (two corner pixels too); a
//   short KEY[1] press moves to the next picture; after the last, the first
//   again. Every fourth panel frame is saved (frame_log/fb_<n>.hex).
// vscreen: no_input   (the stimulus drives KEY)
// defines: -D TIMEOUT_MS=9000
// fb2png: -W 480 -H 272
//----------------------------------------------------------------------------

`define SD_IMAGE "../../../software/apps/picture_viewer/sim/sd_image.mem"

integer     ierrors, cnt;
integer     frame_nr = 0;
reg [255:0] fname;

wire [6:0]  pic = LED[6:0];
wire        pic_err = LED[7];

always @(panel.frame_done) if ($time > 0)
    begin
       if (frame_nr % 4 == 0)
         begin
            $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
            $writememh(fname, panel.fb);
         end
       frame_nr = frame_nr + 1;
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

task wait_ms;
   input integer n;
   begin
      repeat (n * 33333) @(posedge dut.clk_sys);
   end
endtask

task wait_frames;
   input integer n;
   integer       f0;
   begin
      f0 = frame_nr;
      wait (frame_nr >= f0 + n);
   end
endtask

// Wait for picture n's number on the LEDs, at most ms milliseconds
task wait_pic;
   input [6:0]   n;
   input integer ms;
   integer       k;
   begin
      for (k = 0; (k < ms) && (pic !== n); k = k + 1)
         wait_ms(1);
      if (pic !== n) begin $display("  FAIL: picture %0d not shown (LEDs %h)", n, LED); ierrors = ierrors + 1; end
   end
endtask

// The panel's pixel at screen position (x, y), the frame turned by 180 degrees
function [15:0] px;
   input integer x;
   input integer y;
   begin
      px = panel.fb[(271 - y) * 480 + (479 - x)];
   end
endfunction

// The pixel at (x, y) against r, g, b (8 bits), within tol (8 bits)
task check_px;
   input [8*14:1] what;
   input integer  x, y, r, g, b, tol;
   reg     [15:0] p;
   integer        pr, pg, pb;
   begin
      p  = px(x, y);
      pr = p[15:11] * 255 / 31;
      pg = p[10:5] * 255 / 63;
      pb = p[4:0] * 255 / 31;
      if ((pr < r - tol) || (pr > r + tol) || (pg < g - tol) || (pg > g + tol) || (pb < b - tol) || (pb > b + tol))
        begin
           $display("  FAIL: %0s at (%0d, %0d): %0d %0d %0d, %0d %0d %0d expected", what, x, y, pr, pg, pb, r, g, b);
           ierrors = ierrors + 1;
        end
   end
endtask

// A short KEY[1] press, n microseconds (the firmware's clock runs 500
// times faster in simulation: 0.5 ms reads as 250 ms, a long press is 750)
task press_key1;
   input integer n;
   begin
      KEY[1] = 1'b0;
      repeat (n * 33) @(posedge dut.clk_sys);
      KEY[1] = 1'b1;
   end
endtask

// A picture's four quadrants: tl, tr, bl, br colours (8-bit RGB, packed)
task check_quads;
   input [8*14:1] what;
   input integer  x0, x1, y0, y1;            // the quadrants' middles on the screen
   input [23:0]   tl, tr, bl, br;
   input integer  tol;
   begin
      check_px(what, x0, y0, tl[23:16], tl[15:8], tl[7:0], tol);
      check_px(what, x1, y0, tr[23:16], tr[15:8], tr[7:0], tol);
      check_px(what, x0, y1, bl[23:16], bl[15:8], bl[7:0], tol);
      check_px(what, x1, y1, br[23:16], br[15:8], br[7:0], tol);
   end
endtask

// A picture larger than the screen, shrunk to fit its width: the middle of
// each quadrant, and a pixel near the top-left and the bottom-right corners
task check_big;
   input [8*14:1] what;
   input [23:0]   tl, tr, bl, br;
   input integer  tol;
   begin
      check_quads(what, 120, 360, 68, 203, tl, tr, bl, br, tol);
      check_px(what, 2, 3, tl[23:16], tl[15:8], tl[7:0], tol);
      check_px(what, 477, 268, br[23:16], br[15:8], br[7:0], tol);
   end
endtask

integer     k, seen_blue, seen_red;
reg [15:0]  p;

initial
   begin
      ierrors = 0; sd_dat0_oe = 1'b0; sd_dat0_v = 1'b1; t_none = 1'b0; t_sel = 3'd0;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);
      force dut.ahb_bus_system_inst.apb_board_io_inst.sync_deb_key1.debounce_threshold = 19'd15;

      $display("");
      $display(" ====================================================================");
      $display("|      arvern FPGA PICTURE VIEWER TEST (pictures from the SD card)   |");
      $display(" ====================================================================");

      // A1BASE.JPG: 64 x 48, x5 = 320 x 240 at (80, 16)
      wait_pic(7'd1, 400); wait_frames(2);
      cnt = ierrors;
      check_quads("1 JPEG base", 162, 322, 78, 198, 24'hE62828, 24'h28C83C, 24'h3246DC, 24'hF0F0F0, 20);
      if (ierrors == cnt) $display("  PASS: picture 1, baseline JPEG");
      press_key1(500);
      // A2PROG.JPG: the same geometry
      wait_pic(7'd2, 200); wait_frames(2);
      cnt = ierrors;
      check_quads("2 JPEG prog", 162, 322, 78, 198, 24'hE6DC28, 24'h28D2DC, 24'hD232C8, 24'h141414, 20);
      if (ierrors == cnt) $display("  PASS: picture 2, progressive JPEG");
      press_key1(500);
      // A3RGB.PNG: 48 x 36, x7 = 336 x 252 at (72, 10)
      wait_pic(7'd3, 200); wait_frames(2);
      cnt = ierrors;
      check_quads("3 PNG RGB", 159, 327, 76, 202, 24'hF08C1E, 24'h1EA096, 24'h808080, 24'h8C32C8, 12);
      if (ierrors == cnt) $display("  PASS: picture 3, RGB PNG");
      press_key1(500);
      // A4PAL.PNG: interlaced palette, the bottom-right quadrant transparent
      wait_pic(7'd4, 600); wait_frames(2);
      cnt = ierrors;
      check_quads("4 PNG Adam7", 159, 327, 76, 202, 24'hDC2828, 24'hE6DC28, 24'h28BE46, 24'h000000, 12);
      if (ierrors == cnt) $display("  PASS: picture 4, interlaced palette PNG with transparency");
      press_key1(500);
      // A5ANIM.GIF: 32 x 24, x11 = 352 x 264 at (64, 4); the top half blue
      // and red in turn, the bottom half always blue
      wait_pic(7'd5, 200); wait_frames(1);
      cnt = ierrors;
      seen_blue = 0; seen_red = 0;
      for (k = 0; k < 14; k = k + 1)
        begin
           wait_frames(1);
           p = px(245, 75);
           if (p[4:0] > 5'd20 && p[15:11] < 5'd10) seen_blue = seen_blue + 1;
           if (p[15:11] > 5'd20 && p[4:0] < 5'd10) seen_red = seen_red + 1;
        end
      if (!seen_blue || !seen_red)
        begin $display("  FAIL: the GIF's top half: %0d frames blue, %0d red (both expected)", seen_blue, seen_red); ierrors = ierrors + 1; end
      check_px("5 GIF bottom", 245, 207, 40, 60, 230, 9);
      if (ierrors == cnt) $display("  PASS: picture 5, animated GIF (both frames, transparency)");
      press_key1(500);
      // A6QUAD.BMP: 40 x 30, x9 = 360 x 270 at (60, 1)
      wait_pic(7'd6, 200); wait_frames(2);
      cnt = ierrors;
      check_quads("6 BMP", 154, 334, 68, 203, 24'hFFFFFF, 24'hE61E1E, 24'h1EC83C, 24'h283CE6, 9);
      if (ierrors == cnt) $display("  PASS: picture 6, BMP");
      press_key1(500);
      // A7QUAD.QOI: the same geometry
      wait_pic(7'd7, 200); wait_frames(2);
      cnt = ierrors;
      check_quads("7 QOI", 154, 334, 68, 203, 24'h1EDCDC, 24'hDC28D2, 24'hE6DC1E, 24'hFAFAFA, 9);
      if (ierrors == cnt) $display("  PASS: picture 7, QOI");
      press_key1(500);
      // A8BAD.JPG: the error flag, then on to the first picture again
      cnt = ierrors;
      for (k = 0; (k < 200) && (LED !== 8'h88); k = k + 1)
         wait_ms(1);
      if (LED !== 8'h88) begin $display("  FAIL: no error for the cut JPEG (LEDs %h)", LED); ierrors = ierrors + 1; end
      if (ierrors == cnt) $display("  PASS: the cut JPEG is refused");
      press_key1(500);
      // B1BASE.JPG: 1024 x 576, decoded at 1/2 (512 x 288), averaged to 480 x 270 at (0, 1)
      wait_pic(7'd9, 1200); wait_frames(2);
      cnt = ierrors;
      check_big("9 big JPEG", 24'hC83C28, 24'h32AA5A, 24'h3C50C8, 24'hE6D23C, 20);
      if (ierrors == cnt) $display("  PASS: picture 9, 1024 x 576 baseline JPEG shrunk (1/2 IDCT, averaged)");
      press_key1(500);
      // B2PROG.JPG: 640 x 360 progressive, its coefficients kept at 1/4 (160 x 90), grown back smoothly
      wait_pic(7'd10, 1200); wait_frames(2);
      cnt = ierrors;
      check_big("10 big prog", 24'h28C8D2, 24'hD23CAA, 24'hF09628, 24'h5A5A5A, 20);
      if (ierrors == cnt) $display("  PASS: picture 10, 640 x 360 progressive JPEG reduced to fit the memory");
      press_key1(500);
      // B3RGB.PNG: 520 x 296, averaged to 478 x 272 at (1, 0)
      wait_pic(7'd11, 1700); wait_frames(2);
      cnt = ierrors;
      check_big("11 big PNG", 24'h1E2878, 24'h96DC28, 24'hF0826E, 24'h78781E, 12);
      if (ierrors == cnt) $display("  PASS: picture 11, 520 x 296 PNG shrunk (averaged)");
      press_key1(500);
      // B4BIG.GIF: 512 x 290, nearest pixels to 480 x 272
      wait_pic(7'd12, 1200); wait_frames(2);
      cnt = ierrors;
      check_big("12 big GIF", 24'hF078B4, 24'h148C8C, 24'hC89628, 24'h823CC8, 12);
      if (ierrors == cnt) $display("  PASS: picture 12, 512 x 290 GIF shrunk (nearest)");
      press_key1(500);
      // B5BIG.BMP: 496 x 280 bottom-up (a seek outside the buffer per row), averaged to 480 x 271
      wait_pic(7'd13, 1500); wait_frames(2);
      cnt = ierrors;
      check_big("13 big BMP", 24'h78BEF0, 24'hAA461E, 24'h96E6BE, 24'h46506E, 12);
      if (ierrors == cnt) $display("  PASS: picture 13, 496 x 280 bottom-up BMP shrunk (averaged)");
      press_key1(500);
      cnt = ierrors;
      wait_pic(7'd1, 300);
      if (ierrors == cnt) $display("  PASS: the list wraps to picture 1");

      $display("");
      if (ierrors == 0)
         $display("|               PICTURE VIEWER TEST PASSED               |");
      else
         $display("|          PICTURE VIEWER TEST FAILED (%0d error(s))      |", ierrors);
      $display("");

      error = error + ierrors;
      stimulus_done = 1;
   end

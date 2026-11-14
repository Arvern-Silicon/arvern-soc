//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tst_blns
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tst_blns.v
// Module Description : arvern FPGA test stimulus for the Belenos graphic
//                      controller (firmware: software/apps/tst_blns).
//
//   The bench's RGB panel model (480 x 272) sits on the LCD pins. The
//   firmware checks BLNS_ID / BLNS_CFG and the VRAM window, draws an 8 x 2
//   frame of colour bars at 16 bpp and sends one manual refresh, checking the
//   REFR_DONE interrupt on MIP[16]; then the 1.0 GPU features (lines, the
//   hardware cursor, mask plane, palette remap, immediate source, discard,
//   command lists, FIFO room), each read back from the VRAM and registers;
//   it reports on the LEDs (0xA5 pass, 0xE0 | step failure, 0x0C while the
//   cursor is shown). The stimulus checks the cursor over the frame on the
//   panel while it is shown, then that the panel shows the frame (8 x 2,
//   1:1) in its centre with black around it, with no panel timing error,
//   and that the GPIO pins stay undriven.
//----------------------------------------------------------------------------

//
// Image of the 8 x 2 frame on the panel
localparam  X0 = (PANEL_W - 8) / 2;
localparam  Y0 = (PANEL_H - 2) / 2;


// Expected frame: same rule as the firmware (8 x 2, colour bars, the row
// band flips the blue LSBs).
function [15:0] exp_pixel;
   input integer x;
   input integer y;
   reg   [15:0] bar;
   begin
      case ((x * 8) / 8)
        0: bar = 16'hFFFF;  1: bar = 16'hFFE0;  2: bar = 16'h07FF;  3: bar = 16'h07E0;
        4: bar = 16'hF81F;  5: bar = 16'hF800;  6: bar = 16'h001F;  default: bar = 16'h0000;
      endcase
      exp_pixel = bar ^ ((y * 8) / 2);
   end
endfunction

// The cursor the firmware shows over the frame at (2, 0): shape codes of
// frame pixels (2..7, 0..1), the colours RGB888 0xF80000, 0x00FC00, 0x0000F8
// as the panel's RGB565
function [15:0] exp_cursor;
   input integer x;
   input integer y;
   reg   [1:0] c;
   begin
      c = 2'd0;
      if (x >= 2)
        case (y * 8 + x - 2)
          0: c = 1;  1: c = 2;  2: c = 3;  3: c = 0;  4: c = 1;  5: c = 2;
          8: c = 3;  9: c = 0; 10: c = 0; 11: c = 3; 12: c = 2; 13: c = 1;
          default: c = 0;
        endcase
      case (c)
        2'd1:    exp_cursor = 16'hF800;
        2'd2:    exp_cursor = 16'h07E0;
        2'd3:    exp_cursor = 16'h001F;
        default: exp_cursor = exp_pixel(x, y);
      endcase
   end
endfunction

integer     bx, by;
integer     berrors;

initial
   begin
      berrors   = 0;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);
      repeat (2) @(posedge SYS_CLK);

      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA BELENOS TEST (regs + VRAM + RGB refresh)         |");
      $display(" ====================================================================");
      $display("");

      // The hardware cursor over the frame, while the firmware shows it
      // (LED 0x0C, four frames): a panel frame taken after that
      wait ((LED === 8'h0C) || (LED === 8'hA5) || (LED[7:4] === 4'hE));
      if (LED === 8'h0C)
        begin
           @(panel.frame_done);
           @(panel.frame_done);
           for (by = 0; by < 2; by = by + 1)
             for (bx = 0; bx < 8; bx = bx + 1)
               if (panel.fb[(Y0 + by)*PANEL_W + X0 + bx] !== exp_cursor(bx, by))
                 begin
                    $display("  FAIL: cursor: panel pixel (%0d,%0d) = 0x%04h, expected 0x%04h", X0 + bx, Y0 + by,
                             panel.fb[(Y0 + by)*PANEL_W + X0 + bx], exp_cursor(bx, by));
                    berrors = berrors + 1;
                 end
           if (berrors == 0)
             $display("  PASS: the hardware cursor over the frame (shape, position, colours, transparency)");
        end
      else
        begin
           $display("  FAIL: the firmware did not show the cursor (LED=0x%02h)", LED);
           berrors = berrors + 1;
        end

      // Wait for the firmware verdict
      wait ((LED === 8'hA5) || (LED[7:4] === 4'hE));
      repeat (50) @(posedge SYS_CLK);

      if (LED !== 8'hA5)
        begin
           $display("  FAIL: firmware reported failure at step %0d (LED=0x%02h)", LED[3:0], LED);
           berrors = berrors + 1;
        end
      else
        $display("  PASS: firmware checks (BLNS_ID, BLNS_CFG, VRAM window, REFR_DONE on MIP[16], lines, cursor, mask plane, remap, IMM, discard, lists, FIFO room)");

      // HDMI build: the firmware read both EDID blocks over the DDC (256 bytes,
      // and the single byte of its NACK check never sent)
      if (TB_HDMI && (u_edid.sent != 256))
        begin
           $display("  FAIL: %0d EDID bytes sent over the DDC, expected 256", u_edid.sent);
           berrors = berrors + 1;
        end
      else if (TB_HDMI)
        $display("  PASS: EDID read over the DDC (256 bytes)");

      // HDMI build: HDMI signalling on (video guard bands, data islands); the
      // mode is taken at a start of vertical blanking, so the first guarded
      // line may still be on its way
      if (TB_HDMI)
        begin : wait_vguard
           integer n;
           for (n = 0; (n < 600000) && (hdmi_rx.vguard == 0); n = n + 1) @(posedge SYS_CLK);
        end
      if (TB_HDMI && ((hdmi_rx.vguard == 0) || (hdmi_rx.island == 0)))
        begin
           $display("  FAIL: no HDMI structure seen (%0d video guard, %0d island characters)", hdmi_rx.vguard, hdmi_rx.island);
           berrors = berrors + 1;
        end
      else if (TB_HDMI)
        $display("  PASS: HDMI signalling (%0d video guard, %0d island characters)", hdmi_rx.vguard, hdmi_rx.island);
      if (!TB_HDMI && (u_edid.sent != 0))
        begin
           $display("  FAIL: DDC traffic in the RGB panel build");
           berrors = berrors + 1;
        end

      // Panel side: a panel frame taken after the firmware verdict (the one
      // ending first may have been taken before the frame was drawn)
      @(panel.frame_done);
      @(panel.frame_done);
      for (by = 0; by < 2; by = by + 1)
        for (bx = 0; bx < 8; bx = bx + 1)
          if (panel.fb[(Y0 + by)*PANEL_W + X0 + bx] !== exp_pixel(bx, by))
            begin
               $display("  FAIL: panel pixel (%0d,%0d) = 0x%04h, expected 0x%04h", X0 + bx, Y0 + by,
                        panel.fb[(Y0 + by)*PANEL_W + X0 + bx], exp_pixel(bx, by));
               berrors = berrors + 1;
            end
      if ((panel.fb[(Y0 - 1)*PANEL_W + X0] !== 16'h0000) || (panel.fb[Y0*PANEL_W + X0 - 1] !== 16'h0000) ||
          (panel.fb[(Y0 + 2)*PANEL_W + X0] !== 16'h0000) || (panel.fb[Y0*PANEL_W + X0 + 8] !== 16'h0000))
        begin
           $display("  FAIL: the border around the image is not black");
           berrors = berrors + 1;
        end
      if (panel.errors !== 0)
        begin
           $display("  FAIL: %0d panel timing errors", panel.errors);
           berrors = berrors + 1;
        end
      // PMOD pins undriven, but for the Pmod I2S2 on PMOD1 when it is the sound
      // output (the panel build's default)
      if (dut.gpio_pmod_oe !== ((dut.ahb_bus_system_inst.audio_out == 2'd2) ? 16'h7F00 : 16'h0000))
        begin
           $display("  FAIL: GPIO output enables 0x%04h", dut.gpio_pmod_oe);
           berrors = berrors + 1;
        end

      if (berrors == 0)
        $display("  PASS: the panel shows the 8 x 2 frame in its centre (%0d frames)", panel.frame_count);

      $display("");
      if (berrors == 0)
         $display("|                  BELENOS TEST PASSED                   |");
      else
         $display("|             BELENOS TEST FAILED (%0d error(s))          |", berrors);
      $display("");

      error = error + berrors;
      stimulus_done = 1;
   end

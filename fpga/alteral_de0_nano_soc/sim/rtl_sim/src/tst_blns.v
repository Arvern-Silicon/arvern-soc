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
//   An ILI9341 8080-16 panel model sits on the LT24 pins of GPIO_0. The
//   firmware checks BLNS_ID / BLNS_CFG and the VRAM window, initialises the
//   panel, draws an 8 x 2 frame of colour bars at 16 bpp and sends one manual
//   refresh, checking the REFR_DONE interrupt on MIP[16]; then the 1.0 GPU
//   features (lines, the hardware cursor, mask plane, palette remap,
//   immediate source, discard, command lists, FIFO room), each read back
//   from the VRAM and registers, the cursor also sent to the panel (a
//   refresh with it, LED 0x0C, then one without); it reports on the LEDs
//   (0xA5 pass, 0xE0 | step failure). The stimulus checks the cursor's
//   refresh, then that the panel received exactly those frames through
//   CASET/PASET/RAMWR, with no protocol anomaly, that the GPIO pins stay
//   undriven and that the debug references (GPIO_1[35:34]) read 3.3V / GND.
//----------------------------------------------------------------------------

//
// LT24 panel model on GPIO_0 (openMSP430 / LT24 pin map)
//------------------------------------------------------------
wire [15:0] lt24_d_host = {GPIO_0[24], GPIO_0[23], GPIO_0[22], GPIO_0[21],
                           GPIO_0[20], GPIO_0[19], GPIO_0[18], GPIO_0[17],
                           GPIO_0[16], GPIO_0[15], GPIO_0[14], GPIO_0[13],
                           GPIO_0[5],  GPIO_0[6],  GPIO_0[7],  GPIO_0[8]};
wire [15:0] lt24_d_panel;
wire        lt24_rd_drive = (GPIO_0[25] === 1'b0) && (GPIO_0[10] === 1'b0);

assign {GPIO_0[24], GPIO_0[23], GPIO_0[22], GPIO_0[21],
        GPIO_0[20], GPIO_0[19], GPIO_0[18], GPIO_0[17],
        GPIO_0[16], GPIO_0[15], GPIO_0[14], GPIO_0[13],
        GPIO_0[5],  GPIO_0[6],  GPIO_0[7],  GPIO_0[8]} = lt24_rd_drive ? lt24_d_panel : 16'hzzzz;

ili9341_8080_model #(.W(240), .H(320), .LOG_NR(64)) lt24 (
    .reset_n_i ( GPIO_0[33]        ),
    .cs_n_i    ( GPIO_0[25]        ),
    .dc_i      ( GPIO_0[12]        ),
    .wr_n_i    ( GPIO_0[11]        ),
    .rd_n_i    ( GPIO_0[10]        ),
    .d_i       ( lt24_d_host       ),
    .d_oe_i    ( dut.lcd_d_oe      ),
    .d_o       ( lt24_d_panel      )
);

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
      @(posedge FPGA_CLK1_50);
      @(posedge dut.hresetn);
      repeat (2) @(posedge FPGA_CLK1_50);

      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA BELENOS TEST (regs + VRAM + 8080 refresh)        |");
      $display(" ====================================================================");
      $display("");

      // The hardware cursor over the frame: the refresh the firmware sent with
      // it shown (LED 0x0C, held 1 ms)
      wait ((LED === 8'h0C) || (LED === 8'hA5) || (LED[7:4] === 4'hE));
      if (LED === 8'h0C)
        begin
           for (by = 0; by < 2; by = by + 1)
             for (bx = 0; bx < 8; bx = bx + 1)
               if (lt24.fb[by*240 + bx] !== exp_cursor(bx, by))
                 begin
                    $display("  FAIL: cursor: pixel (%0d,%0d) = 0x%04h, expected 0x%04h", bx, by,
                             lt24.fb[by*240 + bx], exp_cursor(bx, by));
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
      repeat (50) @(posedge FPGA_CLK1_50);

      if (LED !== 8'hA5)
        begin
           $display("  FAIL: firmware reported failure at step %0d (LED=0x%02h)", LED[3:0], LED);
           berrors = berrors + 1;
        end
      else
        $display("  PASS: firmware checks (BLNS_ID, BLNS_CFG, VRAM window, REFR_DONE on MIP[16], lines, cursor, mask plane, remap, IMM, discard, lists, FIFO room)");

      // Panel side: three refreshes of the 8 x 2 frame (the bars, with the
      // cursor, without it again)
      if (lt24.px_count !== 48)
        begin
           $display("  FAIL: panel received %0d pixels, expected 48", lt24.px_count);
           berrors = berrors + 1;
        end
      for (by = 0; by < 2; by = by + 1)
        for (bx = 0; bx < 8; bx = bx + 1)
          if (lt24.fb[by*240 + bx] !== exp_pixel(bx, by))
            begin
               $display("  FAIL: pixel (%0d,%0d) = 0x%04h, expected 0x%04h", bx, by, lt24.fb[by*240 + bx], exp_pixel(bx, by));
               berrors = berrors + 1;
            end
      if (lt24.fb[8] !== 16'h0000 || lt24.fb[2*240] !== 16'h0000)
        begin
           $display("  FAIL: pixels written outside the 8 x 2 window");
           berrors = berrors + 1;
        end
      if (lt24.last_cmd !== 8'h2C)
        begin
           $display("  FAIL: last panel command 0x%02h, expected RAMWR (0x2C)", lt24.last_cmd);
           berrors = berrors + 1;
        end
      if (lt24.anomaly_count !== 0)
        begin
           $display("  FAIL: %0d 8080 protocol anomalies", lt24.anomaly_count);
           berrors = berrors + 1;
        end
      if (GPIO_0[35] !== 1'b1 || GPIO_0[33] !== 1'b1)
        begin
           $display("  FAIL: LCD_ON / LCD_RESET_N not driven high");
           berrors = berrors + 1;
        end
      if (GPIO_1[35:34] !== 2'b10 || dut.gpio_oe !== 26'h0)
        begin
           $display("  FAIL: GPIO_1 pins (debug references GPIO_1[35:34] %b, GPIO output enables 0x%07h)", GPIO_1[35:34], dut.gpio_oe);
           berrors = berrors + 1;
        end

      if (berrors == 0)
        $display("  PASS: LT24 received the 8 x 2 frame (%0d commands, %0d pixels)", lt24.cmd_count, lt24.px_count);

      $display("");
      if (berrors == 0)
         $display("|                  BELENOS TEST PASSED                   |");
      else
         $display("|             BELENOS TEST FAILED (%0d error(s))          |", berrors);
      $display("");

      error = error + berrors;
      stimulus_done = 1;
   end

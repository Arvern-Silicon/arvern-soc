//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    demo_effects
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : demo_effects.v
// Module Description : arvern FPGA test stimulus for the effects demo
//                      (firmware: software/apps/demo_effects, built with
//                      -DSIMULATION: short effects, one pass, then LED 0xA5).
//
//   An ILI9341 8080-16 panel model sits on the LT24 pins of GPIO_0. The
//   stimulus follows the effects (LED = effect number) up to LED 0xA5, saves
//   the panel image
//   after every frame (frame_log/fb_<n>.hex, 240 x 320 RGB565), and checks
//   that every effect refreshes the panel with whole frames and that the 8080
//   protocol shows no anomaly.
//
// fb2png: --transpose
// vscreen: view=transpose
//----------------------------------------------------------------------------

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

localparam  FRAME_PX = 240 * 320;

integer     gerrors;
integer     frame_nr;
integer     scene_frames;
integer     sc;
reg [255:0] fname;

// Save the panel image at the end of every frame (end of the RAMWR stream)
always @(posedge GPIO_0[25])
  if ((lt24.last_cmd === 8'h2C) && (lt24.px_count >= (frame_nr + 1) * FRAME_PX))
    begin
       $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
       $writememh(fname, lt24.fb);
       $display("  frame %0d done at %0t (LED=0x%02h)", frame_nr, $time, LED);
       frame_nr     = frame_nr + 1;
       scene_frames = scene_frames + 1;
    end

initial
   begin
      gerrors      = 0;
      frame_nr     = 0;
      scene_frames = 0;
      @(posedge FPGA_CLK1_50);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|               arvern FPGA EFFECTS DEMO (Belenos)                   |");
      $display(" ====================================================================");
      $display("");

      wait (LED === 8'h01);
      sc = 1;
      while (LED !== 8'hA5)
        begin
           scene_frames = 0;
           $display("  effect %0d started at %0t", sc, $time);
           wait (LED !== sc[7:0]);
           if (scene_frames < 1)
             begin
                $display("  FAIL: effect %0d refreshed no frame", sc);
                gerrors = gerrors + 1;
             end
           if ((LED !== 8'hA5) && (LED !== sc[7:0] + 8'd1))
             begin
                $display("  FAIL: LED 0x%02h after effect %0d", LED, sc);
                gerrors = gerrors + 1;
                wait (LED === 8'hA5);
             end
           sc = sc + 1;
        end
      if (lt24.anomaly_count !== 0)
        begin
           $display("  FAIL: %0d 8080 protocol anomalies", lt24.anomaly_count);
           gerrors = gerrors + 1;
        end

      $display("");
      if (gerrors == 0)
         $display("|               EFFECTS DEMO PASSED              |");
      else
         $display("|          EFFECTS DEMO FAILED (%0d error(s))     |", gerrors);
      $display("");

      error = error + gerrors;
      stimulus_done = 1;
   end

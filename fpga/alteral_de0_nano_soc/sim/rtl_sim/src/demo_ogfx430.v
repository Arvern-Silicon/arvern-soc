//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    demo_ogfx430
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : demo_ogfx430.v
// Module Description : arvern FPGA test stimulus for the Belenos graphic demo
//                      (firmware: software/apps/demo_ogfx430, built with
//                      -DSIMULATION: 500x shorter delays, continuous refresh).
//
//   An ILI9341 8080-16 panel model sits on the LT24 pins of GPIO_0. The
//   stimulus follows the demo through its five colour depths (LED = bpp),
//   saves the panel image after every frame (frame_log/fb_<n>.hex, 240 x 320 RGB565,
//   for offline viewing) and checks that every mode refreshes the panel
//   with whole frames and no 8080 protocol anomaly.
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
integer     mode_frames;
integer     mode_px_start;
reg  [7:0]  mode_led;
reg [255:0] fname;

// Save the panel image at the end of every frame (end of the RAMWR stream)
always @(posedge GPIO_0[25])
  if ((lt24.last_cmd === 8'h2C) && (lt24.px_count >= (frame_nr + 1) * FRAME_PX))
    begin
       $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
       $writememh(fname, lt24.fb);
       $display("  frame %0d done at %0t (LED=%0d, %0d px)", frame_nr, $time, LED, lt24.px_count);
       frame_nr    = frame_nr + 1;
       mode_frames = mode_frames + 1;
    end

// One mode: wait for its LED value, then for the next one
task check_mode;
   input [7:0] bpp;
   input [7:0] next_bpp;
   begin
      wait (LED === bpp);
      mode_frames   = 0;
      mode_px_start = lt24.px_count;
      $display("  mode %0d bpp started at %0t", bpp, $time);
      wait (LED === next_bpp);
      if (mode_frames < 2)
        begin
           $display("  FAIL: %0d bpp mode refreshed %0d frame(s), expected at least 2", bpp, mode_frames);
           gerrors = gerrors + 1;
        end
   end
endtask

initial
   begin
      gerrors     = 0;
      frame_nr    = 0;
      mode_frames = 0;
      @(posedge FPGA_CLK1_50);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|          arvern FPGA BELENOS GRAPHIC DEMO (5 modes, 8080)          |");
      $display(" ====================================================================");
      $display("");

      check_mode(16, 8);
      check_mode( 8, 4);
      check_mode( 4, 2);
      check_mode( 2, 1);
      check_mode( 1, 16);

      if (lt24.anomaly_count !== 0)
        begin
           $display("  FAIL: %0d 8080 protocol anomalies", lt24.anomaly_count);
           gerrors = gerrors + 1;
        end

      $display("");
      if (gerrors == 0)
         $display("|               BELENOS GRAPHIC DEMO PASSED              |");
      else
         $display("|          BELENOS GRAPHIC DEMO FAILED (%0d error(s))      |", gerrors);
      $display("");

      error = error + gerrors;
      stimulus_done = 1;
   end

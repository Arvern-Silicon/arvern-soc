//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    demo_arvern_logo
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : demo_arvern_logo.v
// Module Description : arvern FPGA test stimulus for the aRVern logo demo
//                      (firmware: software/apps/demo_arvern_logo, built with
//                      -DSIMULATION: short scenes, one pass, then LED 0xA5).
//
//   The bench's RGB panel model (480 x 272) sits on the LCD pins. The
//   stimulus follows the scenes (LED = scene number), saves the panel image
//   after every frame (frame_log/fb_<n>.hex, 480 x 272 RGB565), and checks
//   that every scene sees panel frames and that the panel timing
//   shows no error.
//
// fb2png: -W 480 -H 272
// vscreen: none
//----------------------------------------------------------------------------

localparam  SCENE_NR = 6;

integer     gerrors;
integer     frame_nr;
integer     scene_frames;
integer     sc;
reg [255:0] fname;

// Save the panel image at the end of every frame (end of the RAMWR stream)
always @(panel.frame_done) if ($time > 0)
    begin
       $sformat(fname, "frame_log/fb_%0d.hex", frame_nr);
       $writememh(fname, panel.fb);
       $display("  frame %0d done at %0t (LED=0x%02h)", frame_nr, $time, LED);
       frame_nr     = frame_nr + 1;
       scene_frames = scene_frames + 1;
    end

initial
   begin
      gerrors      = 0;
      frame_nr     = 0;
      scene_frames = 0;
      @(posedge SYS_CLK);
      @(posedge dut.hresetn);

      $display("");
      $display(" ====================================================================");
      $display("|               arvern FPGA aRVern LOGO DEMO (Belenos)               |");
      $display(" ====================================================================");
      $display("");

      for (sc = 1; sc <= SCENE_NR; sc = sc + 1)
        begin
           wait (LED === sc[7:0]);
           scene_frames = 0;
           $display("  scene %0d started at %0t", sc, $time);
           wait (LED !== sc[7:0]);
           if (scene_frames < 1)
             begin
                $display("  FAIL: scene %0d refreshed no frame", sc);
                gerrors = gerrors + 1;
             end
        end
      if (LED !== 8'hA5)
        begin
           $display("  FAIL: LED 0x%02h after the last scene, expected 0xa5", LED);
           gerrors = gerrors + 1;
        end
      if (panel.errors !== 0)
        begin
           $display("  FAIL: %0d panel timing errors", panel.errors);
           gerrors = gerrors + 1;
        end

      $display("");
      if (gerrors == 0)
         $display("|               aRVern LOGO DEMO PASSED              |");
      else
         $display("|          aRVern LOGO DEMO FAILED (%0d error(s))     |", gerrors);
      $display("");

      error = error + gerrors;
      stimulus_done = 1;
   end

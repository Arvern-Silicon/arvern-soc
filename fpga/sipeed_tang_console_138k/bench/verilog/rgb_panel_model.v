//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    rgb_panel_model
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : rgb_panel_model.v
// Module Description : Parallel RGB (DPI) panel model, DE mode: samples the
//                      pixel at each rising pixel-clock edge while DE is high,
//                      fills an RGB565 image (fb, row-major, W x H, the format
//                      of fb2png.py), and checks the timing: every DE run is W
//                      pixels, H runs per frame, HSYNC low once between runs,
//                      VSYNC low once between frames. A frame ends at the
//                      first VSYNC low after its last line: frame_count then
//                      increments and frame_done toggles. The pins are ignored
//                      while rst_n_i is low (the driver's registers are not
//                      yet initialised).
//----------------------------------------------------------------------------

module  rgb_panel_model #(
    parameter W = 800,                    // active pixels per line
    parameter H = 480                     // active lines
) (
    input  wire        clk_i,             // pixel clock
    input  wire        rst_n_i,           // driver reset (active low)
    input  wire        de_i,              // data enable
    input  wire        hs_n_i,            // HSYNC (active low)
    input  wire        vs_n_i,            // VSYNC (active low)
    input  wire  [7:0] r_i,
    input  wire  [7:0] g_i,
    input  wire  [7:0] b_i
);

reg     [15:0] fb [0:W*H-1];
integer        frame_count;               // complete frames
reg            frame_done;                // toggles at each complete frame
integer        errors;

integer        x;                         // pixel in the current DE run
integer        y;                         // DE runs in the current frame
reg            de_q;
reg            hs_q;
reg            vs_q;
integer        hs_in_line;                // HSYNC pulses since the last DE run

initial begin
    frame_count = 0;
    frame_done  = 1'b0;
    errors      = 0;
    x           = 0;
    y           = 0;
    de_q        = 1'b0;
    hs_q        = 1'b1;
    vs_q        = 1'b1;
    hs_in_line  = 0;
end

always @(posedge clk_i) if (rst_n_i === 1'b1) begin
    // A DE run ends: one line
    if (de_q && !de_i) begin
        if (x != W) begin
            $display("ERROR: RGB panel model -- line %0d has %0d pixels (expected %0d) %t", y, x, W, $time);
            errors = errors + 1;
        end
        y          = y + 1;
        x          = 0;
        hs_in_line = 0;
    end
    if (hs_q && !hs_n_i)
        hs_in_line = hs_in_line + 1;
    if (de_i && !de_q && (y > 0) && (hs_in_line != 1)) begin
        $display("ERROR: RGB panel model -- %0d HSYNC pulses before line %0d (expected 1) %t", hs_in_line, y, $time);
        errors = errors + 1;
    end
    // VSYNC: the frame received so far is complete
    if (vs_q && !vs_n_i) begin
        if (y != 0) begin
            if (y != H) begin
                $display("ERROR: RGB panel model -- frame %0d has %0d lines (expected %0d) %t", frame_count, y, H, $time);
                errors = errors + 1;
            end
            frame_count = frame_count + 1;
            frame_done  = ~frame_done;
        end
        y = 0;
        x = 0;
    end
    if (de_i) begin
        if (de_i && vs_n_i === 1'b0) begin
            $display("ERROR: RGB panel model -- DE during VSYNC %t", $time);
            errors = errors + 1;
        end
        if ((x < W) && (y < H))
            fb[y*W + x] = {r_i[7:3], g_i[7:2], b_i[7:3]};
        x = x + 1;
    end
    de_q = de_i;
    hs_q = hs_n_i;
    vs_q = vs_n_i;
end

endmodule // rgb_panel_model

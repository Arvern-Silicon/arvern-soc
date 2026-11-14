//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    CLKDIV
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : CLKDIV.v
// Module Description : Behavioural model of the Gowin GW5A CLKDIV primitive
//                      (clock divider of the high-speed clock network), for
//                      simulation and lint; the Gowin tools supply the
//                      primitive itself. Integer DIV_MODE only (2, 4, 5, 8):
//                      CLKOUT toggles every DIV_MODE edges of HCLKIN (both
//                      edges counted), so its edges coincide with HCLKIN
//                      edges; RESETN low holds it low. CALIB is not modelled.
//----------------------------------------------------------------------------

module CLKDIV #(
    parameter DIV_MODE = "2",
    parameter GSREN    = "false"
) (
    output reg  CLKOUT,
    input  wire HCLKIN,
    input  wire RESETN,
    input  wire CALIB
);

localparam integer N = (DIV_MODE == "8") ? 8 : (DIV_MODE == "5") ? 5 : (DIV_MODE == "4") ? 4 : 2;

integer edges = 0;
initial CLKOUT = 1'b0;

always @(HCLKIN or negedge RESETN)
  if (!RESETN) begin
     CLKOUT = 1'b0;
     edges  = 0;
  end else if (HCLKIN === 1'b1 || HCLKIN === 1'b0) begin
     edges = edges + 1;
     if (edges == N) begin
        CLKOUT = ~CLKOUT;
        edges  = 0;
     end
  end

wire calib_unused = CALIB;

endmodule

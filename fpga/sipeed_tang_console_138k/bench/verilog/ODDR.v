//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ODDR
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ODDR.v
// Module Description : Behavioural model of the Gowin GW5A ODDR primitive
//                      (DDR output register), for simulation and lint; the
//                      Gowin tools supply the primitive itself. Q0 is D0 while
//                      CLK is high and D1 while it is low, without the
//                      primitive's input pipeline: exact for the constant
//                      inputs of a forwarded clock. Q1 follows TX.
//----------------------------------------------------------------------------

module ODDR #(
    parameter TXCLK_POL = 1'b0,
    parameter INIT      = 1'b0
) (
    output wire Q0,
    output wire Q1,
    input  wire D0,
    input  wire D1,
    input  wire TX,
    input  wire CLK
);

assign Q0 = CLK ? D0 : D1;
assign Q1 = TX;

endmodule // ODDR

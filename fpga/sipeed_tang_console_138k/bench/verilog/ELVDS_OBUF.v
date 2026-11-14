//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ELVDS_OBUF
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ELVDS_OBUF.v
// Module Description : Behavioural model of the Gowin emulated-LVDS output
//                      buffer, for simulation and lint: O = I, OB = ~I.
//----------------------------------------------------------------------------

module ELVDS_OBUF (
    input  wire I,
    output wire O,
    output wire OB
);

assign O  =  I;
assign OB = ~I;

endmodule // ELVDS_OBUF

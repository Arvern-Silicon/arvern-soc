//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    io_buf
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : io_buf.v
// Module Description : Bidirectional pad buffer: drives the pad while oe is
//                      set, reads it back in any direction (the tool maps it
//                      to an IOBUF). Same ports as the DE0 build's io_buf.
//----------------------------------------------------------------------------

module  io_buf (
    input  wire  datain,                 // value driven on the pad
    input  wire  oe,                     // output enable
    inout  wire  dataio,                 // pad
    output wire  dataout                 // pad read-back
);

assign dataio  = oe ? datain : 1'bz;
assign dataout = dataio;

endmodule // io_buf

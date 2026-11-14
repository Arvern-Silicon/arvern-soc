//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ram_24x512
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ram_24x512.v
// Module Description : Belenos colour LUT: 512 x 24 (two banks of 256 RGB888
//                      entries), one-cycle read latency (block RAM). Same ports
//                      as the DE0 build.
//----------------------------------------------------------------------------

module  ram_24x512 (
    input  wire             clk_i,           // clock
    input  wire             cen_i,           // chip enable (active low)
    input  wire             wen_i,           // write enable (active low)
    input  wire       [8:0] addr_i,          // entry address {bank, index}
    input  wire      [23:0] din_i,           // write data (RGB888)
    output wire      [23:0] dout_o           // read data (RGB888)
);

spram_be #(.ADDRW(9), .WORDS(512), .BYTES(3)) u_ram (
    .clk_i  ( clk_i     ),
    .cen_i  ( cen_i     ),
    .wen_i  ( {3{wen_i}} ),
    .addr_i ( addr_i    ),
    .din_i  ( din_i     ),
    .dout_o ( dout_o    )
);

endmodule // ram_24x512

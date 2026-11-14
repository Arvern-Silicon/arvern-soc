//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    spram_be
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : spram_be.v
// Module Description : Single-port synchronous RAM, inferred into block RAM:
//                      byte write enables, one-cycle read latency, optional
//                      $readmemh image. Active-low controls, as the macros
//                      of the DE0 build.
//----------------------------------------------------------------------------

module  spram_be #(
    parameter                  ADDRW     = 11,        // address width
    parameter                  WORDS     = 2048,      // depth in words (<= 2^ADDRW)
    parameter                  BYTES     = 4,         // byte lanes
    parameter                  INIT_FILE = ""         // $readmemh image ("" = none)
) (
    input  wire                clk_i,                 // clock
    input  wire                cen_i,                 // chip enable (active low)
    input  wire    [BYTES-1:0] wen_i,                 // byte write enables (active low)
    input  wire    [ADDRW-1:0] addr_i,                // word address
    input  wire  [8*BYTES-1:0] din_i,                 // write data
    output reg   [8*BYTES-1:0] dout_o                 // read data
);

reg [8*BYTES-1:0] mem [0:WORDS-1];

generate
    if (INIT_FILE != "") begin : g_init
        initial $readmemh(INIT_FILE, mem);
    end
endgenerate

// Normal write mode: a write cycle leaves the output unchanged (no consumer
// uses read data in a write cycle)
integer b;
always @(posedge clk_i)
    if (~cen_i) begin
        for (b = 0; b < BYTES; b = b + 1)
            if (~wen_i[b]) mem[addr_i][8*b +: 8] <= din_i[8*b +: 8];
        if (&wen_i) dout_o <= mem[addr_i];
    end

endmodule // spram_be

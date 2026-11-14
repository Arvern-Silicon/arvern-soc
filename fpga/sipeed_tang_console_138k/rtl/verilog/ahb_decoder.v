//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ahb_decoder
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ahb_decoder.v
// Module Description : AHB Decoder Example
//----------------------------------------------------------------------------

module  ahb_decoder #(
    parameter             ROM_SIZE     = 8*1024,                // Size of the program memory (in Bytes)
    parameter             SRAM_NX_SIZE = 8*1024,                // Size of the data SRAM (in Bytes)
    parameter             VRAM_SIZE    = 8*1024                 // Size of the Belenos VRAM window (in Bytes)
) (

// DECODER INTERFACES
    input  wire    [31:0] decoder_addr_i,
    output wire     [6:0] decoder_1hot_o
);


//=============================================================================
// AHB DECODER
//=============================================================================

// syn_keep on the address copy and the selects keeps the compares inside the
// decoder: Gowin synthesis otherwise shares them with address compares in
// other blocks (the Belenos VRAM port) and places them there
wire [31:0] addr      /* synthesis syn_keep=1 */;
wire  [6:0] dec_1hot  /* synthesis syn_keep=1 */;
assign      addr           = decoder_addr_i;
assign      decoder_1hot_o = dec_1hot;

assign dec_1hot[0]  = (addr>=32'h20000000) & (addr<(32'h20000000+ROM_SIZE    )); //   Program SRAM (writable, executable)
assign dec_1hot[1]  = (addr>=32'h80000000) & (addr<(32'h80000000+SRAM_NX_SIZE)); //   Data SRAM (non-executable)
assign dec_1hot[2]  = (addr>=32'h10040000) & (addr<(32'h10050000             )); //   64KB AHB-to-APB bridge (board I/O, GPIOs)
assign dec_1hot[3]  = (addr>=32'h02000000) & (addr<(32'h02010000             )); //   64KB ACLINT (MSWI/MTIMER/SSWI)
assign dec_1hot[4]  = (addr>=32'h10050000) & (addr<(32'h10051000             )); //   4KB  Belenos registers
assign dec_1hot[5]  = (addr>=32'h82000000) & (addr<(32'h82000000+VRAM_SIZE   )); //   Belenos VRAM window
assign dec_1hot[6]  = (addr>=32'h0C000000) & (addr<(32'h0C400000             )); //   4MB  PLIC


endmodule

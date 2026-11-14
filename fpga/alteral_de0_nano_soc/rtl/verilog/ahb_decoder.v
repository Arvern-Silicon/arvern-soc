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

assign decoder_1hot_o[0]  = (decoder_addr_i>=32'h20000000) & (decoder_addr_i<(32'h20000000+ROM_SIZE    )); //   Program SRAM (writable, executable)
assign decoder_1hot_o[1]  = (decoder_addr_i>=32'h80000000) & (decoder_addr_i<(32'h80000000+SRAM_NX_SIZE)); //   Data SRAM (non-executable)
assign decoder_1hot_o[2]  = (decoder_addr_i>=32'h10040000) & (decoder_addr_i<(32'h10050000             )); //   64KB AHB-to-APB bridge (board I/O, GPIOs)
assign decoder_1hot_o[3]  = (decoder_addr_i>=32'h02000000) & (decoder_addr_i<(32'h02010000             )); //   64KB ACLINT (MSWI/MTIMER/SSWI)
assign decoder_1hot_o[4]  = (decoder_addr_i>=32'h10050000) & (decoder_addr_i<(32'h10051000             )); //   4KB  Belenos registers
assign decoder_1hot_o[5]  = (decoder_addr_i>=32'h82000000) & (decoder_addr_i<(32'h82000000+VRAM_SIZE   )); //   Belenos VRAM window
assign decoder_1hot_o[6]  = (decoder_addr_i>=32'h0C000000) & (decoder_addr_i<(32'h0C400000             )); //   4MB  PLIC


endmodule

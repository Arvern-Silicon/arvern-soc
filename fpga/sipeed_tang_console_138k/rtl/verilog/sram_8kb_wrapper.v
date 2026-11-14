//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    sram_8kb_wrapper
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : sram_8kb_wrapper.v
// Module Description : 8KB SRAM bank (block RAM): 2048 x 32, byte write
//                      enables, one-cycle read latency.
//----------------------------------------------------------------------------

module sram_8kb_wrapper (

// OUTPUTs
    dout_o,                          // Data output

// INPUTs
    addr_i,                          // Address
    cen_i,                           // Chip enable (low active)
    clk_i,                           // Clock
    din_i,                           // Data input
    rst_i,                           // Reset (low active)
    wen_i                            // Byte write enable (low active)
);

// PARAMETERs
//============
parameter ADDRW     = 11;            // Address width: 2048 words = 8KB
parameter INIT_FILE = "";            // Optional $readmemh image

// OUTPUTs
//============
output       [31:0] dout_o;          // Data output

// INPUTs
//============
input   [ADDRW-1:0] addr_i;          // Address
input               cen_i;           // Chip enable (low active)
input               clk_i;           // Clock
input        [31:0] din_i;           // Data input
input               rst_i;           // Reset (low active)
input         [3:0] wen_i;           // Byte write enable (low active)

spram_be #(.ADDRW(ADDRW), .WORDS(1 << ADDRW), .BYTES(4), .INIT_FILE(INIT_FILE)) u_sram (
    .clk_i  ( clk_i  ),
    .cen_i  ( cen_i  ),
    .wen_i  ( wen_i  ),
    .addr_i ( addr_i ),
    .din_i  ( din_i  ),
    .dout_o ( dout_o )
);

wire rst_unused = rst_i;             // no output register to reset

endmodule // sram_8kb_wrapper

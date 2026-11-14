//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    pmem_bram
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : pmem_bram.v
// Module Description : Writable PROGRAM MEMORY (block RAM): MEM_BANKS (up to
//                      36) 2048-word banks, each booting its part of the
//                      firmware image (pmem_bank<n>.mem, n = 0-9 then A-Z,
//                      written from pmem.mem by
//                      sim/rtl_sim/bin/mem_banks.py, also loaded at
//                      configuration time), writable through the bus so a
//                      debugger can load new firmware at 0x20000000. The bank
//                      of a read is registered with it, so the read mux stays
//                      shallow.
//----------------------------------------------------------------------------

module pmem_bram (

// OUTPUTs
    pmem_dout_o,                    // Data output

// INPUTs
    pmem_addr_i,                    // Address
    pmem_cen_i,                     // Chip enable (low active)
    pmem_clk_i,                     // Clock
    pmem_rst_i,                     // Output buffer reset
    pmem_din_i,                     // Data input
    pmem_wen_i                      // Byte write enable (low active)
);

// PARAMETERs
//============
parameter MEM_ADDRW    =  15;           // Width of the address bus
parameter MEM_BANKS    =  12;           // 8KB banks instantiated (<= 2^(MEM_ADDRW-11), <= 36)
parameter ASYNC_RST_EN = 1'b1;          // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

// OUTPUTs
//============
output         [31:0] pmem_dout_o;   // Data output

// INPUTs
//============
input [MEM_ADDRW-1:0] pmem_addr_i;   // Address
input                 pmem_cen_i;    // Chip enable (low active)
input                 pmem_clk_i;    // Clock
input                 pmem_rst_i;    // Output buffer reset
input          [31:0] pmem_din_i;    // Data input
input           [3:0] pmem_wen_i;    // Byte write enable (low active)

localparam BANK_AW = 11;                                   // 2048 words per bank
localparam BANKS   = MEM_BANKS;
localparam SEL_W   = MEM_ADDRW - BANK_AW;

wire  [SEL_W-1:0] bank = pmem_addr_i[MEM_ADDRW-1:BANK_AW];
reg   [BANKS-1:0] sel_q;                                   // bank of the last read
wire       [31:0] bank_q [0:BANKS-1];

genvar gb;
generate
    for (gb = 0; gb < BANKS; gb = gb + 1) begin : g_bank
        spram_be #(.ADDRW(BANK_AW), .WORDS(1 << BANK_AW), .BYTES(4),
                   .INIT_FILE({"pmem_bank", (gb < 10) ? (8'd48 + gb[7:0]) : (8'd55 + gb[7:0]), ".mem"})) u_pmem (
            .clk_i  ( pmem_clk_i                                   ),
            .cen_i  ( pmem_cen_i | (bank != gb[SEL_W-1:0])         ),
            .wen_i  ( pmem_wen_i                                   ),
            .addr_i ( pmem_addr_i[BANK_AW-1:0]                     ),
            .din_i  ( pmem_din_i                                   ),
            .dout_o ( bank_q[gb]                                   )
        );
    end
endgenerate

integer b;
always @(posedge pmem_clk_i)
    if (~pmem_cen_i & (&pmem_wen_i))
        for (b = 0; b < BANKS; b = b + 1)
            sel_q[b] <= (bank == b[SEL_W-1:0]);

reg [31:0] rd_mux;
always @(*) begin
    rd_mux = 32'h00000000;
    for (b = 0; b < BANKS; b = b + 1)
        rd_mux = rd_mux | ({32{sel_q[b]}} & bank_q[b]);
end
assign pmem_dout_o = rd_mux;

wire pmem_rst_unused  = pmem_rst_i;                // no output register to reset
wire async_rst_unused = ASYNC_RST_EN;

endmodule // pmem_bram

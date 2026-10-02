//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    sram_8kb_wrapper (behavioral)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : sram_8kb_wrapper.v
// Module Description : BEHAVIORAL technology-independent stand-in for the 8KB
//                      single-port SRAM macro wrapper. NOT for synthesis.
//
//   The real wrapper is a foundry memory macro supplied by the synthesis
//   library setup ($SRAM_VERILOG_WRAPPER) and is intentionally NOT in
//   rtl/verilog/filelist.f. This model provides the same port interface so the
//   design can be elaborated for lint (and simulated) without a technology
//   library. It matches the macro's timing shape: a synchronous single-port RAM
//   with a REGISTERED read output and byte write-enables.
//----------------------------------------------------------------------------

module sram_8kb_wrapper (

// OUTPUTs
    dout_o,                          // Data output (registered)

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
parameter INIT_FILE = "UNUSED";      // Optional hex file for memory initialization

// PORTs
//============
output reg    [31:0] dout_o;         // Data output (registered)
input    [ADDRW-1:0] addr_i;         // Address
input                cen_i;          // Chip enable (low active)
input                clk_i;          // Clock
input         [31:0] din_i;          // Data input
input                rst_i;          // Reset (low active)
input          [3:0] wen_i;          // Byte write enable (low active)

// Storage
//============
reg [31:0] mem [0:(1<<ADDRW)-1];

// Optional preload (e.g. firmware image into a ROM instance) for simulation.
initial begin
    if (INIT_FILE != "UNUSED") $readmemh(INIT_FILE, mem);
end

// Synchronous single-port RAM with a registered read output (matches the
// macro's internal output register). Byte-enabled write; read-first on a
// same-address write (the nonblocking read captures the pre-write word).
always @(posedge clk_i) begin
    if (!cen_i) begin
        if (!wen_i[0]) mem[addr_i][ 7: 0] <= din_i[ 7: 0];
        if (!wen_i[1]) mem[addr_i][15: 8] <= din_i[15: 8];
        if (!wen_i[2]) mem[addr_i][23:16] <= din_i[23:16];
        if (!wen_i[3]) mem[addr_i][31:24] <= din_i[31:24];
        dout_o <= mem[addr_i];
    end
end

// rst_i models the macro's output-buffer reset; not needed in this behavioral view.
wire rst_i_unused = rst_i;

endmodule // sram_8kb_wrapper

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ram_32x38400
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ram_32x38400.v
// Module Description : Belenos VRAM: WORDS x 32, byte write enables, one-cycle
//                      read latency. Built from 2048-word banks (each maps to
//                      block RAM with one 2K x 9 block per byte lane) and a
//                      one-hot read select registered with the access, so the
//                      read mux stays shallow. Same ports as the DE0 build.
//----------------------------------------------------------------------------

module  ram_32x38400 #(
    parameter               ADDRW = 16,      // address width
    parameter               WORDS = 38400    // depth in 32-bit words (<= 2^ADDRW)
) (
    input  wire             clk_i,           // clock
    input  wire             cen_i,           // chip enable (active low)
    input  wire       [3:0] wen_i,           // byte write enables (active low, all 1 = read)
    input  wire [ADDRW-1:0] addr_i,          // word address
    input  wire      [31:0] din_i,           // write data
    output wire      [31:0] dout_o           // read data
);

localparam BANK_AW = 11;                                    // 2048 words per bank
localparam BANKS   = (WORDS + (1 << BANK_AW) - 1) >> BANK_AW;
localparam SEL_W   = (ADDRW > BANK_AW) ? (ADDRW - BANK_AW) : 1;

wire [SEL_W-1:0] bank   = (ADDRW > BANK_AW) ? addr_i[ADDRW-1:BANK_AW] : {SEL_W{1'b0}};
reg  [BANKS-1:0] sel_q;                                     // bank of the last read
wire     [31:0]  bank_q [0:BANKS-1];

genvar gb;
generate
    for (gb = 0; gb < BANKS; gb = gb + 1) begin : g_bank
        spram_be #(.ADDRW(BANK_AW), .WORDS(1 << BANK_AW), .BYTES(4)) u_ram (
            .clk_i  ( clk_i                                ),
            .cen_i  ( cen_i | (bank != gb[SEL_W-1:0])      ),
            .wen_i  ( wen_i                                ),
            .addr_i ( addr_i[BANK_AW-1:0]                  ),
            .din_i  ( din_i                                ),
            .dout_o ( bank_q[gb]                           )
        );
    end
endgenerate

integer b;
always @(posedge clk_i)
    if (~cen_i & (&wen_i))
        for (b = 0; b < BANKS; b = b + 1)
            sel_q[b] <= (bank == b[SEL_W-1:0]);

reg [31:0] rd_mux;
always @(*) begin
    rd_mux = 32'h00000000;
    for (b = 0; b < BANKS; b = b + 1)
        rd_mux = rd_mux | ({32{sel_q[b]}} & bank_q[b]);
end
assign dout_o = rd_mux;

endmodule // ram_32x38400

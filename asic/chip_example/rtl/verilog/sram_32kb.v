//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    sram_32kb
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : sram_32kb.v
// Module Description : 32KB SRAM using technology-independent sram_8kb_wrapper
//                      The sram_8kb_wrapper module is provided by each technology
//                      library with identical boundaries.
//----------------------------------------------------------------------------

module sram_32kb (

// OUTPUTs
    sram_dout_o,                     // SRAM data output

// INPUTs
    sram_addr_i,                     // SRAM address
    sram_cen_i,                      // SRAM chip enable (low active)
    sram_clk_i,                      // SRAM clock
    sram_rst_i,                      // SRAM output buffer reset
    sram_din_i,                      // SRAM data input
    sram_wen_i                       // SRAM write enable (low active)
);

// PARAMETERs
//============
parameter MEM_ADDRW   =  13;         // Width of the address bus

// OUTPUTs
//============
output         [31:0] sram_dout_o;   // SRAM data output

// INPUTs
//============
input [MEM_ADDRW-1:0] sram_addr_i;   // SRAM address
input                 sram_cen_i;    // SRAM chip enable (low active)
input                 sram_clk_i;    // SRAM clock
input                 sram_rst_i;    // SRAM output buffer reset
input          [31:0] sram_din_i;    // SRAM data input
input           [3:0] sram_wen_i;    // SRAM write enable (low active)


// SRAM INSTANCES
//================

    localparam SRAM_ADDRW = 11;                  // 2048 words (8KB) per wrapper instance
    localparam N          = 1 << (MEM_ADDRW - SRAM_ADDRW);

    wire [31:0] sram_dout [0:N-1];

    genvar i;
    generate
        for (i = 0; i < N; i=i+1) begin : sram_gen
            sram_8kb_wrapper u_sram (
                .dout_o     ( sram_dout[i]           ),
                .din_i      ( sram_din_i             ),
                .addr_i     ( sram_addr_i[SRAM_ADDRW-1:0] ),
                .cen_i      ( sram_cen_i | (sram_addr_i[MEM_ADDRW-1:SRAM_ADDRW] != i[MEM_ADDRW-SRAM_ADDRW-1:0]) ),
                .wen_i      ( sram_wen_i             ),
                .clk_i      ( sram_clk_i             ),
                .rst_i      ( sram_rst_i             )
            );
        end
    endgenerate

    // Bank select registered to match macro's internal output register
    reg [MEM_ADDRW-SRAM_ADDRW-1:0] sram_bank_sel;
    always @(posedge sram_clk_i or negedge sram_rst_i)
        if (!sram_rst_i)       sram_bank_sel <= {(MEM_ADDRW-SRAM_ADDRW){1'b0}};
        else if (~sram_cen_i)  sram_bank_sel <= sram_addr_i[MEM_ADDRW-1:SRAM_ADDRW];

    assign sram_dout_o = sram_dout[sram_bank_sel];


endmodule // sram_32kb

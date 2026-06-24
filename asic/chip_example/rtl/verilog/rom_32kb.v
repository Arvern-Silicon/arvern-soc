//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    rom_32kb
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : rom_32kb.v
// Module Description : 32KB ROM using technology-independent sram_8kb_wrapper
//                      (SRAM wrapper with write ports tied off)
//                      The sram_8kb_wrapper module is provided by each technology
//                      library with identical boundaries.
//----------------------------------------------------------------------------

module rom_32kb (

// OUTPUTs
    rom_dout_o,                     // ROM data output

// INPUTs
    rom_addr_i,                     // ROM address
    rom_cen_i,                      // ROM chip enable (low active)
    rom_clk_i,                      // ROM clock
    rom_rst_i                       // ROM output buffer reset
);

// PARAMETERs
//============
parameter MEM_ADDRW   =  13;          // Width of the address bus

// OUTPUTs
//============
output         [31:0] rom_dout_o;    // ROM data output

// INPUTs
//============
input [MEM_ADDRW-1:0] rom_addr_i;    // ROM address
input                 rom_cen_i;     // ROM chip enable (low active)
input                 rom_clk_i;     // ROM clock
input                 rom_rst_i;     // ROM output buffer reset


// ROM INSTANCES
//================

    localparam ROM_ADDRW = 11;                  // 2048 words (8KB) per wrapper instance
    localparam N         = 1 << (MEM_ADDRW - ROM_ADDRW);

    wire [31:0] rom_dout [0:N-1];

    genvar i;
    generate
        for (i = 0; i < N; i=i+1) begin : sram_gen
            sram_8kb_wrapper u_sram (
                .dout_o     ( rom_dout[i]            ),
                .din_i      ( 32'h00000000           ),
                .addr_i     ( rom_addr_i[ROM_ADDRW-1:0] ),
                .cen_i      ( rom_cen_i | (rom_addr_i[MEM_ADDRW-1:ROM_ADDRW] != i[MEM_ADDRW-ROM_ADDRW-1:0]) ),
                .wen_i      ( 4'b1111                ),
                .clk_i      ( rom_clk_i              ),
                .rst_i      ( rom_rst_i              )
            );
        end
    endgenerate

    // Bank select registered to match macro's internal output register
    reg [MEM_ADDRW-ROM_ADDRW-1:0] rom_bank_sel;
    always @(posedge rom_clk_i or negedge rom_rst_i)
        if (!rom_rst_i)       rom_bank_sel <= {(MEM_ADDRW-ROM_ADDRW){1'b0}};
        else if (~rom_cen_i)  rom_bank_sel <= rom_addr_i[MEM_ADDRW-1:ROM_ADDRW];

    assign rom_dout_o = rom_dout[rom_bank_sel];


endmodule // rom_32kb

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    dpi_line_buffer
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : dpi_line_buffer.v
// Module Description : Belenos DPI line buffers: NB banks of 2^AW x 24, one
//                      write port on wclk_i (shared address and data, a write
//                      enable per bank) and one read port on rclk_i (shared
//                      address), read data one cycle after the address. The
//                      write side is Belenos hclk, the read side its pixel
//                      domain (hclk too with DPI_PCLK_SEP = 0). Inferred
//                      simple dual-port block RAMs.
//----------------------------------------------------------------------------

module  dpi_line_buffer #(
    parameter NB = 2,                        // banks
    parameter AW = 10                        // address width
) (
    input  wire             wclk_i,          // write clock (hclk)
    input  wire             rclk_i,          // read clock (the pixel-domain clock)
    input  wire    [NB-1:0] we_i,            // write enables (active high), one per bank
    input  wire    [AW-1:0] waddr_i,         // write address
    input  wire      [23:0] wdata_i,         // write data
    input  wire    [AW-1:0] raddr_i,         // read address
    output wire [NB*24-1:0] rdata_o          // read data, bank b in [24*b+23:24*b]
);

genvar gb;
generate
    for (gb = 0; gb < NB; gb = gb + 1) begin : g_bank
        reg [23:0] mem [0:(1<<AW)-1];
        reg [23:0] q;
        always @(posedge wclk_i)
            if (we_i[gb]) mem[waddr_i] <= wdata_i;
        always @(posedge rclk_i)
            q <= mem[raddr_i];
        assign rdata_o[gb*24 +: 24] = q;
    end
endgenerate

endmodule // dpi_line_buffer

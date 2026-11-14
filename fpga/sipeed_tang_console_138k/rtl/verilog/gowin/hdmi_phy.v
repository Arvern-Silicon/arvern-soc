//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    hdmi_phy
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : hdmi_phy.v
// Module Description : HDMI (DVI) output lanes of the Tang Console J13: the
//                      three TMDS data lanes from Belenos (DIF_TYPE 5) and the
//                      clock lane (0b1111100000), each through a Gowin 10:1
//                      serialiser (OSER10: parallel clock pclk_i, DDR bit
//                      clock fclk_i = 5 x pclk_i, D0 sent first) and an
//                      emulated LVDS output buffer. With EN = 0 the lanes
//                      are held at a constant level (LCD build); with en_i
//                      low every lane, the clock lane included, sends 0, so
//                      the sink sees the link go away and come back. en_i
//                      comes from the register side: two pclk_i flops
//                      synchronise it.
//----------------------------------------------------------------------------
module  hdmi_phy #(
    parameter         EN = 1                // 1 = serialisers present
) (
    input  wire        pclk_i,              // pixel clock (the Belenos pixel domain)
    input  wire        fclk_i,              // 5 x pclk_i; pclk_i = fclk_i / 5 through a CLKDIV
    input  wire        rst_i,               // serialiser reset (active high, pclk_i domain)
    input  wire        en_i,                // 1 = link on, 0 = every lane sends 0 (asynchronous)
    input  wire [29:0] tmds_i,              // {red, green, blue} TMDS symbols
    output wire  [2:0] d_p_o,               // data lanes 0..2 (blue, green, red)
    output wire  [2:0] d_n_o,
    output wire        clk_p_o,             // clock lane
    output wire        clk_n_o
);

reg   [1:0] en_q;
always @(posedge pclk_i) en_q <= {en_q[0], en_i};

wire [39:0] sym = {10'b1111100000, tmds_i} & {40{en_q[1]}};  // lane 3 = clock
wire  [3:0] ser;

genvar gl;
generate
    for (gl = 0; gl < 4; gl = gl + 1) begin : g_lane
        if (EN != 0) begin : g_ser
            OSER10 u_ser (
                .Q     ( ser[gl]            ),
                .D0    ( sym[gl*10 + 0]     ),
                .D1    ( sym[gl*10 + 1]     ),
                .D2    ( sym[gl*10 + 2]     ),
                .D3    ( sym[gl*10 + 3]     ),
                .D4    ( sym[gl*10 + 4]     ),
                .D5    ( sym[gl*10 + 5]     ),
                .D6    ( sym[gl*10 + 6]     ),
                .D7    ( sym[gl*10 + 7]     ),
                .D8    ( sym[gl*10 + 8]     ),
                .D9    ( sym[gl*10 + 9]     ),
                .PCLK  ( pclk_i             ),
                .FCLK  ( fclk_i             ),
                .RESET ( rst_i              )
            );
        end else begin : g_idle
            assign ser[gl] = 1'b0;
        end
    end
endgenerate

generate
    if (EN == 0) begin : g_unused
        wire [42:0] in_unused = {pclk_i, fclk_i, rst_i, sym};
    end
endgenerate

ELVDS_OBUF u_buf_d0  ( .I ( ser[0] ), .O ( d_p_o[0] ), .OB ( d_n_o[0] ) );
ELVDS_OBUF u_buf_d1  ( .I ( ser[1] ), .O ( d_p_o[1] ), .OB ( d_n_o[1] ) );
ELVDS_OBUF u_buf_d2  ( .I ( ser[2] ), .O ( d_p_o[2] ), .OB ( d_n_o[2] ) );
ELVDS_OBUF u_buf_clk ( .I ( ser[3] ), .O ( clk_p_o  ), .OB ( clk_n_o  ) );

endmodule // hdmi_phy

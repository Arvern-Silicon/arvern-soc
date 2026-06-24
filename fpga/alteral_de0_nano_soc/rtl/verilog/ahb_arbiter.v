//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ahb_arbiter
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ahb_arbiter.v
// Module Description : AHB Arbiter Example for two masters
//----------------------------------------------------------------------------

module  ahb_arbiter #(
    parameter ASYNC_RST_EN = 1'b1   // Reset architecture: 1=async active-low reset (default), 0=synchronous reset
) (

// AHB CLOCK & RESET
    input  wire       hclk_i,
    input  wire       hresetn_i,

// ARBITER INTERFACES
    input  wire [1:0] request_i,
    output wire [1:0] grant_o
);


//=============================================================================
// 1)  INTERNAL WIRES/REGISTERS/PARAMETERS DECLARATION
//=============================================================================

wire            last_grant;
wire            last_grant_nxt;


//=============================================================================
// 2)  ARBITER LOGIC
//=============================================================================

assign          grant_o        =  (request_i == 2'b01) ? { 1'b0,       1'b1      }  :
                                 ((request_i == 2'b10) ? { 1'b1,       1'b0      }  :
                                 ((request_i == 2'b11) ? {~last_grant, last_grant}  :
                                                         { 1'b0,       1'b0      }));

assign          last_grant_nxt =  (request_i == 2'b01) ?   1'b0        :
                                 ((request_i == 2'b10) ?   1'b1        :
                                 ((request_i == 2'b11) ?  ~last_grant  :
                                                           last_grant));

arv_ipdff #(.WIDTH(1), .RST_VAL(1'b1), .ARST_EN(ASYNC_RST_EN)) u_last_grant (
                                        .clk_i(hclk_i), .rst_n_i(hresetn_i), .en_i(1'b1),
                                                                             .d_i (last_grant_nxt),
                                                                             .q_o (last_grant));

endmodule

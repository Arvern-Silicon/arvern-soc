//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    PLL
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : PLL.v
// Module Description : Behavioural model of the Gowin GW5A PLL primitive, for
//                      simulation and lint; the Gowin tools supply the
//                      primitive itself. Models the static configuration only:
//                      CLKOUT0-CLKOUT2 at f_in * (MDIV_SEL + MDIV_FRAC_SEL / 8)
//                      * FBDIV_SEL / (IDIV_SEL * ODIVn_SEL), CLKOUT1/CLKOUT2 delayed by
//                      CLKOUTn_PE_COARSE VCO periods, LOCK after 16 input
//                      cycles. The input period is measured, not read from
//                      FCLKIN. The other outputs stay low.
//----------------------------------------------------------------------------

module PLL #(
    parameter FCLKIN = "50",
    parameter IDIV_SEL = 1,
    parameter FBDIV_SEL = 1,
    parameter ODIV0_SEL = 60,
    parameter ODIV1_SEL = 60,
    parameter ODIV2_SEL = 27,
    parameter ODIV3_SEL = 27,
    parameter ODIV4_SEL = 8,
    parameter ODIV5_SEL = 8,
    parameter ODIV6_SEL = 8,
    parameter MDIV_SEL = 36,
    parameter MDIV_FRAC_SEL = 0,
    parameter ODIV0_FRAC_SEL = 0,
    parameter CLKOUT0_EN = "TRUE",
    parameter CLKOUT1_EN = "TRUE",
    parameter CLKOUT2_EN = "FALSE",
    parameter CLKOUT3_EN = "FALSE",
    parameter CLKOUT4_EN = "FALSE",
    parameter CLKOUT5_EN = "FALSE",
    parameter CLKOUT6_EN = "FALSE",
    parameter CLKFB_SEL = "INTERNAL",
    parameter CLKOUT0_DT_DIR = 1'b1,
    parameter CLKOUT1_DT_DIR = 1'b1,
    parameter CLKOUT2_DT_DIR = 1'b1,
    parameter CLKOUT3_DT_DIR = 1'b1,
    parameter CLKOUT0_DT_STEP = 0,
    parameter CLKOUT1_DT_STEP = 0,
    parameter CLKOUT2_DT_STEP = 0,
    parameter CLKOUT3_DT_STEP = 0,
    parameter CLK0_IN_SEL = 1'b0,
    parameter CLK0_OUT_SEL = 1'b0,
    parameter CLK1_IN_SEL = 1'b0,
    parameter CLK1_OUT_SEL = 1'b0,
    parameter CLK2_IN_SEL = 1'b0,
    parameter CLK2_OUT_SEL = 1'b0,
    parameter CLK3_IN_SEL = 1'b0,
    parameter CLK3_OUT_SEL = 1'b0,
    parameter CLK4_IN_SEL = 2'b00,
    parameter CLK4_OUT_SEL = 1'b0,
    parameter CLK5_IN_SEL = 1'b0,
    parameter CLK5_OUT_SEL = 1'b0,
    parameter CLK6_IN_SEL = 1'b0,
    parameter CLK6_OUT_SEL = 1'b0,
    parameter DYN_DPA_EN = "FALSE",
    parameter CLKOUT0_PE_COARSE = 0,
    parameter CLKOUT0_PE_FINE = 0,
    parameter CLKOUT1_PE_COARSE = 30,
    parameter CLKOUT1_PE_FINE = 0,
    parameter CLKOUT2_PE_COARSE = 0,
    parameter CLKOUT2_PE_FINE = 0,
    parameter CLKOUT3_PE_COARSE = 16,
    parameter CLKOUT3_PE_FINE = 7,
    parameter CLKOUT4_PE_COARSE = 0,
    parameter CLKOUT4_PE_FINE = 0,
    parameter CLKOUT5_PE_COARSE = 0,
    parameter CLKOUT5_PE_FINE = 0,
    parameter CLKOUT6_PE_COARSE = 0,
    parameter CLKOUT6_PE_FINE = 0,
    parameter DYN_PE0_SEL = "FALSE",
    parameter DYN_PE1_SEL = "FALSE",
    parameter DYN_PE2_SEL = "FALSE",
    parameter DYN_PE3_SEL = "FALSE",
    parameter DYN_PE4_SEL = "FALSE",
    parameter DYN_PE5_SEL = "FALSE",
    parameter DYN_PE6_SEL = "FALSE",
    parameter DE0_EN = "FALSE",
    parameter DE1_EN = "FALSE",
    parameter DE2_EN = "FALSE",
    parameter DE3_EN = "FALSE",
    parameter DE4_EN = "FALSE",
    parameter DE5_EN = "FALSE",
    parameter DE6_EN = "FALSE",
    parameter RESET_I_EN = "FALSE",
    parameter RESET_O_EN = "FALSE",
    parameter ICP_SEL = 6'b000000,
    parameter LPF_RES = 3'b000,
    parameter LPF_CAP = 2'b00,
    parameter SSC_EN = "FALSE",
    parameter DYN_IDIV_SEL = "FALSE",
    parameter DYN_FBDIV_SEL = "FALSE",
    parameter DYN_MDIV_SEL = "FALSE",
    parameter DYN_ODIV0_SEL = "FALSE",
    parameter DYN_ODIV1_SEL = "FALSE",
    parameter DYN_ODIV2_SEL = "FALSE",
    parameter DYN_ODIV3_SEL = "FALSE",
    parameter DYN_ODIV4_SEL = "FALSE",
    parameter DYN_ODIV5_SEL = "FALSE",
    parameter DYN_ODIV6_SEL = "FALSE",
    parameter DYN_DT0_SEL = "FALSE",
    parameter DYN_DT1_SEL = "FALSE",
    parameter DYN_DT2_SEL = "FALSE",
    parameter DYN_DT3_SEL = "FALSE",
    parameter DYN_ICP_SEL = "FALSE",
    parameter DYN_LPF_SEL = "FALSE"
) (
    output wire        LOCK,
    output wire        CLKOUT0,
    output wire        CLKOUT1,
    output wire        CLKOUT2,
    output wire        CLKOUT3,
    output wire        CLKOUT4,
    output wire        CLKOUT5,
    output wire        CLKOUT6,
    output wire        CLKFBOUT,
    input  wire        CLKIN,
    input  wire        CLKFB,
    input  wire        RESET,
    input  wire        PLLPWD,
    input  wire        RESET_I,
    input  wire        RESET_O,
    input  wire [5:0]  FBDSEL,
    input  wire [5:0]  IDSEL,
    input  wire [6:0]  MDSEL,
    input  wire [2:0]  MDSEL_FRAC,
    input  wire [6:0]  ODSEL0,
    input  wire [2:0]  ODSEL0_FRAC,
    input  wire [6:0]  ODSEL1,
    input  wire [6:0]  ODSEL2,
    input  wire [6:0]  ODSEL3,
    input  wire [6:0]  ODSEL4,
    input  wire [6:0]  ODSEL5,
    input  wire [6:0]  ODSEL6,
    input  wire [3:0]  DT0,
    input  wire [3:0]  DT1,
    input  wire [3:0]  DT2,
    input  wire [3:0]  DT3,
    input  wire [5:0]  ICPSEL,
    input  wire [2:0]  LPFRES,
    input  wire [1:0]  LPFCAP,
    input  wire [2:0]  PSSEL,
    input  wire        PSDIR,
    input  wire        PSPULSE,
    input  wire        ENCLK0,
    input  wire        ENCLK1,
    input  wire        ENCLK2,
    input  wire        ENCLK3,
    input  wire        ENCLK4,
    input  wire        ENCLK5,
    input  wire        ENCLK6,
    input  wire        SSCPOL,
    input  wire        SSCON,
    input  wire [6:0]  SSCMDSEL,
    input  wire [2:0]  SSCMDSEL_FRAC
);

realtime t_in;
realtime t_vco;
reg      clk0;
reg      clk1;
reg      clk2;
reg      lock_q;
integer  n_in;

initial begin
    clk0   = 1'b0;
    clk1   = 1'b0;
    clk2   = 1'b0;
    lock_q = 1'b0;
    n_in   = 0;
    @(posedge CLKIN); t_in = $realtime;
    @(posedge CLKIN); t_in = $realtime - t_in;
    t_vco = t_in * IDIV_SEL / ((MDIV_SEL + MDIV_FRAC_SEL / 8.0) * FBDIV_SEL);
    fork
        forever begin
            clk0 = 1'b1; #(t_vco * ODIV0_SEL / 2.0);
            clk0 = 1'b0; #(t_vco * ODIV0_SEL / 2.0);
        end
        begin
            #(t_vco * CLKOUT1_PE_COARSE);
            forever begin
                clk1 = 1'b1; #(t_vco * ODIV1_SEL / 2.0);
                clk1 = 1'b0; #(t_vco * ODIV1_SEL / 2.0);
            end
        end
        begin
            #(t_vco * CLKOUT2_PE_COARSE);
            forever begin
                clk2 = 1'b1; #(t_vco * ODIV2_SEL / 2.0);
                clk2 = 1'b0; #(t_vco * ODIV2_SEL / 2.0);
            end
        end
    join
end

always @(posedge CLKIN)
    if (n_in < 16) n_in <= n_in + 1;
    else           lock_q <= 1'b1;

assign LOCK     = lock_q;
assign CLKOUT0  = clk0 & (CLKOUT0_EN == "TRUE");
assign CLKOUT1  = clk1 & (CLKOUT1_EN == "TRUE");
assign CLKOUT2  = clk2 & (CLKOUT2_EN == "TRUE");
assign CLKOUT3  = 1'b0;
assign CLKOUT4  = 1'b0;
assign CLKOUT5  = 1'b0;
assign CLKOUT6  = 1'b0;
assign CLKFBOUT = 1'b0;

endmodule // PLL

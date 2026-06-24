//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    chip_example
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : chip_example.v
// Module Description : Chip example for arvern core synthesis trials
//                      The purpose of the chip example is only to perform
//                      synthesis and implementation trials with the arvern
//                      core together with the AHB Interconnect and SRAM/ROM
//                      controllers.
//----------------------------------------------------------------------------

module  chip_example (

// AHB CLOCK & RESET
    input  wire             free_clk_i,
    input  wire             hresetn_i,

// AHB Peripheral #0
    output wire      [31:0] periph0_reg_00_o,
    output wire      [31:0] periph0_reg_01_o,
    output wire      [31:0] periph0_reg_02_o,
    output wire      [31:0] periph0_reg_03_o,
    output wire      [31:0] periph0_reg_04_o,
    output wire      [31:0] periph0_reg_05_o,
    output wire      [31:0] periph0_reg_06_o,
    output wire      [31:0] periph0_reg_07_o,
    input  wire      [31:0] periph0_reg_08_i,
    input  wire      [31:0] periph0_reg_09_i,
    input  wire      [31:0] periph0_reg_10_i,
    input  wire      [31:0] periph0_reg_11_i,
    input  wire      [31:0] periph0_reg_12_i,
    input  wire      [31:0] periph0_reg_13_i,
    input  wire      [31:0] periph0_reg_14_i,
    input  wire      [31:0] periph0_reg_15_i,

// AHB Peripheral #1
    output wire      [31:0] periph1_reg_00_o,
    output wire      [31:0] periph1_reg_01_o,
    output wire      [31:0] periph1_reg_02_o,
    output wire      [31:0] periph1_reg_03_o,
    output wire      [31:0] periph1_reg_04_o,
    output wire      [31:0] periph1_reg_05_o,
    output wire      [31:0] periph1_reg_06_o,
    output wire      [31:0] periph1_reg_07_o,
    input  wire      [31:0] periph1_reg_08_i,
    input  wire      [31:0] periph1_reg_09_i,
    input  wire      [31:0] periph1_reg_10_i,
    input  wire      [31:0] periph1_reg_11_i,
    input  wire      [31:0] periph1_reg_12_i,
    input  wire      [31:0] periph1_reg_13_i,
    input  wire      [31:0] periph1_reg_14_i,
    input  wire      [31:0] periph1_reg_15_i,

// AHB Peripheral #2
    output wire      [31:0] periph2_reg_00_o,
    output wire      [31:0] periph2_reg_01_o,
    output wire      [31:0] periph2_reg_02_o,
    output wire      [31:0] periph2_reg_03_o,
    output wire      [31:0] periph2_reg_04_o,
    output wire      [31:0] periph2_reg_05_o,
    output wire      [31:0] periph2_reg_06_o,
    output wire      [31:0] periph2_reg_07_o,
    input  wire      [31:0] periph2_reg_08_i,
    input  wire      [31:0] periph2_reg_09_i,
    input  wire      [31:0] periph2_reg_10_i,
    input  wire      [31:0] periph2_reg_11_i,
    input  wire      [31:0] periph2_reg_12_i,
    input  wire      [31:0] periph2_reg_13_i,
    input  wire      [31:0] periph2_reg_14_i,
    input  wire      [31:0] periph2_reg_15_i,

// Read-only CCSR registers
    input  wire      [31:0] ccsr_usr_ro_0_i,
    input  wire      [31:0] ccsr_sup_ro_0_i,
    input  wire      [31:0] ccsr_mac_ro_0_i,
    input  wire      [31:0] ccsr_mac_ro_1_i,

// Read-write CCSR values
    output wire      [31:0] ccsr_usr_rw_0_o,
    output wire      [31:0] ccsr_usr_rw_1_o,
    output wire      [31:0] ccsr_sup_rw_0_o,
    output wire      [31:0] ccsr_sup_rw_1_o,
    output wire      [31:0] ccsr_mac_rw_0_o,
    output wire      [31:0] ccsr_mac_rw_1_o,
    output wire      [31:0] ccsr_mac_rw_2_o,
    output wire      [31:0] ccsr_mac_rw_3_o,
    output wire      [31:0] ccsr_mac_rw_4_o,
    output wire      [31:0] ccsr_mac_rw_5_o,
    output wire      [31:0] ccsr_mac_rw_6_o,
    output wire      [31:0] ccsr_mac_rw_7_o,

// External interrupt inputs (software/timer IRQs are now generated internally by the ACLINT)
    input  wire             irq_m_external_i,
    input  wire             irq_s_external_i,
    input  wire      [15:0] irq_platform_i,

// NMI (Smrnmi)
    input  wire             nmi_i,
    input  wire      [31:0] nmi_vector_i,

// Lockup status
    output wire             lockup_o,

// ACLINT low-frequency clock & reset (always-on MTIME domain)
    input  wire             clk_lf_i,
    input  wire             resetn_lf_i,

// Platform HPM events (Zihpm)
    input  wire       [7:0] hpm_platform_events_i

);


//////======================================================================================================================//////
//////                              INTERNAL WIRES/REGISTERS/PARAMETERS DECLARATION: TRAP-HANDLING REGISTERS                //////
//////======================================================================================================================//////

// Parameters
parameter                ROM_SIZE     = 32*1024;  // Size of the ROM memory instance (in Bytes)
parameter                SRAM_X_SIZE  = 32*1024;  // Size of the Executable SRAM memory instance (in Bytes)
parameter                SRAM_NX_SIZE = 32*1024;  // Size of the Non-executable SRAM memory instance (in Bytes)

// Clock / Reset
wire                     dut_hclk;
wire                     dut_hclk_en;
wire                     system_hclk;
wire                     system_hclk_en;
wire                     ccsr_hclk;
wire                     ccsr_hclk_en;

// ACLINT -> core interrupt & time signals
wire                     aclint_irq_m_software;
wire                     aclint_irq_s_software;
wire                     aclint_irq_m_timer;
wire                     aclint_time_req;
wire                     aclint_time_gnt;
wire              [63:0] aclint_time_val;

// AHB Manager interfaces
wire              [31:0] inst_haddr;
wire               [2:0] inst_hburst;
wire                     inst_hmastlock;
wire               [3:0] inst_hprot;
wire               [2:0] inst_hsize;
wire                     inst_hsmode;
wire               [1:0] inst_htrans;
wire              [31:0] inst_hwdata;
wire                     inst_hwrite;
wire              [31:0] inst_hrdata;
wire                     inst_hready;
wire                     inst_hresp;

wire              [31:0] data_haddr;
wire               [2:0] data_hburst;
wire                     data_hmastlock;
wire               [3:0] data_hprot;
wire               [2:0] data_hsize;
wire                     data_hsmode;
wire               [1:0] data_htrans;
wire              [31:0] data_hwdata;
wire                     data_hwrite;
wire              [31:0] data_hrdata;
wire                     data_hready;
wire                     data_hresp;

// Interface between ORV core and CCSR unit
wire              [10:0] ccsr_bank;
wire              [63:0] ccsr_reg_sel;
wire              [31:0] ccsr_wdata;
wire                     ccsr_wen;
wire              [31:0] ccsr_rdata;


//////======================================================================================================================//////
//////                                                       ARVERN                                                         //////
//////======================================================================================================================//////

parameter   RV32E_EN            =  0;             // Base ISA: 0=RV32I, 1=RV32E
parameter   NMI_EN              =  1;             // Smrnmi resumable NMI extension: 0=absent, 1=present
parameter   SU_MODE_EN          =  1;             // S-mode + U-mode privilege modes
parameter   ZICNTR_EN           =  1;             // Zicntr extension: 0=absent, 1=present (cycle, time, instret)
parameter   ZIHPM_NR            =  1;             // Zihpm: number of HPM counters (0-8)
parameter   B_EXTENSION         =  4;             // Bit manipulation extension: 0=none, 1=Zbb, 2=Zbb+Zba, 3=Zbb+Zba+Zbs, 4=Zbb+Zba+Zbs+Zbc
parameter   C_EXTENSION         =  4;             // Compressed instructions extension: 0=none, 1=Zca, 2=Zca+Zcb, 3=Zca+Zcb+Zcmp, 4=Zca+Zcb+Zcmp+Zcmt
parameter   M_EXTENSION         =  2;             // Integer Multiply/Divide extension: 0=none, 1=Zmmul, 2=M-extension
parameter   MUL_TYPE            =  1;             // Multiplier type: 1=1xCycle,  2=4xCycles,  3=16xCycles (valid only with Zmmul and M-extension)
parameter   DIV_TYPE            =  3;             // Divider type:    1=12xCycle, 2=17xCycles, 3=33xCycles (valid only with M-extension)
parameter   CCSR_EN             =  1;             // Enable Custom-CSR interface
parameter   SINGLE_CYCLE_BRANCH =  1;             // Taken-branch latency: 1=zero-bubble (max IPC, lower Fmax), 0=one-bubble (lower IPC, max Fmax)
parameter   ASYNC_RST_EN        =  1'b1;          // Reset architecture: 1=async active-low reset (default), 0=synchronous reset
parameter   MVENDORID           =  32'h00000000;  // JEDEC manufacturer ID of the chip vendor integrator.

arvern   #(.RV32E_EN            ( RV32E_EN            ),
           .NMI_EN              ( NMI_EN              ),
           .SU_MODE_EN          ( SU_MODE_EN          ),
           .ZICNTR_EN           ( ZICNTR_EN           ),
           .ZIHPM_NR            ( ZIHPM_NR            ),
           .B_EXTENSION         ( B_EXTENSION         ),
           .C_EXTENSION         ( C_EXTENSION         ),
           .M_EXTENSION         ( M_EXTENSION         ),
           .MUL_TYPE            ( MUL_TYPE            ),
           .DIV_TYPE            ( DIV_TYPE            ),
           .CCSR_EN             ( CCSR_EN             ),
           .SINGLE_CYCLE_BRANCH ( SINGLE_CYCLE_BRANCH ),
           .ASYNC_RST_EN        ( ASYNC_RST_EN        ),
           .MVENDORID           ( MVENDORID           )) dut (

// AHB CLOCK & RESET
    .hclk_i                    ( free_clk_i                ),
    .hresetn_i                 ( hresetn_i                 ),
    .hclk_en_o                 ( dut_hclk_en               ),

// INSTRUCTION AHB BUS
    .inst_hrdata_i             ( inst_hrdata               ),
    .inst_hready_i             ( inst_hready               ),
    .inst_hresp_i              ( inst_hresp                ),

    .inst_haddr_o              ( inst_haddr                ),
    .inst_hburst_o             ( inst_hburst               ),
    .inst_hmastlock_o          ( inst_hmastlock            ),
    .inst_hprot_o              ( inst_hprot                ),
    .inst_hsize_o              ( inst_hsize                ),
    .inst_hsmode_o             ( inst_hsmode               ),
    .inst_htrans_o             ( inst_htrans               ),
    .inst_hwdata_o             ( inst_hwdata               ),
    .inst_hwrite_o             ( inst_hwrite               ),

// DATA AHB BUS
    .data_hrdata_i             ( data_hrdata               ),
    .data_hready_i             ( data_hready               ),
    .data_hresp_i              ( data_hresp                ),

    .data_haddr_o              ( data_haddr                ),
    .data_hburst_o             ( data_hburst               ),
    .data_hmastlock_o          ( data_hmastlock            ),
    .data_hprot_o              ( data_hprot                ),
    .data_hsize_o              ( data_hsize                ),
    .data_hsmode_o             ( data_hsmode               ),
    .data_htrans_o             ( data_htrans               ),
    .data_hwdata_o             ( data_hwdata               ),
    .data_hwrite_o             ( data_hwrite               ),

// INTERFACE TO CUSTOM CSR REGISTERS
    .ccsr_rdata_i              ( ccsr_rdata                ),
    .ccsr_bank_o               ( ccsr_bank                 ),
    .ccsr_reg_sel_o            ( ccsr_reg_sel              ),
    .ccsr_wdata_o              ( ccsr_wdata                ),
    .ccsr_wen_o                ( ccsr_wen                  ),

// EXTERNAL INTERRUPT INPUTS
    .irq_m_software_i          ( aclint_irq_m_software     ),
    .irq_s_software_i          ( aclint_irq_s_software     ),
    .irq_m_timer_i             ( aclint_irq_m_timer        ),
    .irq_m_external_i          ( irq_m_external_i          ),
    .irq_s_external_i          ( irq_s_external_i          ),
    .irq_platform_i            ( irq_platform_i            ),

// OTHERS
    .hartid_i                  ( 8'h23                     ),
    .reset_vector_i            ( 32'h20000000              ),

// LOCKUP STATUS
    .lockup_o                  ( lockup_o                  ),

// NMI (SMRNMI)
    .nmi_i                     ( nmi_i                     ),
    .nmi_vector_i              ( nmi_vector_i              ),

// TIME INTERFACE (ZICNTR)
    .time_req_o                ( aclint_time_req           ),
    .time_gnt_i                ( aclint_time_gnt           ),
    .time_val_i                ( aclint_time_val           ),

// PLATFORM EVENTS (ZIHPM)
    .hpm_platform_events_i     ( hpm_platform_events_i     )

);

//////======================================================================================================================//////
//////                                                 CUSTOM CSR REGISTERS                                                 //////
//////======================================================================================================================//////

arv_custom_csr #(.NR_USR_RW(2), .NR_USR_RO(1),
                 .NR_SUP_RW(2), .NR_SUP_RO(1),
                 .NR_MAC_RW(8), .NR_MAC_RO(2),
                 .ASYNC_RST_EN(ASYNC_RST_EN)) arv_custom_csr_inst (

// AHB CLOCK & RESET
    .hclk_i                    ( free_clk_i                                                          ),
    .hresetn_i                 ( hresetn_i                                                           ),
    .hclk_en_o                 ( ccsr_hclk_en                                                        ),

// READ-ONLY VALUES FROM OUTSIDE WORLD
    .ccsr_usr_ro_i             ( {ccsr_usr_ro_0_i}                                                   ),
    .ccsr_sup_ro_i             ( {ccsr_sup_ro_0_i}                                                   ),
    .ccsr_mac_ro_i             ( {ccsr_mac_ro_1_i, ccsr_mac_ro_0_i}                                  ),

// READ-WRITE VALUES TO OUTSIDE WORLD
    .ccsr_usr_rw_o             ( {ccsr_usr_rw_1_o, ccsr_usr_rw_0_o}                                  ),
    .ccsr_sup_rw_o             ( {ccsr_sup_rw_1_o, ccsr_sup_rw_0_o}                                  ),
    .ccsr_mac_rw_o             ( {ccsr_mac_rw_7_o, ccsr_mac_rw_6_o, ccsr_mac_rw_5_o, ccsr_mac_rw_4_o,
                                  ccsr_mac_rw_3_o, ccsr_mac_rw_2_o, ccsr_mac_rw_1_o, ccsr_mac_rw_0_o}),

// INTERFACE TO CUSTOM CSR REGISTERS
    .ccsr_bank_i               ( ccsr_bank                                                           ),
    .ccsr_reg_sel_i            ( ccsr_reg_sel                                                        ),
    .ccsr_wdata_i              ( ccsr_wdata                                                          ),
    .ccsr_wen_i                ( ccsr_wen                                                            ),
    .ccsr_rdata_o              ( ccsr_rdata                                                          )
);

//////======================================================================================================================//////
//////                                                       BUS SYSTEM                                                     //////
//////======================================================================================================================//////

ahb_bus_system #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_bus_system_inst (

// AHB CLOCK & RESET
    .hclk_i                    ( free_clk_i                ),
    .hresetn_i                 ( hresetn_i                 ),
    .hclk_en_o                 ( system_hclk_en            ),

// EXECUTABLE AHB BUS MANAGER INTERFACE
    .m_x_haddr_i               ( inst_haddr                ),
    .m_x_hburst_i              ( inst_hburst               ),
    .m_x_hmastlock_i           ( inst_hmastlock            ),
    .m_x_hprot_i               ( inst_hprot                ),
    .m_x_hsize_i               ( inst_hsize                ),
    .m_x_hsmode_i              ( inst_hsmode               ),
    .m_x_htrans_i              ( inst_htrans               ),
    .m_x_hwdata_i              ( inst_hwdata               ),
    .m_x_hwrite_i              ( inst_hwrite               ),

    .m_x_hrdata_o              ( inst_hrdata               ),
    .m_x_hready_o              ( inst_hready               ),
    .m_x_hresp_o               ( inst_hresp                ),

// NON-EXECUTABLE AHB MANAGER INTERFACE
    .m_nx_haddr_i              ( data_haddr                ),
    .m_nx_hburst_i             ( data_hburst               ),
    .m_nx_hmastlock_i          ( data_hmastlock            ),
    .m_nx_hprot_i              ( data_hprot                ),
    .m_nx_hsize_i              ( data_hsize                ),
    .m_nx_hsmode_i             ( data_hsmode               ),
    .m_nx_htrans_i             ( data_htrans               ),
    .m_nx_hwdata_i             ( data_hwdata               ),
    .m_nx_hwrite_i             ( data_hwrite               ),

    .m_nx_hrdata_o             ( data_hrdata               ),
    .m_nx_hready_o             ( data_hready               ),
    .m_nx_hresp_o              ( data_hresp                ),

// AHB PERIPHERAL #0
    .periph0_reg_08_i          ( periph0_reg_08_i          ),
    .periph0_reg_09_i          ( periph0_reg_09_i          ),
    .periph0_reg_10_i          ( periph0_reg_10_i          ),
    .periph0_reg_11_i          ( periph0_reg_11_i          ),
    .periph0_reg_12_i          ( periph0_reg_12_i          ),
    .periph0_reg_13_i          ( periph0_reg_13_i          ),
    .periph0_reg_14_i          ( periph0_reg_14_i          ),
    .periph0_reg_15_i          ( periph0_reg_15_i          ),

    .periph0_reg_00_o          ( periph0_reg_00_o          ),
    .periph0_reg_01_o          ( periph0_reg_01_o          ),
    .periph0_reg_02_o          ( periph0_reg_02_o          ),
    .periph0_reg_03_o          ( periph0_reg_03_o          ),
    .periph0_reg_04_o          ( periph0_reg_04_o          ),
    .periph0_reg_05_o          ( periph0_reg_05_o          ),
    .periph0_reg_06_o          ( periph0_reg_06_o          ),
    .periph0_reg_07_o          ( periph0_reg_07_o          ),

// AHB PERIPHERAL #1
    .periph1_reg_08_i          ( periph1_reg_08_i          ),
    .periph1_reg_09_i          ( periph1_reg_09_i          ),
    .periph1_reg_10_i          ( periph1_reg_10_i          ),
    .periph1_reg_11_i          ( periph1_reg_11_i          ),
    .periph1_reg_12_i          ( periph1_reg_12_i          ),
    .periph1_reg_13_i          ( periph1_reg_13_i          ),
    .periph1_reg_14_i          ( periph1_reg_14_i          ),
    .periph1_reg_15_i          ( periph1_reg_15_i          ),

    .periph1_reg_00_o          ( periph1_reg_00_o          ),
    .periph1_reg_01_o          ( periph1_reg_01_o          ),
    .periph1_reg_02_o          ( periph1_reg_02_o          ),
    .periph1_reg_03_o          ( periph1_reg_03_o          ),
    .periph1_reg_04_o          ( periph1_reg_04_o          ),
    .periph1_reg_05_o          ( periph1_reg_05_o          ),
    .periph1_reg_06_o          ( periph1_reg_06_o          ),
    .periph1_reg_07_o          ( periph1_reg_07_o          ),

// AHB PERIPHERAL #2
    .periph2_reg_08_i          ( periph2_reg_08_i          ),
    .periph2_reg_09_i          ( periph2_reg_09_i          ),
    .periph2_reg_10_i          ( periph2_reg_10_i          ),
    .periph2_reg_11_i          ( periph2_reg_11_i          ),
    .periph2_reg_12_i          ( periph2_reg_12_i          ),
    .periph2_reg_13_i          ( periph2_reg_13_i          ),
    .periph2_reg_14_i          ( periph2_reg_14_i          ),
    .periph2_reg_15_i          ( periph2_reg_15_i          ),

    .periph2_reg_00_o          ( periph2_reg_00_o          ),
    .periph2_reg_01_o          ( periph2_reg_01_o          ),
    .periph2_reg_02_o          ( periph2_reg_02_o          ),
    .periph2_reg_03_o          ( periph2_reg_03_o          ),
    .periph2_reg_04_o          ( periph2_reg_04_o          ),
    .periph2_reg_05_o          ( periph2_reg_05_o          ),
    .periph2_reg_06_o          ( periph2_reg_06_o          ),
    .periph2_reg_07_o          ( periph2_reg_07_o          ),

// ACLINT CLOCK & RESET
    .clk_lf_i                  ( clk_lf_i                  ),
    .resetn_lf_i               ( resetn_lf_i               ),
    .hclk_aon_i                ( free_clk_i                ),

// ACLINT INTERRUPTS & ZICNTR TIME INTERFACE
    .aclint_irq_m_software_o   ( aclint_irq_m_software     ),
    .aclint_irq_s_software_o   ( aclint_irq_s_software     ),
    .aclint_irq_m_timer_o      ( aclint_irq_m_timer        ),

    .aclint_time_req_i         ( aclint_time_req           ),
    .aclint_time_gnt_o         ( aclint_time_gnt           ),
    .aclint_time_val_o         ( aclint_time_val           )
);


endmodule

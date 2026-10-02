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

// DFT
    input  wire             scan_mode_i,    // 1 = test mode: bypasses the reset synchronisers and clock-as-data paths. Tie 0 functionally

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

// Lockup status
    output wire             lockup_o,

// ACLINT low-frequency clock & reset (always-on MTIME domain)
    input  wire             clk_lf_i,
    input  wire             resetn_lf_i,

// Platform HPM events (Zihpm)
    input  wire       [7:0] hpm_platform_events_i,

// EXTERNAL DEBUG - JTAG Debug Transport Module (only meaningful when DEBUG_EN=1)
    input  wire       [3:0] jtag_idcode_version_i,
    input  wire             jtag_tck_i,
    input  wire             jtag_trst_n_i,
    input  wire             jtag_tms_i,
    input  wire             jtag_tdi_i,
    output wire             jtag_tdo_o,
    output wire             jtag_tdo_oe_o,

// EXTERNAL DEBUG - status / system hooks (all driven 0 when DEBUG_EN=0)
    output wire             dbg_debug_mode_o,
    output wire             dbg_halted_o,
    output wire             dbg_stoptime_o,
    output wire             dbg_ndmreset_o
);


//////======================================================================================================================//////
//////                              INTERNAL WIRES/REGISTERS/PARAMETERS DECLARATION: TRAP-HANDLING REGISTERS                //////
//////======================================================================================================================//////

// Parameters
parameter                ROM_SIZE     = 32*1024;  // Size of the ROM memory instance (in Bytes)
parameter                SRAM_X_SIZE  = 32*1024;  // Size of the Executable SRAM memory instance (in Bytes)
parameter                SRAM_NX_SIZE = 32*1024;  // Size of the Non-executable SRAM memory instance (in Bytes)

// Clock / Reset
// This trial has no clock-gate cell (free_clk_i feeds every block directly), so
// each block's hclk_en_o is captured but unused; see the LINT CLEANUP at the end.
wire                     dut_hclk_en;
wire                     system_hclk_en;
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
wire                     data_hmaster;

// Interface between ORV core and CCSR unit
wire              [10:0] ccsr_bank;
wire              [63:0] ccsr_reg_sel;
wire              [31:0] ccsr_wdata;
wire                     ccsr_wen;
wire              [31:0] ccsr_rdata;

// Debug Module Interface (DMI, APB) - DTM master <-> core DM slave
wire                     dmi_psel;
wire                     dmi_penable;
wire              [8:0]  dmi_paddr;
wire                     dmi_pwrite;
wire              [31:0] dmi_pwdata;
wire              [2:0]  dmi_pprot;
wire                     dmi_pready;
wire              [31:0] dmi_prdata;
wire                     dmi_pslverr;


//////======================================================================================================================//////
//////                                                       ARVERN                                                         //////
//////======================================================================================================================//////

parameter   RV32E_EN            =  0;             // Base ISA: 0=RV32I, 1=RV32E
parameter   SU_MODE_EN          =  1;             // S-mode + U-mode privilege modes
parameter   PMP_NR              = 16;             // Physical Memory Protection: writable entries (0, 4, 8 or 16; 0=absent)
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
parameter   DEBUG_EN            =  1;             // External debug (RISC-V Debug 1.0): 0=absent, 1=present (JTAG DTM instantiated below)
parameter   DM_TRIGGER_NR       =  2;             // Sdtrig hardware triggers (0-8, only meaningful when DEBUG_EN=1)

arvern   #(.RV32E_EN            ( RV32E_EN            ),
           .SU_MODE_EN          ( SU_MODE_EN          ),
           .PMP_NR              ( PMP_NR              ),
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
           .DEBUG_EN            ( DEBUG_EN            ),
           .DM_TRIGGER_NR       ( DM_TRIGGER_NR       )) dut (

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
    .data_hmaster_o            ( data_hmaster              ),
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

// EXTERNAL DEBUG (RISC-V Debug 1.0) - DMI slave + status; inert when DEBUG_EN=0
    .dbgresetn_i               ( hresetn_i                 ),
    .dbg_debug_mode_o          ( dbg_debug_mode_o          ),
    .dbg_halted_o              ( dbg_halted_o              ),
    .dbg_stoptime_o            ( dbg_stoptime_o            ),
    .dbg_ndmreset_o            ( dbg_ndmreset_o            ),
    .dmi_psel_i                ( dmi_psel                  ),
    .dmi_penable_i             ( dmi_penable               ),
    .dmi_paddr_i               ( dmi_paddr                 ),
    .dmi_pwrite_i              ( dmi_pwrite                ),
    .dmi_pwdata_i              ( dmi_pwdata                ),
    .dmi_pprot_i               ( dmi_pprot                 ),
    .dmi_pready_o              ( dmi_pready                ),
    .dmi_prdata_o              ( dmi_prdata                ),
    .dmi_pslverr_o             ( dmi_pslverr               ),

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
// TIME INTERFACE (ZICNTR)
    .time_req_o                ( aclint_time_req           ),
    .time_gnt_i                ( aclint_time_gnt           ),
    .time_val_i                ( aclint_time_val           ),

// PLATFORM EVENTS (ZIHPM)
    .hpm_platform_events_i     ( hpm_platform_events_i     )

);

//////======================================================================================================================//////
//////                                     DEBUG TRANSPORT MODULE (JTAG DTM) - DEBUG_EN only                                //////
//////======================================================================================================================//////

generate
if (DEBUG_EN) begin : g_jtag_dtm

    // Cold-attach wake request: a TCKC/TCK-domain toggle the SoC's always-on
    // controller would use to start a gated oscillator. This chip never gates it.
    wire dbg_wakeup;
    wire dbg_wakeup_unused = dbg_wakeup;

    // JTAG Debug Transport Module. Owns the DMI (APB) master side and drives
    // the core's DM slave. hclk_i MUST be the ungated oscillator (free_clk_i).
    arv_dtm_jtag #(.ARST_EN ( ASYNC_RST_EN )) u_dtm (
        .idcode_version_i ( jtag_idcode_version_i ),
        .scan_mode_i      ( scan_mode_i           ),
        .dbg_wakeup_o     ( dbg_wakeup            ),  // cold-attach wake; unused on this chip
        .tck_i            ( jtag_tck_i            ),
        .trst_n_i         ( jtag_trst_n_i         ),
        .tms_i            ( jtag_tms_i            ),
        .tdi_i            ( jtag_tdi_i            ),
        .tdo_o            ( jtag_tdo_o            ),
        .tdo_oe_o         ( jtag_tdo_oe_o         ),

        .hclk_i           ( free_clk_i            ),
        .dbgresetn_i      ( hresetn_i             ),

        .dmi_psel_o       ( dmi_psel              ),
        .dmi_penable_o    ( dmi_penable           ),
        .dmi_paddr_o      ( dmi_paddr             ),
        .dmi_pwrite_o     ( dmi_pwrite            ),
        .dmi_pwdata_o     ( dmi_pwdata            ),
        .dmi_pprot_o      ( dmi_pprot             ),
        .dmi_pready_i     ( dmi_pready            ),
        .dmi_prdata_i     ( dmi_prdata            ),
        .dmi_pslverr_i    ( dmi_pslverr           )
    );

end
else begin : g_no_dtm

    // DEBUG_EN=0: no DTM. Drive the DMI master side idle and tri-off the TDO pad.
    assign dmi_psel      =  1'b0;
    assign dmi_penable   =  1'b0;
    assign dmi_paddr     =  9'h0;
    assign dmi_pwrite    =  1'b0;
    assign dmi_pwdata    = 32'h0;
    assign dmi_pprot     =  3'h0;
    assign jtag_tdo_o    =  1'b0;
    assign jtag_tdo_oe_o =  1'b0;

    // Sink otherwise-unused JTAG inputs and core DMI responses.
    wire  unused_dbg = 1'b0 | jtag_tck_i | jtag_trst_n_i | jtag_tms_i | jtag_tdi_i
                            | dmi_pready | dmi_pslverr | (|dmi_prdata);

end
endgenerate

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

// DFT
    .scan_mode_i               ( scan_mode_i               ),

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
    .m_nx_hmaster_i            ( data_hmaster              ),
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


//////======================================================================================================================//////
//////                                                     LINT CLEANUP                                                     //////
//////======================================================================================================================//////

// Per-block clock enables (hclk_en_o) are driven but unused: this trial has no
// clock-gate cell, so every block runs on the free-running free_clk_i.
wire  dut_hclk_en_unused    = dut_hclk_en;
wire  system_hclk_en_unused = system_hclk_en;
wire  ccsr_hclk_en_unused   = ccsr_hclk_en;


endmodule

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
//                      core together with the AHB Interconnect, the SRAM/ROM
//                      controllers and the Belenos graphic controller.
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
    input  wire      [15:0] irq_platform_i, // bit 2 is ORed with the Belenos interrupt (MIP[18])

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
    output wire             dbg_ndmreset_o,

// Belenos display panels: 8080-16 bus
    input  wire      [15:0] lcd_d_i,        // data bus (input)
    output wire      [15:0] lcd_d_o,        // data bus (output)
    output wire             lcd_d_oe_o,     // data bus output enable
    output wire             lcd_cs_n_o,     // chip select (active low)
    output wire             lcd_rd_n_o,     // read strobe (active low)
    output wire             lcd_wr_n_o,     // write strobe (active low)
    output wire             lcd_dc_o,       // data/command select

// Belenos display panels: SPI
    output wire             spi_sck_o,      // SPI clock
    output wire             spi_mosi_o,     // SPI data out
    input  wire             spi_miso_i,     // SPI data in
    output wire             spi_cs_n_o,     // SPI chip select (active low)
    output wire             spi_dc_o,       // SPI data/command select

// Belenos display panels: shared control
    output wire             lcd_reset_n_o,  // panel reset (active low)
    output wire             lcd_on_o,       // panel on / backlight
    input  wire             lcd_te_i        // panel tearing effect
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

// Belenos interrupt -> core platform IRQ[2] (MIP[18])
wire                     blns_irq;

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
    .irq_platform_i            ({irq_platform_i[15:3],
                                  irq_platform_i[2] | blns_irq,
                                  irq_platform_i[1:0]      }), // [2]=Belenos IRQ (MIP[18])

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
//////                                              BELENOS GRAPHIC CONTROLLER                                              //////
//////======================================================================================================================//////
//
//   Register port: 4KB at 0x10043000. VRAM window: BLNS_VRAM_WORDS*4 bytes at
//   0x82000000. Both are decoded by the bus system. Both DBI PHYs are built
//   (DIF_TYPE=3); the BDI is not exported (tied per the IP contract). VRAM =
//   sixteen sram_8kb_wrapper banks (sram_32kb with a 15-bit address), LUT = one
//   sram_8kb_wrapper (512 entries, 24 bits used).
//

// Register and VRAM-window ports (from the bus system)
wire                     blnsr_hsel;
wire              [31:0] blnsr_haddr;
wire               [1:0] blnsr_htrans;
wire                     blnsr_hwrite;
wire               [2:0] blnsr_hsize;
wire               [3:0] blnsr_hprot;
wire                     blnsr_hsmode;
wire                     blnsr_hready;
wire              [31:0] blnsr_hwdata;
wire              [31:0] blnsr_hrdata;
wire                     blnsr_hreadyout;
wire                     blnsr_hresp;

wire                     blnsv_hsel;
wire              [31:0] blnsv_haddr;
wire               [1:0] blnsv_htrans;
wire                     blnsv_hwrite;
wire               [2:0] blnsv_hsize;
wire               [3:0] blnsv_hprot;
wire                     blnsv_hsmode;
wire                     blnsv_hready;
wire              [31:0] blnsv_hwdata;
wire              [31:0] blnsv_hrdata;
wire                     blnsv_hreadyout;
wire                     blnsv_hresp;

wire                     blns_hclk_en;

// VRAM and LUT macro interfaces
wire                     vram_cen;
wire               [3:0] vram_wen;
wire              [31:0] vram_din;
wire              [31:0] vram_dout;

wire                     lut_cen;
wire                     lut_wen;
wire               [8:0] lut_addr;
wire              [23:0] lut_din;
wire              [31:0] lut_dout;

// BDI outputs (built-in driver: not exported)
wire                     dpi_de_unused;
wire                     dpi_hs_unused;
wire                     dpi_vs_unused;
wire              [23:0] dpi_rgb_unused;
wire                     dpi_clk_d0_unused;
wire                     dpi_clk_d1_unused;
wire               [1:0] dpi_lb_we_unused;
wire               [9:0] dpi_lb_waddr_unused;
wire              [23:0] dpi_lb_wdata_unused;
wire               [9:0] dpi_lb_raddr_unused;
wire              [29:0] tmds_unused;
wire               [1:0] ddc_pd_unused;
wire                     bdi_px_valid_unused;
wire              [23:0] bdi_px_data_unused;
wire                     bdi_px_last_unused;
wire                     bdi_refresh_trig_unused;
wire               [9:0] bdi_width_unused;
wire               [9:0] bdi_height_unused;
wire                     bdi_reg_sel_unused;
wire               [5:0] bdi_reg_addr_unused;
wire                     bdi_reg_wr_unused;
wire              [31:0] bdi_reg_wdata_unused;

// Belenos configuration (ultra persona)
parameter   BLNS_VRAM_WORDS       =  32768;          // VRAM depth in 32-bit words (1 - 2^24): 128KB
localparam  BLNS_VRAM_AW          =  $clog2(BLNS_VRAM_WORDS); // VRAM word-address width, ceil(log2(BLNS_VRAM_WORDS)): derived
parameter   BLNS_VRAM_RD_REG_EN   =  0;              // VRAM read data registered: 1=one more read cycle, the macro's clock-to-output leaves too little of the cycle
parameter   BLNS_DIM_W            =  10;             // Width/height field width in bits (4-16): sizes up to 2^DIM_W - 1 pixels
parameter   BLNS_FRAME_NR         =  2;              // Number of frame pointers (1, 2 or 4)
parameter   BLNS_LUT_EN           =  1;              // Programmable colour LUT: 0=absent, 1=present
parameter   BLNS_LUT_BANKS        =  2;              // LUT banks of 256 entries (1 or 2, needs LUT_EN)
parameter   BLNS_GPU_EN           =  1;              // GPU: 0=absent, 1=present
parameter   BLNS_GPU_XY_EN        =  1;              // GPU x/y coordinates, clip window and affine blits (SET_DST XY, SET_XY, SET_CLIP, EXEC_TEXRECT, ...)
parameter   BLNS_GPU_GRAD_EN      =  1;              // GPU gradient fills (SET_GRAD)
parameter   BLNS_GPU_BLEND_EN     =  1;              // GPU alpha blending at 16 bpp (SET_ALPHA, EXPAND masks)
parameter   BLNS_GPU_TRI_EN       =  1;              // GPU triangles (SET_V0..V2, EXEC_FILL TRI), needs GPU_XY_EN
parameter   BLNS_GPU_RUNAHEAD_EN  =  1;              // GPU run-ahead: decode and set up the next primitive while one draws
parameter   BLNS_DIF_TYPE         =  3;              // Built-in display driver: 0=BDI (none), 1=DBI_SPI, 2=DBI_PAR (8080), 3=DBI_PAR_SPI: both PHYs on the chip pins
parameter   BLNS_PRIV_CHECK_EN    =  0;              // User-mode accesses on both ports: 0=allowed, 1=refused

wire  [BLNS_VRAM_AW-1:0] vram_addr;

belenos  #(.VRAM_WORDS          ( BLNS_VRAM_WORDS        ),
           .VRAM_AW             ( BLNS_VRAM_AW           ),
           .VRAM_RD_REG_EN      ( BLNS_VRAM_RD_REG_EN    ),
           .DIM_W               ( BLNS_DIM_W             ),
           .FRAME_NR            ( BLNS_FRAME_NR          ),
           .LUT_EN              ( BLNS_LUT_EN            ),
           .LUT_BANKS           ( BLNS_LUT_BANKS         ),
           .GPU_EN              ( BLNS_GPU_EN            ),
           .GPU_XY_EN           ( BLNS_GPU_XY_EN         ),
           .GPU_GRAD_EN         ( BLNS_GPU_GRAD_EN       ),
           .GPU_BLEND_EN        ( BLNS_GPU_BLEND_EN      ),
           .GPU_TRI_EN          ( BLNS_GPU_TRI_EN        ),
           .GPU_RUNAHEAD_EN     ( BLNS_GPU_RUNAHEAD_EN   ),
           .DIF_TYPE            ( BLNS_DIF_TYPE          ),
           .PRIV_CHECK_EN       ( BLNS_PRIV_CHECK_EN     ),
           .ASYNC_RST_EN        ( ASYNC_RST_EN           ))  belenos_inst (

// AHB CLOCK & RESET
    .hclk_i                ( free_clk_i                                ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( blns_hclk_en                              ),

// REGISTER PORT
    .r_hsel_i              ( blnsr_hsel                                ),
    .r_haddr_i             ( blnsr_haddr[11:0]                         ),
    .r_htrans_i            ( blnsr_htrans                              ),
    .r_hwrite_i            ( blnsr_hwrite                              ),
    .r_hsize_i             ( blnsr_hsize                               ),
    .r_hprot_i             ( blnsr_hprot                               ),
    .r_hsmode_i            ( blnsr_hsmode                              ),
    .r_hready_i            ( blnsr_hready                              ),
    .r_hwdata_i            ( blnsr_hwdata                              ),
    .r_hrdata_o            ( blnsr_hrdata                              ),
    .r_hreadyout_o         ( blnsr_hreadyout                           ),
    .r_hresp_o             ( blnsr_hresp                               ),

// VRAM-WINDOW PORT
    .v_hsel_i              ( blnsv_hsel                                ),
    .v_haddr_i             ( blnsv_haddr[BLNS_VRAM_AW+1:0]             ),
    .v_htrans_i            ( blnsv_htrans                              ),
    .v_hwrite_i            ( blnsv_hwrite                              ),
    .v_hsize_i             ( blnsv_hsize                               ),
    .v_hprot_i             ( blnsv_hprot                               ),
    .v_hsmode_i            ( blnsv_hsmode                              ),
    .v_hready_i            ( blnsv_hready                              ),
    .v_hwdata_i            ( blnsv_hwdata                              ),
    .v_hrdata_o            ( blnsv_hrdata                              ),
    .v_hreadyout_o         ( blnsv_hreadyout                           ),
    .v_hresp_o             ( blnsv_hresp                               ),

// VRAM MACRO
    .vram_cen_o            ( vram_cen                                  ),
    .vram_wen_o            ( vram_wen                                  ),
    .vram_addr_o           ( vram_addr                                 ),
    .vram_din_o            ( vram_din                                  ),
    .vram_dout_i           ( vram_dout                                 ),

// LUT MACRO
    .lut_cen_o             ( lut_cen                                   ),
    .lut_wen_o             ( lut_wen                                   ),
    .lut_addr_o            ( lut_addr                                  ),
    .lut_din_o             ( lut_din                                   ),
    .lut_dout_i            ( lut_dout[23:0]                            ),

// DEBUG / INTERRUPT
    .dbg_stoptime_i        ( dbg_stoptime_o                            ),
    .irq_o                 ( blns_irq                                  ),

// 8080 PANEL
    .lcd_d_i               ( lcd_d_i                                   ),
    .lcd_d_o               ( lcd_d_o                                   ),
    .lcd_d_oe_o            ( lcd_d_oe_o                                ),
    .lcd_cs_n_o            ( lcd_cs_n_o                                ),
    .lcd_rd_n_o            ( lcd_rd_n_o                                ),
    .lcd_wr_n_o            ( lcd_wr_n_o                                ),
    .lcd_dc_o              ( lcd_dc_o                                  ),

// SPI PANEL
    .spi_sck_o             ( spi_sck_o                                 ),
    .spi_mosi_o            ( spi_mosi_o                                ),
    .spi_miso_i            ( spi_miso_i                                ),
    .spi_cs_n_o            ( spi_cs_n_o                                ),
    .spi_dc_o              ( spi_dc_o                                  ),

// SHARED PANEL CONTROL
    .lcd_reset_n_o         ( lcd_reset_n_o                             ),
    .lcd_on_o              ( lcd_on_o                                  ),
    .lcd_te_i              ( lcd_te_i                                  ),

// DPI PANEL (absent: DIF_TYPE is a DBI driver)
    .dpi_de_o              ( dpi_de_unused                             ),
    .dpi_hs_o              ( dpi_hs_unused                             ),
    .dpi_vs_o              ( dpi_vs_unused                             ),
    .dpi_rgb_o             ( dpi_rgb_unused                            ),
    .dpi_clk_d0_o          ( dpi_clk_d0_unused                         ),
    .dpi_clk_d1_o          ( dpi_clk_d1_unused                         ),
    .dpi_pclk_i            ( 1'b0                                      ),
    .dpi_presetn_i         ( 1'b1                                      ),
    .panel_detect_i        ( 1'b1                                      ),
    .tmds_o                ( tmds_unused                               ),
    .ddc_scl_i             ( 1'b1                                      ),
    .ddc_sda_i             ( 1'b1                                      ),
    .ddc_scl_pd_o          ( ddc_pd_unused[0]                          ),
    .ddc_sda_pd_o          ( ddc_pd_unused[1]                          ),
    .i2s_bclk_i            ( 1'b0                                      ),  // HDMI audio: no TMDS encoder in this build
    .i2s_lrck_i            ( 1'b0                                      ),
    .i2s_sd_i              ( 1'b0                                      ),
    .dpi_lb_we_o           ( dpi_lb_we_unused                          ),
    .dpi_lb_waddr_o        ( dpi_lb_waddr_unused                       ),
    .dpi_lb_wdata_o        ( dpi_lb_wdata_unused                       ),
    .dpi_lb_raddr_o        ( dpi_lb_raddr_unused                       ),
    .dpi_lb_rdata_i        ( 48'h0                                     ),

// BELENOS DISPLAY INTERFACE (not exported: built-in driver)
    .bdi_px_valid_o        ( bdi_px_valid_unused                       ),
    .bdi_px_data_o         ( bdi_px_data_unused                        ),
    .bdi_px_last_o         ( bdi_px_last_unused                        ),
    .bdi_px_ready_i        ( 1'b1                                      ),
    .bdi_refresh_trig_o    ( bdi_refresh_trig_unused                   ),
    .bdi_refresh_active_i  ( 1'b0                                      ),
    .bdi_width_o           ( bdi_width_unused                          ),
    .bdi_height_o          ( bdi_height_unused                         ),
    .bdi_reg_sel_o         ( bdi_reg_sel_unused                        ),
    .bdi_reg_addr_o        ( bdi_reg_addr_unused                       ),
    .bdi_reg_wr_o          ( bdi_reg_wr_unused                         ),
    .bdi_reg_wdata_o       ( bdi_reg_wdata_unused                      ),
    .bdi_reg_rdata_i       ( 32'h00000000                              ),
    .bdi_reg_ready_i       ( 1'b1                                      ),
    .bdi_irq_i             ( 1'b0                                      ),
    .bdi_busy_i            ( 1'b0                                      )
);

// VRAM: 32768 x 32 (128KB) in sixteen 8KB banks
sram_32kb #(BLNS_VRAM_AW) vram_inst (

// OUTPUTs
    .sram_dout_o           ( vram_dout                                 ),

// INPUTs
    .sram_addr_i           ( vram_addr                                 ),
    .sram_cen_i            ( vram_cen                                  ),
    .sram_clk_i            ( free_clk_i                                ),
    .sram_rst_i            ( hresetn_i                                 ),
    .sram_din_i            ( vram_din                                  ),
    .sram_wen_i            ( vram_wen                                  )
);

// LUT: 512 x 24 in one 8KB macro (upper address bits and data byte unused)
sram_8kb_wrapper lut_inst (
    .dout_o                ( lut_dout                                  ),
    .din_i                 ( {8'h00, lut_din}                          ),
    .addr_i                ( {2'b00, lut_addr}                         ),
    .cen_i                 ( lut_cen                                   ),
    .wen_i                 ( {4{lut_wen}}                              ),
    .clk_i                 ( free_clk_i                                ),
    .rst_i                 ( hresetn_i                                 )
);

//////======================================================================================================================//////
//////                                                       BUS SYSTEM                                                     //////
//////======================================================================================================================//////

ahb_bus_system #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .VRAM_WORDS(BLNS_VRAM_WORDS), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_bus_system_inst (

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
    .aclint_time_val_o         ( aclint_time_val           ),

// BELENOS GRAPHIC CONTROLLER
    .blnsr_hsel_o              ( blnsr_hsel                ),
    .blnsr_haddr_o             ( blnsr_haddr               ),
    .blnsr_htrans_o            ( blnsr_htrans              ),
    .blnsr_hwrite_o            ( blnsr_hwrite              ),
    .blnsr_hsize_o             ( blnsr_hsize               ),
    .blnsr_hprot_o             ( blnsr_hprot               ),
    .blnsr_hsmode_o            ( blnsr_hsmode              ),
    .blnsr_hready_o            ( blnsr_hready              ),
    .blnsr_hwdata_o            ( blnsr_hwdata              ),
    .blnsr_hrdata_i            ( blnsr_hrdata              ),
    .blnsr_hreadyout_i         ( blnsr_hreadyout           ),
    .blnsr_hresp_i             ( blnsr_hresp               ),

    .blnsv_hsel_o              ( blnsv_hsel                ),
    .blnsv_haddr_o             ( blnsv_haddr               ),
    .blnsv_htrans_o            ( blnsv_htrans              ),
    .blnsv_hwrite_o            ( blnsv_hwrite              ),
    .blnsv_hsize_o             ( blnsv_hsize               ),
    .blnsv_hprot_o             ( blnsv_hprot               ),
    .blnsv_hsmode_o            ( blnsv_hsmode              ),
    .blnsv_hready_o            ( blnsv_hready              ),
    .blnsv_hwdata_o            ( blnsv_hwdata              ),
    .blnsv_hrdata_i            ( blnsv_hrdata              ),
    .blnsv_hreadyout_i         ( blnsv_hreadyout           ),
    .blnsv_hresp_i             ( blnsv_hresp               ),

    .blns_hclk_en_i            ( blns_hclk_en              )
);


//////======================================================================================================================//////
//////                                                     LINT CLEANUP                                                     //////
//////======================================================================================================================//////

// Per-block clock enables (hclk_en_o) are driven but unused: this trial has no
// clock-gate cell, so every block runs on the free-running free_clk_i.
wire  dut_hclk_en_unused    = dut_hclk_en;
wire  system_hclk_en_unused = system_hclk_en;
wire  ccsr_hclk_en_unused   = ccsr_hclk_en;

// Belenos: register port 4KB, VRAM window BLNS_VRAM_WORDS words, LUT 24 bits of 32
wire  blnsr_haddr_hi_unused = |{1'b0, blnsr_haddr[31:12]};
wire  blnsv_haddr_hi_unused = |{1'b0, blnsv_haddr[31:BLNS_VRAM_AW+2]};
wire  lut_dout_hi_unused    = |{1'b0, lut_dout[31:24]};


endmodule

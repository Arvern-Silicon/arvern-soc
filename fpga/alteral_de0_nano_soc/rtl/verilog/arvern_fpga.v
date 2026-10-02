//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    arvern_fpga
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : arvern_fpga.v
// Module Description : arvern FPGA Top-level for the DE0 Nano SoC
//----------------------------------------------------------------------------

module arvern_fpga (

  //-----------------------------
  // USER CLOCKS
  //-----------------------------
  input         FPGA_CLK1_50,
  input         FPGA_CLK2_50,
  input         FPGA_CLK3_50,

  //-----------------------------
  // USER INTERFACE (FPGA)
  //-----------------------------
  input   [1:0] KEY,
  input   [3:0] SW,
  output  [7:0] LED,

  //-----------------------------
  // GPIO
  //-----------------------------
  inout  [35:0] GPIO_0,
  inout  [35:0] GPIO_1,

  //-----------------------------
  // ARDUINO DIGITAL INTERFACE
  //-----------------------------
  inout  [15:0] ARDUINO_IO,
  inout         ARDUINO_RESET_N,

  //-----------------------------
  // ADC
  //-----------------------------
  output        ADC_CONVST,
  output        ADC_SCK,
  output        ADC_SDI,
  input         ADC_SDO
);

//=============================================================================
// 1)  INTERNAL WIRES/REGISTERS/PARAMETERS DECLARATION
//=============================================================================

// Clock & Resets
wire        clk_50mhz;
wire        dbgresetn;
wire        resetn_lf;
wire        hresetn;
wire        clk_lf;
wire        dbg_ndmreset;

// Unused output ports (Lint cleanup)
wire [10:0] ccsr_bank_o_unused;
wire [63:0] ccsr_reg_sel_o_unused;
wire [31:0] ccsr_wdata_o_unused;
wire        ccsr_wen_o_unused;
wire        lockup_o_unused;

// Debug status observation (not brought to board pins on this FPGA)
wire        dbg_debug_mode_o_unused;
wire        dbg_halted_o_unused;
wire        dbg_stoptime_o_unused;
wire        dbg_wakeup_unused;

// Debug Module Interface (DMI, APB) - DTM master <-> core DM slave
wire        dmi_psel;
wire        dmi_penable;
wire  [8:0] dmi_paddr;
wire        dmi_pwrite;
wire [31:0] dmi_pwdata;
wire  [2:0] dmi_pprot;
wire        dmi_pready;
wire [31:0] dmi_prdata;
wire        dmi_pslverr;

// DTM PHY nets (mapped to GPIO_1; see the DTM section below)
wire        jtag_tck;
wire        jtag_trst_n;
wire        jtag_tms;
wire        jtag_tdi;
wire        jtag_tdo;
wire        jtag_tdo_oe;
wire        uart_rx;
wire        uart_tx;
wire        i2c_scl;
wire        i2c_sda;
wire        i2c_scl_pd;
wire        i2c_sda_pd;
wire        cjtag_tckc;
wire        cjtag_tmsc_i;
wire        cjtag_tmsc_o;
wire        cjtag_tmsc_oe;

wire [31:0] periph1_reg_00_o_unused;
wire [31:0] periph1_reg_01_o_unused;
wire [31:0] periph1_reg_02_o_unused;
wire [31:0] periph1_reg_03_o_unused;
wire [31:0] periph1_reg_04_o_unused;
wire [31:0] periph1_reg_05_o_unused;
wire [31:0] periph1_reg_06_o_unused;
wire [31:0] periph1_reg_07_o_unused;

wire [31:0] periph2_reg_00_o_unused;
wire [31:0] periph2_reg_01_o_unused;
wire [31:0] periph2_reg_02_o_unused;
wire [31:0] periph2_reg_03_o_unused;
wire [31:0] periph2_reg_04_o_unused;
wire [31:0] periph2_reg_05_o_unused;
wire [31:0] periph2_reg_06_o_unused;
wire [31:0] periph2_reg_07_o_unused;


//=============================================================================
// 2)  CLOCK AND RESET GENERATION
//=============================================================================

assign clk_50mhz  = FPGA_CLK1_50;

// Use KEY[0] as the raw async power-on reset (active low on the board).
wire   reset_in_n = KEY[0];

// Low-frequency timebase for the ACLINT MTIME counter.
//
// clk_lf is sampled AS DATA by the ACLINT's tick detector in BOTH LF_SYNC_EN
// modes, and with LF_SYNC_EN=1 that tick is MTIME's clock enable -- no tick
// means no time at all. Every clk_lf phase must therefore span at least two
// hclk_aon periods; the IP quotes R >= 10 unless the duty cycle is bounded.
// Toggling on a terminal count gives an exact 50/50 duty:
//   clk_lf = clk_50mhz / (2 * (CLK_LF_TC + 1))
// 4 -> /10 -> 5 MHz, the fastest rate the IP allows with margin (R >= 10). The
// same timebase serves the board and the simulation, so firmware has one set of
// mtime units (1 tick = 200 ns).
localparam [9:0] CLK_LF_TC = 10'd4;

reg  [9:0]  clk_lf_cnt;
reg         clk_lf_q;

always @(posedge clk_50mhz or negedge reset_in_n)
  if (!reset_in_n) begin
     clk_lf_cnt <= 10'd0;
     clk_lf_q   <= 1'b0;
  end else if (clk_lf_cnt == CLK_LF_TC) begin
     clk_lf_cnt <= 10'd0;
     clk_lf_q   <= ~clk_lf_q;
  end else begin
     clk_lf_cnt <= clk_lf_cnt + 10'd1;
  end

assign clk_lf = clk_lf_q;

// Reset generation. LF_GATE_EN=0 here on purpose: this board builds the ACLINT
// with LF_SYNC_EN=1, where clk_lf is sampled purely as DATA and resetn_lf has no
// consumer, so synthesis removes its clk_lf-clocked synchroniser. Gating hresetn
// behind it would keep that synchroniser and put a fabric-routed clk_lf domain
// (global buffer, create_generated_clock in design.sdc) on the boot path, to
// order a reset nothing uses. A platform with LF_SYNC_EN=0 and a real LF domain
// wants LF_GATE_EN=1 and those constraints.
arv_reset_gen #(
    .LF_GATE_EN    ( 1'b0          )
) u_reset_gen (
    .porn_async_i  ( reset_in_n    ),
    .warm_reset_i  ( dbg_ndmreset  ),
    .scan_mode_i   ( 1'b0          ),

    .clk_lf_i      ( clk_lf        ),
    .hclk_i        ( clk_50mhz     ),

    .resetn_lf_o   ( resetn_lf     ),
    .dbgresetn_o   ( dbgresetn     ),
    .hresetn_o     ( hresetn       ));


//=============================================================================
// 4)  PERIPHERAL I/O MAPPING
//=============================================================================

// Peripheral #0: LEDs / Keys / Switches (active register outputs for testbench probing)
wire  [7:0] periph0_led;            // CPU-controlled LEDs
wire [31:0] periph0_reg_01;         // TTY data register
wire [31:0] periph0_reg_08;         // Key/Switch value register
wire        periph0_irq_key;        // KEY edge IRQ -> core platform IRQ[0] (MIP[16])
wire        periph0_irq_sw;         // SW  edge IRQ -> core platform IRQ[1] (MIP[17])

assign      LED = periph0_led[7:0];


//=============================================================================
// 5)  ARVERN CORE
//=============================================================================

// Memory sizes
parameter                ROM_SIZE     = 32*1024;
parameter                SRAM_X_SIZE  = 32*1024;
parameter                SRAM_NX_SIZE = 32*1024;

// Clock enable (unused in FPGA, always-on)
wire                     dut_hclk_en;
wire                     system_hclk_en;

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
wire                     data_hmaster;
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

// Core configuration
parameter   RV32E_EN            =  1;               // Base ISA: 0=RV32I, 1=RV32E
parameter   SU_MODE_EN          =  1;               // S-mode + U-mode privilege modes
parameter   PMP_NR              =  4;               // Physical Memory Protection: writable entries (0, 4, 8 or 16; 0=absent)
parameter   ZICNTR_EN           =  0;               // Zicntr extension enable (cycle, time, instret) (0=absent, 1=present)
parameter   ZIHPM_NR            =  0;               // Zihpm: number of HPM counters (0-8)
parameter   B_EXTENSION         =  4;               // Bit manipulation extension: 0=none, 1=Zbb, 2=Zbb+Zba, 3=Zbb+Zba+Zbs, 4=Zbb+Zba+Zbs+Zbc
parameter   C_EXTENSION         =  4;               // Compressed instructions extension: 0=none, 1=Zca, 2=Zca+Zcb, 3=Zca+Zcb+Zcmp, 4=Zca+Zcb+Zcmp+Zcmt
parameter   M_EXTENSION         =  0;               // Integer Multiply/Divide extension: 0=none, 1=Zmmul, 2=M-extension
parameter   MUL_TYPE            =  3;               // Multiplier type: 1=1xCycle,  2=4xCycles,  3=16xCycles (valid only with Zmmul and M-extension)
parameter   DIV_TYPE            =  3;               // Divider type:    1=12xCycle, 2=17xCycles, 3=33xCycles (valid only with M-extension)
parameter   CCSR_EN             =  0;               // Enable Custom-CSR interface
parameter   SINGLE_CYCLE_BRANCH =  0;               // Taken-branch latency: 1=zero-bubble (max IPC, lower Fmax), 0=one-bubble (lower IPC, max Fmax)
parameter   ASYNC_RST_EN        =  1'b0;            // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

parameter   DEBUG_EN            =  1;               // External debug (RISC-V Debug 1.0): 0=absent, 1=present (DTM wrapper below)
parameter   DM_TRIGGER_NR       =  2;               // Sdtrig hardware triggers (0-8, only meaningful when DEBUG_EN=1)
parameter   DTM_TYPE            =  0;               // DTM transport picked at build time: 0=JTAG, 1=UART, 2=I2C, 3=cJTAG
parameter   IDCODE_BASE         =  28'h800_01F7;    // JTAG IDCODE[27:0]: mfg field [11:1] = Arvern Silicon's JEDEC
                                                    // The version field [31:28] is NOT here -- it is a port
                                                    // (idcode_version_i) so silicon can bump it by ECO. On FPGA
                                                    // there is no metal ECO, so it is simply strapped to 0 below.
                                                    // identity (fixed); part-number [27:12] = 0x8000 = this
                                                    // board (DE0-Nano-SoC) in Arvern's own FPGA reference-board
                                                    // catalog (0x8000-0x8FFF). See arv_dtm.md for the catalog
                                                    // and the manufacturer-field derivation.
parameter   UART_LOOPBACK       =  0;               // Bring-up sanity check ONLY: 1 = combinationally echo the UART_RX pad back out the UART_TX pad (bypasses the DTM), so a host loopback proves the full physical path (adapter+wires+FPGA pads+pins). Leave 0 for a real build.
localparam  DEBUG_ON            =  (DEBUG_EN != 0); // 1-bit form of DEBUG_EN for gating the GPIO pad assigns

arvern   #(.RV32E_EN            ( RV32E_EN               ),
           .SU_MODE_EN          ( SU_MODE_EN             ),
           .PMP_NR              ( PMP_NR                 ),
           .ZICNTR_EN           ( ZICNTR_EN              ),
           .ZIHPM_NR            ( ZIHPM_NR               ),
           .B_EXTENSION         ( B_EXTENSION            ),
           .C_EXTENSION         ( C_EXTENSION            ),
           .M_EXTENSION         ( M_EXTENSION            ),
           .MUL_TYPE            ( MUL_TYPE               ),
           .DIV_TYPE            ( DIV_TYPE               ),
           .CCSR_EN             ( CCSR_EN                ),
           .SINGLE_CYCLE_BRANCH ( SINGLE_CYCLE_BRANCH    ),
           .ASYNC_RST_EN        ( ASYNC_RST_EN           ),
           .DEBUG_EN            ( DEBUG_EN               ),
           .DM_TRIGGER_NR       ( DM_TRIGGER_NR          )) dut (

// AHB CLOCK & RESET
    .hclk_i                    ( clk_50mhz               ),
    .hresetn_i                 ( hresetn                 ),
    .dbgresetn_i               ( dbgresetn               ),
    .hclk_en_o                 ( dut_hclk_en             ),

// INSTRUCTION AHB BUS
    .inst_hrdata_i             ( inst_hrdata             ),
    .inst_hready_i             ( inst_hready             ),
    .inst_hresp_i              ( inst_hresp              ),

    .inst_haddr_o              ( inst_haddr              ),
    .inst_hburst_o             ( inst_hburst             ),
    .inst_hmastlock_o          ( inst_hmastlock          ),
    .inst_hprot_o              ( inst_hprot              ),
    .inst_hsize_o              ( inst_hsize              ),
    .inst_hsmode_o             ( inst_hsmode             ),
    .inst_htrans_o             ( inst_htrans             ),
    .inst_hwdata_o             ( inst_hwdata             ),
    .inst_hwrite_o             ( inst_hwrite             ),

// DATA AHB BUS
    .data_hrdata_i             ( data_hrdata             ),
    .data_hready_i             ( data_hready             ),
    .data_hresp_i              ( data_hresp              ),

    .data_haddr_o              ( data_haddr              ),
    .data_hburst_o             ( data_hburst             ),
    .data_hmastlock_o          ( data_hmastlock          ),
    .data_hprot_o              ( data_hprot              ),
    .data_hsize_o              ( data_hsize              ),
    .data_hsmode_o             ( data_hsmode             ),
    .data_htrans_o             ( data_htrans             ),
    .data_hwdata_o             ( data_hwdata             ),
    .data_hwrite_o             ( data_hwrite             ),

// INTERFACE TO CUSTOM CSR REGISTERS (disabled)
    .ccsr_rdata_i              ( 32'h00000000            ),
    .ccsr_bank_o               ( ccsr_bank_o_unused      ),
    .ccsr_reg_sel_o            ( ccsr_reg_sel_o_unused   ),
    .ccsr_wdata_o              ( ccsr_wdata_o_unused     ),
    .ccsr_wen_o                ( ccsr_wen_o_unused       ),

// EXTERNAL DEBUG (RISC-V Debug 1.0) - DMI slave + status; inert when DEBUG_EN=0
    .data_hmaster_o            ( data_hmaster            ),
    .dbg_debug_mode_o          ( dbg_debug_mode_o_unused ),
    .dbg_halted_o              ( dbg_halted_o_unused     ),
    .dbg_stoptime_o            ( dbg_stoptime_o_unused   ),
    .dbg_ndmreset_o            ( dbg_ndmreset            ),
    .dmi_psel_i                ( dmi_psel                ),
    .dmi_penable_i             ( dmi_penable             ),
    .dmi_paddr_i               ( dmi_paddr               ),
    .dmi_pwrite_i              ( dmi_pwrite              ),
    .dmi_pwdata_i              ( dmi_pwdata              ),
    .dmi_pprot_i               ( dmi_pprot               ),
    .dmi_pready_o              ( dmi_pready              ),
    .dmi_prdata_o              ( dmi_prdata              ),
    .dmi_pslverr_o             ( dmi_pslverr             ),

// INTERRUPT INPUTS
    .irq_m_software_i          ( aclint_irq_m_software   ),
    .irq_s_software_i          ( aclint_irq_s_software   ),
    .irq_m_timer_i             ( aclint_irq_m_timer      ),
    .irq_m_external_i          ( 1'b0                    ),
    .irq_s_external_i          ( 1'b0                    ),
    .irq_platform_i            ( {14'h0000,
                                  periph0_irq_sw,
                                  periph0_irq_key       }), // [1]=SW IRQ (MIP[17]), [0]=KEY IRQ (MIP[16])

// OTHERS
    .hartid_i                  ( 8'h23                   ),
    .reset_vector_i            ( 32'h20000000            ),

// LOCKUP STATUS
    .lockup_o                  ( lockup_o_unused         ),

// NMI (SMRNMI)
    .nmi_i                     (  1'b0                   ),
// TIME INTERFACE (ZICNTR)
    .time_req_o                ( aclint_time_req         ),
    .time_gnt_i                ( aclint_time_gnt         ),
    .time_val_i                ( aclint_time_val         ),

// PLATFORM EVENTS (ZIHPM)
    .hpm_platform_events_i     (  8'h00                  )

);


//=============================================================================
// 6)  BUS SYSTEM
//=============================================================================

ahb_bus_system #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_bus_system_inst (

// AHB CLOCK & RESET
    .hclk_i                    ( clk_50mhz                 ),
    .hresetn_i                 ( hresetn                   ),
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

// AHB PERIPHERAL #0 -- LEDs / Keys / Switches
    .periph0_led_o             ( periph0_led               ),
    .periph0_key_i             ( {KEY[1], 1'b1}            ),  // KEY[0] is the board reset (reset_in_n)

    .periph0_sw_i              ( SW                        ),
    .periph0_irq_key_o         ( periph0_irq_key           ),
    .periph0_irq_sw_o          ( periph0_irq_sw            ),
    .periph0_tty_data_o        ( periph0_reg_01            ),
    .periph0_key_sw_val_o      ( periph0_reg_08            ),

// AHB PERIPHERAL #1 -- Unused
    .periph1_reg_08_i          ( 32'h00000000              ),
    .periph1_reg_09_i          ( 32'h00000000              ),
    .periph1_reg_10_i          ( 32'h00000000              ),
    .periph1_reg_11_i          ( 32'h00000000              ),
    .periph1_reg_12_i          ( 32'h00000000              ),
    .periph1_reg_13_i          ( 32'h00000000              ),
    .periph1_reg_14_i          ( 32'h00000000              ),
    .periph1_reg_15_i          ( 32'h00000000              ),

    .periph1_reg_00_o          ( periph1_reg_00_o_unused   ),
    .periph1_reg_01_o          ( periph1_reg_01_o_unused   ),
    .periph1_reg_02_o          ( periph1_reg_02_o_unused   ),
    .periph1_reg_03_o          ( periph1_reg_03_o_unused   ),
    .periph1_reg_04_o          ( periph1_reg_04_o_unused   ),
    .periph1_reg_05_o          ( periph1_reg_05_o_unused   ),
    .periph1_reg_06_o          ( periph1_reg_06_o_unused   ),
    .periph1_reg_07_o          ( periph1_reg_07_o_unused   ),

// AHB PERIPHERAL #2 -- Unused
    .periph2_reg_08_i          ( 32'h00000000              ),
    .periph2_reg_09_i          ( 32'h00000000              ),
    .periph2_reg_10_i          ( 32'h00000000              ),
    .periph2_reg_11_i          ( 32'h00000000              ),
    .periph2_reg_12_i          ( 32'h00000000              ),
    .periph2_reg_13_i          ( 32'h00000000              ),
    .periph2_reg_14_i          ( 32'h00000000              ),
    .periph2_reg_15_i          ( 32'h00000000              ),

    .periph2_reg_00_o          ( periph2_reg_00_o_unused   ),
    .periph2_reg_01_o          ( periph2_reg_01_o_unused   ),
    .periph2_reg_02_o          ( periph2_reg_02_o_unused   ),
    .periph2_reg_03_o          ( periph2_reg_03_o_unused   ),
    .periph2_reg_04_o          ( periph2_reg_04_o_unused   ),
    .periph2_reg_05_o          ( periph2_reg_05_o_unused   ),
    .periph2_reg_06_o          ( periph2_reg_06_o_unused   ),
    .periph2_reg_07_o          ( periph2_reg_07_o_unused   ),

// ACLINT CLOCK & RESET
    .clk_lf_i                  ( clk_lf                    ),
    .resetn_lf_i               ( resetn_lf                 ),
    .hclk_aon_i                ( clk_50mhz                 ),

// ACLINT INTERRUPTS & ZICNTR TIME INTERFACE
    .aclint_irq_m_software_o   ( aclint_irq_m_software     ),
    .aclint_irq_s_software_o   ( aclint_irq_s_software     ),
    .aclint_irq_m_timer_o      ( aclint_irq_m_timer        ),

    .aclint_time_req_i         ( aclint_time_req           ),
    .aclint_time_gnt_o         ( aclint_time_gnt           ),
    .aclint_time_val_o         ( aclint_time_val           )
);


//=============================================================================
// 6b) DEBUG TRANSPORT MODULE (selectable JTAG/UART/I2C/cJTAG wrapper) - DEBUG_EN only
//=============================================================================

generate
if (DEBUG_ON) begin : g_dtm

    // Synthesizable transport-selectable DTM. DTM_TYPE picks JTAG/UART/I2C/cJTAG at
    // build time (generate, not a runtime mux). Drives the core DMI (APB)
    // master side. clk_i MUST be the ungated oscillator (clk_50mhz).
    arv_dtm #(.DTM_TYPE           ( DTM_TYPE      ),
              .IDCODE_BASE        ( IDCODE_BASE   ),
              .UART_RX_FIFO_DEPTH ( 128           ),
              .ARST_EN            ( ASYNC_RST_EN  )) u_dtm (

        .idcode_version_i ( 4'h0              ),   // no metal ECO on FPGA: version strapped to 0

        .clk_i            ( clk_50mhz         ),
        .dbgresetn_i      ( dbgresetn         ),
        .dbg_wakeup_o     ( dbg_wakeup_unused ),   // cold-attach wake up not needed on FPGA
        .scan_mode_i      ( 1'b0              ),   // no scan infrastructure on FPGA

        .tck_i            ( jtag_tck          ),
        .trst_n_i         ( jtag_trst_n       ),
        .tms_i            ( jtag_tms          ),
        .tdi_i            ( jtag_tdi          ),
        .tdo_o            ( jtag_tdo          ),
        .tdo_oe_o         ( jtag_tdo_oe       ),

        .uart_rx_i        ( uart_rx           ),
        .uart_tx_o        ( uart_tx           ),

        .scl_i            ( i2c_scl           ),
        .sda_i            ( i2c_sda           ),
        .scl_pd_o         ( i2c_scl_pd        ),
        .sda_pd_o         ( i2c_sda_pd        ),

        .tckc_i           ( cjtag_tckc        ),
        .tmsc_i           ( cjtag_tmsc_i      ),
        .tmsc_o           ( cjtag_tmsc_o      ),
        .tmsc_oe_o        ( cjtag_tmsc_oe     ),

        .dmi_psel_o       ( dmi_psel          ),
        .dmi_penable_o    ( dmi_penable       ),
        .dmi_paddr_o      ( dmi_paddr         ),
        .dmi_pwrite_o     ( dmi_pwrite        ),
        .dmi_pwdata_o     ( dmi_pwdata        ),
        .dmi_pprot_o      ( dmi_pprot         ),
        .dmi_pready_i     ( dmi_pready        ),
        .dmi_prdata_i     ( dmi_prdata        ),
        .dmi_pslverr_i    ( dmi_pslverr       )
    );

end
else begin : g_no_dtm

    // DEBUG_EN=0: no DTM. Drive the DMI master side idle and idle the PHY nets.
    assign dmi_psel      =  1'b0;
    assign dmi_penable   =  1'b0;
    assign dmi_paddr     =  9'h0;
    assign dmi_pwrite    =  1'b0;
    assign dmi_pwdata    = 32'h0;
    assign dmi_pprot     =  3'h0;
    assign jtag_tdo      =  1'b0;
    assign jtag_tdo_oe   =  1'b0;
    assign uart_tx       =  1'b1;   // UART idles high
    assign i2c_scl_pd    =  1'b0;
    assign i2c_sda_pd    =  1'b0;
    assign cjtag_tmsc_o  =  1'b0;
    assign cjtag_tmsc_oe =  1'b0;

    // Sink the core DMI responses and the (unconsumed) PHY input nets.
    wire   unused_dbg    = 1'b0 | dmi_pready | dmi_pslverr | (|dmi_prdata)
                                | jtag_tck   | jtag_trst_n | jtag_tms | jtag_tdi
                                | uart_rx    | i2c_scl     | i2c_sda
                                | cjtag_tckc | cjtag_tmsc_i;

end
endgenerate

//-----------------------------------------------------------------------------
// DTM PHY <-> GPIO_1 pin map (active only when DEBUG_EN=1; else outputs Hi-Z).
//
// Only one transport is wired at a time (selected by DTM_TYPE at build)
//
// Board orientation (DE0-Nano-SoC User Manual Fig 2-1 / Fig 3-18, top view):
//
//   * The DTM signals (pins 1-9) are on the Ethernet/buttons side; the high
//     pins (39/40) point toward the board interior (USB-Blaster / HPS User LED).
//
//               <- Gigabit Ethernet RJ45 (adjacent board edge) ->
//               <- KEY0/KEY1 buttons + 2x5 ADC header near pin 1 ->
//                                    ||
//     odd (left)                     \/                    even (right)
//    +---------------------------------+   +-----------------------------------+
//    |  1  GPIO_1[ 0]  Y15   JTAG_TCK  | . |  2  GPIO_1[ 1]  AG28  JTAG_TMS    |
//    |  3  GPIO_1[ 2]  AA15  JTAG_TDI  | . |  4  GPIO_1[ 3]  AH27  JTAG_TRST_N |
//    |  5  GPIO_1[ 4]  AG26  JTAG_TDO  | . |  6  GPIO_1[ 5]  AH24  (unused)    |
//    |  7  GPIO_1[ 6]  AF23  UART_TX   | . |  8  GPIO_1[ 7]  AE22  I2C_SCL     |
//    |  9  GPIO_1[ 8]  AF21  I2C_SDA   | . | 10  GPIO_1[ 9]  AG20  UART_RX     |
//    | 11  +5V                         | . | 12  GND                           |
//    | 13  GPIO_1[10]  AG19  CJTAG_TCKC| . | 14  GPIO_1[11]  AF20  CJTAG_TMSC  |
//    | 15  GPIO_1[12]  AC23  (unused)  | . | 16  GPIO_1[13]  AG18  (unused)    |
//    | 17  GPIO_1[14]  AH26  (unused)  | . | 18  GPIO_1[15]  AA19  (unused)    |
//    | 19  GPIO_1[16]  AG24  (unused)  | . | 20  GPIO_1[17]  AF25  (unused)    |
//    | 21  GPIO_1[18]  AH23  (unused)  | . | 22  GPIO_1[19]  AG23  (unused)    |
//    | 23  GPIO_1[20]  AE19  (unused)  | . | 24  GPIO_1[21]  AF18  (unused)    |
//    | 25  GPIO_1[22]  AD19  (unused)  | . | 26  GPIO_1[23]  AE20  (unused)    |
//    | 27  GPIO_1[24]  AE24  (unused)  | . | 28  GPIO_1[25]  AD20  (unused)    |
//    | 29  +3.3V                       | . | 30  GND                           |
//    | 31  GPIO_1[26]  AF22  (unused)  | . | 32  GPIO_1[27]  AH22  (unused)    |
//    | 33  GPIO_1[28]  AH19  (unused)  | . | 34  GPIO_1[29]  AH21  (unused)    |
//    | 35  GPIO_1[30]  AG21  (unused)  | . | 36  GPIO_1[31]  AH18  (unused)    |
//    | 37  GPIO_1[32]  AD23  (unused)  | . | 38  GPIO_1[33]  AE23  (unused)    |
//    | 39  GPIO_1[34]  AA18  (unused)  | . | 40  GPIO_1[35]  AC22  (unused)    |
//    +---------------------------------+   +-----------------------------------+
//                                    /\
//               <- toward board interior: USB-Blaster / HPS User LED ->
//
//-----------------------------------------------------------------------------

wire jtag_tdo_pad_rdata_unused;   // TDO is push-pull output: pad read-back unused
wire uart_tx_pad_rdata_unused;    // UART_TX is push-pull output: pad read-back unused

// UART_TX pad source: normally the DTM's uart_tx; in UART_LOOPBACK bring-up mode
// it is the raw UART_RX pad value (combinational passthrough) so a host loopback
// exercises adapter TX -> wire -> UART_RX pad (pin 10) -> fabric -> UART_TX pad
// (pin 7) -> wire -> adapter RX, i.e. the entire physical link minus the DTM. The
// pad is driven whenever the DTM is present OR loopback is on (so a DEBUG_EN=0
// wiring-check build still drives TX).
wire uart_tx_pad_din = UART_LOOPBACK ? uart_rx : uart_tx;
wire uart_tx_pad_oe  = DEBUG_ON || (UART_LOOPBACK != 0);

io_buf io_buf_jtag_tck (    // [0] JTAG_TCK (input)
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( jtag_tck                  ),
    .dataio  ( GPIO_1[0]                 ));

io_buf io_buf_jtag_tms (    // [1] JTAG_TMS (input)
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( jtag_tms                  ),
    .dataio  ( GPIO_1[1]                 ));

io_buf io_buf_jtag_tdi (    // [2] JTAG_TDI (input)
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( jtag_tdi                  ),
    .dataio  ( GPIO_1[2]                 ));

io_buf io_buf_jtag_trst_n ( // [3] JTAG_TRST_N (input)
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( jtag_trst_n               ),
    .dataio  ( GPIO_1[3]                 ));

io_buf io_buf_jtag_tdo (    // [4] JTAG_TDO (output, tri-stated by tdo_oe)
    .datain  ( jtag_tdo                  ),
    .oe      ( DEBUG_ON && jtag_tdo_oe   ),
    .dataout ( jtag_tdo_pad_rdata_unused ),
    .dataio  ( GPIO_1[4]                 ));

io_buf io_buf_i2c_scl (     // [7] I2C_SCL (inout, open-drain)  -> AE22 / JP7 pin 8
    .datain  ( 1'b0                      ),
    .oe      ( DEBUG_ON && i2c_scl_pd    ),
    .dataout ( i2c_scl                   ),
    .dataio  ( GPIO_1[7]                 ));

io_buf io_buf_uart_tx (     // [6] UART_TX (output)  -> AF23 / JP7 pin 7
    .datain  ( uart_tx_pad_din           ),
    .oe      ( uart_tx_pad_oe            ),
    .dataout ( uart_tx_pad_rdata_unused  ),
    .dataio  ( GPIO_1[6]                 ));

io_buf io_buf_uart_rx (     // [9] UART_RX (input)  -> AG20 / JP7 pin 10
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( uart_rx                   ),
    .dataio  ( GPIO_1[9]                 ));

io_buf io_buf_i2c_sda (     // [8] I2C_SDA (inout, open-drain)
    .datain  ( 1'b0                      ),
    .oe      ( DEBUG_ON && i2c_sda_pd    ),
    .dataout ( i2c_sda                   ),
    .dataio  ( GPIO_1[8]                 ));

io_buf io_buf_cjtag_tckc (  // [10] CJTAG_TCKC (input)  -> AG19 / JP7 pin 13
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( cjtag_tckc                ),
    .dataio  ( GPIO_1[10]                ));

// TMSC is push-pull and bidirectional: the target drives only in the TDO phase
// while TCKC is low. The pad keeper (bus hold) sustains the level while nobody
// drives, which the OScan1 sampling contract depends on.
wire tmsc_pad_do = cjtag_tmsc_o;
wire tmsc_pad_oe = DEBUG_ON && cjtag_tmsc_oe;

io_buf io_buf_cjtag_tmsc (  // [11] CJTAG_TMSC (inout, bus-hold)  -> AF20 / JP7 pin 14
    .datain  ( tmsc_pad_do               ),
    .oe      ( tmsc_pad_oe               ),
    .dataout ( cjtag_tmsc_i              ),
    .dataio  ( GPIO_1[11]                ));


//=============================================================================
// 7)  UNUSED I/O
//=============================================================================

// GPIO_1 bits [4:0], [6], [7], [8], [9], [10], [11] are driven by the DTM pad buffers
// (io_buf) above; the remaining bits are idled here. Bit [5]/JP7 pin 6 is the
// deliberate gap that keeps the UART pair and I2C pair aligned on the header;
// GPIO_0 is entirely unused (high-Z).
assign GPIO_1[5]       = 1'bz;
assign GPIO_1[35:12]   = {24{1'bz}};
assign GPIO_0          = 36'hzzzzzzzzz;
assign ARDUINO_IO      = 16'hzzzz;
assign ARDUINO_RESET_N =  1'hz;
assign ADC_CONVST      =  1'hz;
assign ADC_SCK         =  1'hz;
assign ADC_SDI         =  1'hz;


//=============================================================================
// 8)  LINT CLEANUP
//=============================================================================

// Unused signals: consumed by a "*_unused"-named wire (Verilator does not flag
// UNUSEDSIGNAL on any signal whose name contains "unused").
wire        dut_hclk_en_unused    = dut_hclk_en;      // FPGA: clock enable unused (always-on)
wire        system_hclk_en_unused = system_hclk_en;   // FPGA: clock enable unused (always-on)
wire        FPGA_CLK2_50_unused   = FPGA_CLK2_50;     // Unused board clock input
wire        FPGA_CLK3_50_unused   = FPGA_CLK3_50;     // Unused board clock input
wire        ADC_SDO_unused        = ADC_SDO;          // Unused ADC serial-data input
wire [31:0] periph0_reg_01_unused = periph0_reg_01;   // TTY data register (probe only)
wire [31:0] periph0_reg_08_unused = periph0_reg_08;   // Key/Switch value register (probe only)


endmodule // arvern_fpga

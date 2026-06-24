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

wire        clk_50mhz;
wire        reset_n;


// Unused output ports (Lint cleanup)
wire [10:0] ccsr_bank_o_unused;
wire [63:0] ccsr_reg_sel_o_unused;
wire [31:0] ccsr_wdata_o_unused;
wire        ccsr_wen_o_unused;
wire        lockup_o_unused;

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

assign clk_50mhz = FPGA_CLK1_50;

// Use KEY[0] as reset (active low on the board)
wire reset_in_n = KEY[0];

// Release system reset a few clock cycles after the FPGA power-on-reset
reg [7:0] reset_dly_chain;
always @ (posedge clk_50mhz or negedge reset_in_n)
  if (!reset_in_n) reset_dly_chain <= 8'h00;
  else             reset_dly_chain <= {1'b1, reset_dly_chain[7:1]};

assign reset_n = reset_dly_chain[0];


//=============================================================================
// 4)  PERIPHERAL I/O MAPPING
//=============================================================================

// Peripheral #0: LEDs / Keys / Switches (active register outputs for testbench probing)
wire  [7:0] periph0_led;            // CPU-controlled LEDs
wire [31:0] periph0_reg_01;         // TTY data register
wire [31:0] periph0_reg_08;         // Key/Switch value register
wire        periph0_irq_key;        // KEY edge IRQ -> core platform IRQ[0] (MIP[16])
wire        periph0_irq_sw;         // SW  edge IRQ -> core platform IRQ[1] (MIP[17])

assign LED = periph0_led[7:0];


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
parameter   RV32E_EN            =  1;             // Base ISA: 0=RV32I, 1=RV32E
parameter   NMI_EN              =  0;             // Smrnmi resumable NMI extension (0=absent, 1=present)
parameter   SU_MODE_EN          =  1;             // S-mode + U-mode privilege modes
parameter   ZICNTR_EN           =  0;             // Zicntr extension enable (cycle, time, instret) (0=absent, 1=present)
parameter   ZIHPM_NR            =  0;             // Zihpm: number of HPM counters (0-8)
parameter   B_EXTENSION         =  4;             // Bit manipulation extension: 0=none, 1=Zbb, 2=Zbb+Zba, 3=Zbb+Zba+Zbs, 4=Zbb+Zba+Zbs+Zbc
parameter   C_EXTENSION         =  4;             // Compressed instructions extension: 0=none, 1=Zca, 2=Zca+Zcb, 3=Zca+Zcb+Zcmp, 4=Zca+Zcb+Zcmp+Zcmt
parameter   M_EXTENSION         =  0;             // Integer Multiply/Divide extension: 0=none, 1=Zmmul, 2=M-extension
parameter   MUL_TYPE            =  3;             // Multiplier type: 1=1xCycle,  2=4xCycles,  3=16xCycles (valid only with Zmmul and M-extension)
parameter   DIV_TYPE            =  3;             // Divider type:    1=12xCycle, 2=17xCycles, 3=33xCycles (valid only with M-extension)
parameter   CCSR_EN             =  0;             // Enable Custom-CSR interface
parameter   SINGLE_CYCLE_BRANCH =  0;             // Taken-branch latency: 1=zero-bubble (max IPC, lower Fmax), 0=one-bubble (lower IPC, max Fmax)
parameter   ASYNC_RST_EN        =  1'b0;          // Reset architecture: 1=async active-low reset (default), 0=synchronous reset
parameter   MVENDORID           =  32'h00000000;  // JEDEC manufacturer ID of the chip vendor integrator.

arvern   #(.RV32E_EN            ( RV32E_EN             ),
           .NMI_EN              ( NMI_EN               ),
           .SU_MODE_EN          ( SU_MODE_EN           ),
           .ZICNTR_EN           ( ZICNTR_EN            ),
           .ZIHPM_NR            ( ZIHPM_NR             ),
           .B_EXTENSION         ( B_EXTENSION          ),
           .C_EXTENSION         ( C_EXTENSION          ),
           .M_EXTENSION         ( M_EXTENSION          ),
           .MUL_TYPE            ( MUL_TYPE             ),
           .DIV_TYPE            ( DIV_TYPE             ),
           .CCSR_EN             ( CCSR_EN              ),
           .SINGLE_CYCLE_BRANCH ( SINGLE_CYCLE_BRANCH  ),
           .ASYNC_RST_EN        ( ASYNC_RST_EN         ),
           .MVENDORID           ( MVENDORID            )) dut (

// AHB CLOCK & RESET
    .hclk_i                    ( clk_50mhz             ),
    .hresetn_i                 ( reset_n               ),
    .hclk_en_o                 ( dut_hclk_en           ),

// INSTRUCTION AHB BUS
    .inst_hrdata_i             ( inst_hrdata           ),
    .inst_hready_i             ( inst_hready           ),
    .inst_hresp_i              ( inst_hresp            ),

    .inst_haddr_o              ( inst_haddr            ),
    .inst_hburst_o             ( inst_hburst           ),
    .inst_hmastlock_o          ( inst_hmastlock        ),
    .inst_hprot_o              ( inst_hprot            ),
    .inst_hsize_o              ( inst_hsize            ),
    .inst_hsmode_o             ( inst_hsmode           ),
    .inst_htrans_o             ( inst_htrans           ),
    .inst_hwdata_o             ( inst_hwdata           ),
    .inst_hwrite_o             ( inst_hwrite           ),

// DATA AHB BUS
    .data_hrdata_i             ( data_hrdata           ),
    .data_hready_i             ( data_hready           ),
    .data_hresp_i              ( data_hresp            ),

    .data_haddr_o              ( data_haddr            ),
    .data_hburst_o             ( data_hburst           ),
    .data_hmastlock_o          ( data_hmastlock        ),
    .data_hprot_o              ( data_hprot            ),
    .data_hsize_o              ( data_hsize            ),
    .data_hsmode_o             ( data_hsmode           ),
    .data_htrans_o             ( data_htrans           ),
    .data_hwdata_o             ( data_hwdata           ),
    .data_hwrite_o             ( data_hwrite           ),

// INTERFACE TO CUSTOM CSR REGISTERS (disabled)
    .ccsr_rdata_i              ( 32'h00000000          ),
    .ccsr_bank_o               ( ccsr_bank_o_unused    ),
    .ccsr_reg_sel_o            ( ccsr_reg_sel_o_unused ),
    .ccsr_wdata_o              ( ccsr_wdata_o_unused   ),
    .ccsr_wen_o                ( ccsr_wen_o_unused     ),

// INTERRUPT INPUTS
    .irq_m_software_i          ( aclint_irq_m_software ),
    .irq_s_software_i          ( aclint_irq_s_software ),
    .irq_m_timer_i             ( aclint_irq_m_timer    ),
    .irq_m_external_i          ( 1'b0                  ),
    .irq_s_external_i          ( 1'b0                  ),
    .irq_platform_i            ( {14'h0000,
                                  periph0_irq_sw,
                                  periph0_irq_key     }), // [1]=SW IRQ (MIP[17]), [0]=KEY IRQ (MIP[16])

// OTHERS
    .hartid_i                  ( 8'h23                 ),
    .reset_vector_i            ( 32'h20000000          ),

// LOCKUP STATUS
    .lockup_o                  ( lockup_o_unused       ),

// NMI (SMRNMI)
    .nmi_i                     (  1'b0                 ),
    .nmi_vector_i              ( 32'h20000000          ),

// TIME INTERFACE (ZICNTR)
    .time_req_o                ( aclint_time_req       ),
    .time_gnt_i                ( aclint_time_gnt       ),
    .time_val_i                ( aclint_time_val       ),

// PLATFORM EVENTS (ZIHPM)
    .hpm_platform_events_i     (  8'h00                )

);


//=============================================================================
// 6)  BUS SYSTEM
//=============================================================================

ahb_bus_system #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_bus_system_inst (

// AHB CLOCK & RESET
    .hclk_i                    ( clk_50mhz                 ),
    .hresetn_i                 ( reset_n                   ),
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
//   On the DE0-Nano-SoC the only available clock is 50 MHz, so the always-on
//   low-frequency MTIME clock reuses the main hclk net.  MTIME therefore ticks
//   at 50 MHz (firmware must size mtimecmp deltas against 50e6, not 32768).
//   resetn_lf reuses reset_n (already sync-deasserted on clk_50mhz).
    .clk_lf_i                  ( clk_50mhz                 ),
    .resetn_lf_i               ( reset_n                   ),
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
// 7)  UNUSED I/O
//=============================================================================

assign GPIO_0          = 36'hzzzzzzzzz;
assign GPIO_1          = 36'hzzzzzzzzz;
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

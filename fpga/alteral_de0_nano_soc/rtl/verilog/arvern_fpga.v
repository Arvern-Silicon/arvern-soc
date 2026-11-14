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
wire        dbg_wakeup_unused;

// Debug stop-time request (core -> Belenos refresh / GPU freeze)
wire        dbg_stoptime;

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
wire        tck_pad_in;
wire        tms_pad_in;

// Belenos graphic controller: interrupt and panel nets (mapped to GPIO_0)
wire        blns_irq;
wire [15:0] lcd_d_in;
wire [15:0] lcd_d_out;
wire        lcd_d_oe;
wire        lcd_cs_n;
wire        lcd_rd_n;
wire        lcd_wr_n;
wire        lcd_dc;
wire        lcd_reset_n;
wire        lcd_on;

// PLIC external interrupts
wire        plic_irq_m_external;
wire        plic_irq_s_external;

// GPIO pins (mapped to GPIO_1[25:0])
localparam  GPIO_NR = 26;
wire [GPIO_NR-1:0] gpio_in;
wire [GPIO_NR-1:0] gpio_out;
wire [GPIO_NR-1:0] gpio_oe;

// Arduino GPIO pins (mapped to ARDUINO_IO[15:0])
localparam  GPIO_ARD_NR = 16;
wire [GPIO_ARD_NR-1:0] gpio_ard_in;
wire [GPIO_ARD_NR-1:0] gpio_ard_out;
wire [GPIO_ARD_NR-1:0] gpio_ard_oe;


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

assign      LED = periph0_led[7:0];


//=============================================================================
// 5)  ARVERN CORE
//=============================================================================

// Memory sizes
parameter                ROM_SIZE     = 48*1024;      // program memory @ 0x20000000 (executable)
parameter                SRAM_NX_SIZE = 48*1024;      // data SRAM      @ 0x80000000 (non-executable)

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
parameter   SU_MODE_EN          =  0;               // S-mode + U-mode privilege modes: off, M-mode software only
parameter   PMP_NR              =  0;               // Physical Memory Protection: writable entries (0, 4, 8 or 16; 0=absent): absent, M-mode software only
parameter   ZICNTR_EN           =  0;               // Zicntr extension enable (cycle, time, instret) (0=absent, 1=present)
parameter   ZIHPM_NR            =  0;               // Zihpm: number of HPM counters (0-8)
parameter   B_EXTENSION         =  4;               // Bit manipulation extension: 0=none, 1=Zbb, 2=Zbb+Zba, 3=Zbb+Zba+Zbs, 4=Zbb+Zba+Zbs+Zbc
parameter   C_EXTENSION         =  4;               // Compressed instructions extension: 0=none, 1=Zca, 2=Zca+Zcb, 3=Zca+Zcb+Zcmp, 4=Zca+Zcb+Zcmp+Zcmt
parameter   M_EXTENSION         =  2;               // Integer Multiply/Divide extension: 0=none, 1=Zmmul, 2=M-extension
parameter   MUL_TYPE            =  1;               // Multiplier type: 1=1xCycle,  2=4xCycles,  3=16xCycles (valid only with Zmmul and M-extension)
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
    .dbg_stoptime_o            ( dbg_stoptime            ),
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
    .irq_m_external_i          ( plic_irq_m_external     ),
    .irq_s_external_i          ( plic_irq_s_external     ),
    .irq_platform_i            ( {15'h0000, blns_irq}    ), // [0]=Belenos IRQ (MIP[16])

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
// 6)  BELENOS GRAPHIC CONTROLLER
//=============================================================================
//
//   Register port: 4KB at 0x10050000. VRAM window: BLNS_VRAM_WORDS*4 bytes at
//   0x82000000. Both are decoded by the bus system. Built-in DBI driver, 8080-16
//   PHY only (DIF_TYPE=2): it drives the LT24 module on GPIO_0, which has no TE
//   line. The BDI is not exported (tied per the IP contract).
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
wire              [23:0] lut_dout;

// SPI panel and BDI outputs (8080 driver only, BDI not exported)
wire                     spi_sck_unused;
wire                     spi_mosi_unused;
wire                     spi_cs_n_unused;
wire                     spi_dc_unused;
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


// Belenos configuration (classic persona, sized for this device)
parameter   BLNS_VRAM_WORDS       =  40960;          // VRAM depth in 32-bit words (1 - 2^24): one 320 x 240 frame at 16 bpp (two at 8 bpp), plus 10 KB off screen
localparam  BLNS_VRAM_AW          =  $clog2(BLNS_VRAM_WORDS); // VRAM word-address width, ceil(log2(BLNS_VRAM_WORDS)): derived
parameter   BLNS_VRAM_RD_REG_EN   =  0;              // VRAM read data registered: 0=used as the macro delivers it (M10K meets the board clock), 1=one more read cycle
parameter   BLNS_DIM_W            =  10;             // Width/height field width in bits (4-16): sizes up to 2^DIM_W - 1 pixels
parameter   BLNS_FRAME_NR         =  2;              // Number of frame pointers (1, 2 or 4)
parameter   BLNS_LUT_EN           =  1;              // Programmable colour LUT: 0=absent, 1=present
parameter   BLNS_LUT_BANKS        =  2;              // LUT banks of 256 entries (1 or 2, needs LUT_EN)
parameter   BLNS_GPU_EN           =  1;              // GPU: 0=absent, 1=present
parameter   BLNS_GPU_XY_EN        =  1;              // GPU x/y coordinates, clip window and affine blits (SET_DST XY, SET_XY, SET_CLIP, EXEC_TEXRECT, ...)
parameter   BLNS_GPU_GRAD_EN      =  1;              // GPU gradient fills (SET_GRAD)
parameter   BLNS_GPU_BLEND_EN     =  0;              // GPU alpha blending at 16 bpp (SET_ALPHA, EXPAND masks): off, 16 bpp frames do not fit the VRAM
parameter   BLNS_GPU_TRI_EN       =  0;              // GPU triangles (SET_V0..V2, EXEC_FILL TRI), needs GPU_XY_EN: off, does not fit the device
parameter   BLNS_GPU_RUNAHEAD_EN  =  0;              // GPU run-ahead: decode and set up the next primitive while one draws
parameter   BLNS_GPU_REMAP_EN     =  1;              // GPU palette remap (SET_AUX REMAP_TABLE, REMAP fields)
parameter   BLNS_GPU_MPLANE_EN    =  1;              // GPU mask plane on copies (SET_AUX MASK_PLANE, COPY MPLANE)
parameter   BLNS_GPU_IMM_EN       =  1;              // GPU immediate-data source, SET_SRC IMM
parameter   BLNS_GPU_FIFO_AW      =  3;              // GPU command FIFO: 2^GPU_FIFO_AW words (8, as the classic persona)
parameter   BLNS_GPU_LINE_EN      =  1;              // GPU lines: solid and dashed (EXEC_FILL LINE), needs GPU_XY_EN
parameter   BLNS_CURSOR_EN        =  1;              // Hardware cursor: 32x32, 2 bpp, shape in VRAM
parameter   BLNS_DIF_TYPE         =  2;              // Built-in display driver: 0=BDI (none), 1=DBI_SPI, 2=DBI_PAR (8080), 3=DBI_PAR_SPI: the LT24 on GPIO_0 is 8080
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
           .GPU_REMAP_EN        ( BLNS_GPU_REMAP_EN      ),
           .GPU_MPLANE_EN       ( BLNS_GPU_MPLANE_EN     ),
           .GPU_IMM_EN          ( BLNS_GPU_IMM_EN        ),
           .GPU_FIFO_AW         ( BLNS_GPU_FIFO_AW       ),
           .GPU_LINE_EN         ( BLNS_GPU_LINE_EN       ),
           .CURSOR_EN           ( BLNS_CURSOR_EN         ),
           .DIF_TYPE            ( BLNS_DIF_TYPE          ),
           .PRIV_CHECK_EN       ( BLNS_PRIV_CHECK_EN     ),
           .ASYNC_RST_EN        ( ASYNC_RST_EN           ))  belenos_inst (

// AHB CLOCK & RESET
    .hclk_i                ( clk_50mhz                                 ),
    .hresetn_i             ( hresetn                                   ),
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
    .lut_dout_i            ( lut_dout                                  ),

// DEBUG / INTERRUPT
    .dbg_stoptime_i        ( dbg_stoptime                              ),
    .irq_o                 ( blns_irq                                  ),

// 8080 PANEL
    .lcd_d_i               ( lcd_d_in                                  ),
    .lcd_d_o               ( lcd_d_out                                 ),
    .lcd_d_oe_o            ( lcd_d_oe                                  ),
    .lcd_cs_n_o            ( lcd_cs_n                                  ),
    .lcd_rd_n_o            ( lcd_rd_n                                  ),
    .lcd_wr_n_o            ( lcd_wr_n                                  ),
    .lcd_dc_o              ( lcd_dc                                    ),

// SPI PANEL
    .spi_sck_o             ( spi_sck_unused                            ),
    .spi_mosi_o            ( spi_mosi_unused                           ),
    .spi_miso_i            ( 1'b0                                      ),
    .spi_cs_n_o            ( spi_cs_n_unused                           ),
    .spi_dc_o              ( spi_dc_unused                             ),

// SHARED PANEL CONTROL
    .lcd_reset_n_o         ( lcd_reset_n                               ),
    .lcd_on_o              ( lcd_on                                    ),
    .lcd_te_i              ( 1'b0                                      ),

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

// VRAM: BLNS_VRAM_WORDS x 32, byte write enables, 1-cycle read latency
ram_32x38400 #(.ADDRW(BLNS_VRAM_AW), .WORDS(BLNS_VRAM_WORDS)) vram_inst (
    .clk_i                 ( clk_50mhz                                 ),
    .cen_i                 ( vram_cen                                  ),
    .wen_i                 ( vram_wen                                  ),
    .addr_i                ( vram_addr                                 ),
    .din_i                 ( vram_din                                  ),
    .dout_o                ( vram_dout                                 )
);

// LUT: 512 x 24 (two banks of 256 RGB888 entries), 1-cycle read latency
ram_24x512 lut_inst (
    .clk_i                 ( clk_50mhz                                 ),
    .cen_i                 ( lut_cen                                   ),
    .wen_i                 ( lut_wen                                   ),
    .addr_i                ( lut_addr                                  ),
    .din_i                 ( lut_din                                   ),
    .dout_o                ( lut_dout                                  )
);


//=============================================================================
// 7)  BUS SYSTEM
//=============================================================================

ahb_bus_system #(.ROM_SIZE(ROM_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .VRAM_WORDS(BLNS_VRAM_WORDS), .SU_MODE_EN(SU_MODE_EN), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_bus_system_inst (

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
    .periph0_tty_data_o        ( periph0_reg_01            ),
    .periph0_key_sw_val_o      ( periph0_reg_08            ),

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
    .aclint_time_val_o         ( aclint_time_val           ),

// BELENOS GRAPHIC CONTROLLER: register and VRAM-window ports
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

    .blns_hclk_en_i            ( blns_hclk_en              ),

// PLIC
    .plic_irq_m_external_o     ( plic_irq_m_external       ),
    .plic_irq_s_external_o     ( plic_irq_s_external       ),

// GPIO
    .gpio_i                    ( gpio_in                   ),
    .gpio_o                    ( gpio_out                  ),
    .gpio_oe_o                 ( gpio_oe                   ),

// GPIO (Arduino header)
    .gpio_ard_i                ( gpio_ard_in               ),
    .gpio_ard_o                ( gpio_ard_out              ),
    .gpio_ard_oe_o             ( gpio_ard_oe               )
);


//=============================================================================
// 7b) DEBUG TRANSPORT MODULE (selectable JTAG/UART/I2C/cJTAG wrapper) - DEBUG_EN only
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
        .trst_n_i         ( 1'b1              ),
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
                                | jtag_tck   | jtag_tms    | jtag_tdi
                                | uart_rx    | i2c_scl     | i2c_sda
                                | cjtag_tckc | cjtag_tmsc_i;

end
endgenerate

//-----------------------------------------------------------------------------
// GPIO_1 pin map.
//
// Pins 1-28: GPIO pins 0-25 (apb_gpio, section 6d).
// Pins 31-38: DTM PHY (active only when DEBUG_EN=1; else outputs Hi-Z). Only
// one transport is wired at a time (selected by DTM_TYPE at build); cJTAG uses
// the JTAG TCK/TMS pins as TCKC/TMSC. There is no TRST: the TAP resets through
// TMS and the debug reset.
// Pin 40 is driven high, the 3.3V reference for a debug adapter's VTref, and
// pin 39 low, a ground reference beside it: I/O pins, a few mA at most,
// references, not a supply. Pins 31-40 have the layout of the Tang Console
// SDRAM1 header (J10) pins 10-1, so one debug cable fits both boards. The next
// pair differs (pins 29/30 here are +3.3V / GND, J10 pins 12/11 are GND / +5V):
// a shared cable stops at the 2x5.
//
// Board orientation (DE0-Nano-SoC User Manual Fig 2-1 / Fig 3-18, top view):
//
//   * Pin 1 is on the Ethernet/buttons side; the high pins (39/40) point
//     toward the board interior (USB-Blaster / HPS User LED).
//
//                            <- Gigabit Ethernet RJ45 (adjacent board edge) ->
//                            <- KEY0/KEY1 buttons + 2x5 ADC header near pin 1 ->
//                                                 ||
//     odd (left)                                  \/                    even (right)
//    +-------------------------------------------+   +-------------------------------------------+
//    |  1  GPIO_1[ 0]  Y15   GPIO[ 0]            | . |  2  GPIO_1[ 1]  AG28  GPIO[ 1]            |
//    |  3  GPIO_1[ 2]  AA15  GPIO[ 2]            | . |  4  GPIO_1[ 3]  AH27  GPIO[ 3]            |
//    |  5  GPIO_1[ 4]  AG26  GPIO[ 4]            | . |  6  GPIO_1[ 5]  AH24  GPIO[ 5]            |
//    |  7  GPIO_1[ 6]  AF23  GPIO[ 6]            | . |  8  GPIO_1[ 7]  AE22  GPIO[ 7]            |
//    |  9  GPIO_1[ 8]  AF21  GPIO[ 8]            | . | 10  GPIO_1[ 9]  AG20  GPIO[ 9]            |
//    | 11  +5V                                   | . | 12  GND                                   |
//    | 13  GPIO_1[10]  AG19  GPIO[10]            | . | 14  GPIO_1[11]  AF20  GPIO[11]            |
//    | 15  GPIO_1[12]  AC23  GPIO[12]            | . | 16  GPIO_1[13]  AG18  GPIO[13]            |
//    | 17  GPIO_1[14]  AH26  GPIO[14]            | . | 18  GPIO_1[15]  AA19  GPIO[15]            |
//    | 19  GPIO_1[16]  AG24  GPIO[16]            | . | 20  GPIO_1[17]  AF25  GPIO[17]            |
//    | 21  GPIO_1[18]  AH23  GPIO[18]            | . | 22  GPIO_1[19]  AG23  GPIO[19]            |
//    | 23  GPIO_1[20]  AE19  GPIO[20]            | . | 24  GPIO_1[21]  AF18  GPIO[21]            |
//    | 25  GPIO_1[22]  AD19  GPIO[22]            | . | 26  GPIO_1[23]  AE20  GPIO[23]            |
//    | 27  GPIO_1[24]  AE24  GPIO[24]            | . | 28  GPIO_1[25]  AD20  GPIO[25]            |
//    | 29  +3.3V                                 | . | 30  GND                                   |
//    | 31  GPIO_1[26]  AF22  UART_RX             | . | 32  GPIO_1[27]  AH22  UART_TX             |
//    | 33  GPIO_1[28]  AH19  I2C_SCL             | . | 34  GPIO_1[29]  AH21  I2C_SDA             |
//    | 35  GPIO_1[30]  AG21  JTAG_TDO            | . | 36  GPIO_1[31]  AH18  JTAG_TDI            |
//    | 37  GPIO_1[32]  AD23  JTAG_TCK/CJTAG_TCKC | . | 38  GPIO_1[33]  AE23  JTAG_TMS/CJTAG_TMSC |
//    | 39  GPIO_1[34]  AA18  0 (GND ref)         | . | 40  GPIO_1[35]  AC22  1 (3.3V ref)        |
//    +-------------------------------------------+   +-------------------------------------------+
//                                                  /\
//                           <- toward board interior: USB-Blaster / HPS User LED ->
//
//-----------------------------------------------------------------------------

wire jtag_tdo_pad_rdata_unused;   // TDO is push-pull output: pad read-back unused
wire uart_tx_pad_rdata_unused;    // UART_TX is push-pull output: pad read-back unused

// UART_TX pad source: normally the DTM's uart_tx; in UART_LOOPBACK bring-up mode
// it is the raw UART_RX pad value (combinational passthrough) so a host loopback
// exercises adapter TX -> wire -> UART_RX pad (pin 31) -> fabric -> UART_TX pad
// (pin 32) -> wire -> adapter RX, i.e. the entire physical link minus the DTM. The
// pad is driven whenever the DTM is present OR loopback is on (so a DEBUG_EN=0
// wiring-check build still drives TX).
wire uart_tx_pad_din = UART_LOOPBACK ? uart_rx : uart_tx;
wire uart_tx_pad_oe  = DEBUG_ON || (UART_LOOPBACK != 0);

// Pin 37 carries JTAG TCK or cJTAG TCKC, pin 38 JTAG TMS or cJTAG TMSC: one pad
// each, its input feeding both transports' nets (the DTM consumes only the
// selected one).
io_buf io_buf_tck_tckc (    // [32] JTAG_TCK / CJTAG_TCKC (input)  -> AD23 / JP7 pin 37
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( tck_pad_in                ),
    .dataio  ( GPIO_1[32]                ));

assign jtag_tck   = tck_pad_in;
assign cjtag_tckc = tck_pad_in;

// TMSC is push-pull and bidirectional: the target drives only in the TDO phase
// while TCKC is low. The pad keeper (bus hold) sustains the level while nobody
// drives, which the OScan1 sampling contract depends on. The JTAG, UART and I2C
// transports never enable this driver.
wire tmsc_pad_do = cjtag_tmsc_o;
wire tmsc_pad_oe = DEBUG_ON && cjtag_tmsc_oe;

io_buf io_buf_tms_tmsc (    // [33] JTAG_TMS (input) / CJTAG_TMSC (inout, bus-hold)  -> AE23 / JP7 pin 38
    .datain  ( tmsc_pad_do               ),
    .oe      ( tmsc_pad_oe               ),
    .dataout ( tms_pad_in                ),
    .dataio  ( GPIO_1[33]                ));

assign jtag_tms     = tms_pad_in;
assign cjtag_tmsc_i = tms_pad_in;

io_buf io_buf_jtag_tdi (    // [31] JTAG_TDI (input)  -> AH18 / JP7 pin 36
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( jtag_tdi                  ),
    .dataio  ( GPIO_1[31]                ));

io_buf io_buf_jtag_tdo (    // [30] JTAG_TDO (output, tri-stated by tdo_oe)  -> AG21 / JP7 pin 35
    .datain  ( jtag_tdo                  ),
    .oe      ( DEBUG_ON && jtag_tdo_oe   ),
    .dataout ( jtag_tdo_pad_rdata_unused ),
    .dataio  ( GPIO_1[30]                ));

io_buf io_buf_i2c_scl (     // [28] I2C_SCL (inout, open-drain)  -> AH19 / JP7 pin 33
    .datain  ( 1'b0                      ),
    .oe      ( DEBUG_ON && i2c_scl_pd    ),
    .dataout ( i2c_scl                   ),
    .dataio  ( GPIO_1[28]                ));

io_buf io_buf_i2c_sda (     // [29] I2C_SDA (inout, open-drain)  -> AH21 / JP7 pin 34
    .datain  ( 1'b0                      ),
    .oe      ( DEBUG_ON && i2c_sda_pd    ),
    .dataout ( i2c_sda                   ),
    .dataio  ( GPIO_1[29]                ));

io_buf io_buf_uart_tx (     // [27] UART_TX (output)  -> AH22 / JP7 pin 32
    .datain  ( uart_tx_pad_din           ),
    .oe      ( uart_tx_pad_oe            ),
    .dataout ( uart_tx_pad_rdata_unused  ),
    .dataio  ( GPIO_1[27]                ));

io_buf io_buf_uart_rx (     // [26] UART_RX (input)  -> AF22 / JP7 pin 31
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( uart_rx                   ),
    .dataio  ( GPIO_1[26]                ));

// GPIO_1[n] is GPIO pin n: driven from gpio_out while gpio_oe is set,
// read back through the pad input whatever the direction.
genvar gi;
generate
for (gi = 0; gi < GPIO_NR; gi = gi + 1) begin : g_gpio_pin
    io_buf io_buf_gpio (
        .datain  ( gpio_out[gi]              ),
        .oe      ( gpio_oe[gi]               ),
        .dataout ( gpio_in[gi]               ),
        .dataio  ( GPIO_1[gi]                ));
end
endgenerate

//=============================================================================
// 7c) GPIO_0 pin map: BELENOS DISPLAY PANELS
//=============================================================================

//-----------------------------------------------------------------------------
// GPIO_0 pin map: Terasic LT24 module (ILI9341, 8080-16 bus).
//
// The LT24 plugs directly onto GPIO_0. Its touch controller (ADC_*, PENIRQ_N)
// is not used and stays high-Z, as does every pin not listed as LT24_*.
//
//     odd                                    even
//    +----------------------------------+   +------------------------------------+
//    |  1  GPIO_0[ 0]  V12   (unused)   | . |  2  GPIO_0[ 1]  AF7   (unused)     |
//    |  3  GPIO_0[ 2]  W12   (unused)   | . |  4  GPIO_0[ 3]  AF8   (unused)     |
//    |  5  GPIO_0[ 4]  Y8    (unused)   | . |  6  GPIO_0[ 5]  AB4   LT24_D3      |
//    |  7  GPIO_0[ 6]  W8    LT24_D2    | . |  8  GPIO_0[ 7]  Y4    LT24_D1      |
//    |  9  GPIO_0[ 8]  Y5    LT24_D0    | . | 10  GPIO_0[ 9]  U11   (unused)     |
//    | 11  +5V                          | . | 12  GND                            |
//    | 13  GPIO_0[10]  T8    LT24_RD_N  | . | 14  GPIO_0[11]  T12   LT24_WR_N    |
//    | 15  GPIO_0[12]  AH5   LT24_RS    | . | 16  GPIO_0[13]  AH6   LT24_D4      |
//    | 17  GPIO_0[14]  AH4   LT24_D5    | . | 18  GPIO_0[15]  AG5   LT24_D6      |
//    | 19  GPIO_0[16]  AH3   LT24_D7    | . | 20  GPIO_0[17]  AH2   LT24_D8      |
//    | 21  GPIO_0[18]  AF4   LT24_D9    | . | 22  GPIO_0[19]  AG6   LT24_D10     |
//    | 23  GPIO_0[20]  AF5   LT24_D11   | . | 24  GPIO_0[21]  AE4   LT24_D12     |
//    | 25  GPIO_0[22]  T13   LT24_D13   | . | 26  GPIO_0[23]  T11   LT24_D14     |
//    | 27  GPIO_0[24]  AE7   LT24_D15   | . | 28  GPIO_0[25]  AF6   LT24_CS_N    |
//    | 29  +3.3V                        | . | 30  GND                            |
//    | 31  GPIO_0[26]  AF9   (unused)   | . | 32  GPIO_0[27]  AE8   (unused)     |
//    | 33  GPIO_0[28]  AD10  (unused)   | . | 34  GPIO_0[29]  AE9   (unused)     |
//    | 35  GPIO_0[30]  AD11  (unused)   | . | 36  GPIO_0[31]  AF10  (unused)     |
//    | 37  GPIO_0[32]  AD12  (unused)   | . | 38  GPIO_0[33]  AE11  LT24_RESET_N |
//    | 39  GPIO_0[34]  AF11  ADC_CS_N=1 | . | 40  GPIO_0[35]  AE12  LT24_LCD_ON  |
//    +----------------------------------+   +------------------------------------+
//
// The LT24 has no TE line to the FPGA: Belenos' lcd_te_i is tied low.
//-----------------------------------------------------------------------------

// 8080 data bus D[15:0]: bidirectional, driven while lcd_d_oe is set
io_buf io_buf_lcd_d00 (.datain(lcd_d_out[ 0]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 0]), .dataio(GPIO_0[ 8]));
io_buf io_buf_lcd_d01 (.datain(lcd_d_out[ 1]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 1]), .dataio(GPIO_0[ 7]));
io_buf io_buf_lcd_d02 (.datain(lcd_d_out[ 2]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 2]), .dataio(GPIO_0[ 6]));
io_buf io_buf_lcd_d03 (.datain(lcd_d_out[ 3]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 3]), .dataio(GPIO_0[ 5]));
io_buf io_buf_lcd_d04 (.datain(lcd_d_out[ 4]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 4]), .dataio(GPIO_0[13]));
io_buf io_buf_lcd_d05 (.datain(lcd_d_out[ 5]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 5]), .dataio(GPIO_0[14]));
io_buf io_buf_lcd_d06 (.datain(lcd_d_out[ 6]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 6]), .dataio(GPIO_0[15]));
io_buf io_buf_lcd_d07 (.datain(lcd_d_out[ 7]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 7]), .dataio(GPIO_0[16]));
io_buf io_buf_lcd_d08 (.datain(lcd_d_out[ 8]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 8]), .dataio(GPIO_0[17]));
io_buf io_buf_lcd_d09 (.datain(lcd_d_out[ 9]), .oe(lcd_d_oe), .dataout(lcd_d_in[ 9]), .dataio(GPIO_0[18]));
io_buf io_buf_lcd_d10 (.datain(lcd_d_out[10]), .oe(lcd_d_oe), .dataout(lcd_d_in[10]), .dataio(GPIO_0[19]));
io_buf io_buf_lcd_d11 (.datain(lcd_d_out[11]), .oe(lcd_d_oe), .dataout(lcd_d_in[11]), .dataio(GPIO_0[20]));
io_buf io_buf_lcd_d12 (.datain(lcd_d_out[12]), .oe(lcd_d_oe), .dataout(lcd_d_in[12]), .dataio(GPIO_0[21]));
io_buf io_buf_lcd_d13 (.datain(lcd_d_out[13]), .oe(lcd_d_oe), .dataout(lcd_d_in[13]), .dataio(GPIO_0[22]));
io_buf io_buf_lcd_d14 (.datain(lcd_d_out[14]), .oe(lcd_d_oe), .dataout(lcd_d_in[14]), .dataio(GPIO_0[23]));
io_buf io_buf_lcd_d15 (.datain(lcd_d_out[15]), .oe(lcd_d_oe), .dataout(lcd_d_in[15]), .dataio(GPIO_0[24]));

// 8080 control and panel controls: push-pull outputs through the pad buffer,
// output enable tied high, pad read-back unused.
wire  [6:0] panel_pad_rdata_unused;

io_buf io_buf_lt24_cs_n  (.datain(lcd_cs_n),    .oe(1'b1), .dataout(panel_pad_rdata_unused[ 0]), .dataio(GPIO_0[25])); // LT24_CS_N
io_buf io_buf_lt24_rd_n  (.datain(lcd_rd_n),    .oe(1'b1), .dataout(panel_pad_rdata_unused[ 1]), .dataio(GPIO_0[10])); // LT24_RD_N
io_buf io_buf_lt24_wr_n  (.datain(lcd_wr_n),    .oe(1'b1), .dataout(panel_pad_rdata_unused[ 2]), .dataio(GPIO_0[11])); // LT24_WR_N
io_buf io_buf_lt24_rs    (.datain(lcd_dc),      .oe(1'b1), .dataout(panel_pad_rdata_unused[ 3]), .dataio(GPIO_0[12])); // LT24_RS (D/C)
io_buf io_buf_lt24_rst_n (.datain(lcd_reset_n), .oe(1'b1), .dataout(panel_pad_rdata_unused[ 4]), .dataio(GPIO_0[33])); // LT24_RESET_N
io_buf io_buf_lt24_on    (.datain(lcd_on),      .oe(1'b1), .dataout(panel_pad_rdata_unused[ 5]), .dataio(GPIO_0[35])); // LT24_LCD_ON
io_buf io_buf_adc_cs_n   (.datain(1'b1),        .oe(1'b1), .dataout(panel_pad_rdata_unused[ 6]), .dataio(GPIO_0[34])); // LT24 touch-ADC CS_N held high


//=============================================================================
// 7d) Arduino header pin map (D0-D13/SDA/SCL)
//=============================================================================

//-----------------------------------------------------------------------------
// Arduino Uno R3 header map. ARDUINO_IO[15:0] are the 16 pins of the second
// GPIO: GPIO_ARD[n] is pin n of apb_gpio_ard_inst (base 0x10042000, PLIC
// source 32+n). RESET is unused (Hi-Z).
//
// Board orientation (DE0-Nano-SoC mechanical drawing, top view): the four
// single-row headers sit between the two 2x20 headers, in two columns.
//
//   * Power (JP5) and analog (JP6) form the column next to GPIO_1 (the 2x20
//     header whose pin 1 is near the Ethernet jack and the 2x5 ADC header).
//   * Digital (JP2, JP3) form the column next to GPIO_0 (the LT24 header).
//   * Every Arduino header has its pin 1 (square pad) at the end away from the
//     Ethernet edge: each column reads pin 1..N walking toward the RJ45 jack,
//     in the Arduino order NC..VIN, A0..A5 and SCL..D8, D7..D0.
//   * The 2x3 ICSP header (JP4) between the columns is not used.
//
//   The analog pins go to the on-board LTC2308 ADC, not to the FPGA. Power:
//   VIN is 9 V (VCC9); IOREF and 3.3V are both the board's 3.3 V (VCC3P3).
//
//                               <- board edge opposite the Ethernet jack ->
//
//   <- GPIO_1 side                                       GPIO_0 side (LT24) ->
//   JP5 (power, 8 pins)                                  JP2 (digital, 10 pins)
//   +----------------------------------------------+     +----------------------------------------------+
//   |  1  NC                                       |     |  1  SCL   ARDUINO_IO[15]  AG11  GPIO_ARD[15] |
//   |  2  IOREF +3.3V                              |     |  2  SDA   ARDUINO_IO[14]  AH9   GPIO_ARD[14] |
//   |  3  RESET ARDUINO_RESET_N  AH7   (unused)    |     |  3  AREF  ADC reference (VREF)               |
//   |  4  3.3V  +3.3V                              |     |  4  GND                                      |
//   |  5  5V    +5V                                |     |  5  D13   ARDUINO_IO[13]  AH12  GPIO_ARD[13] |
//   |  6  GND                                      |     |  6  D12   ARDUINO_IO[12]  AH11  GPIO_ARD[12] |
//   |  7  GND                                      |     |  7  D11   ARDUINO_IO[11]  AG16  GPIO_ARD[11] |
//   |  8  VIN   +9V                                |     |  8  D10   ARDUINO_IO[10]  AF15  GPIO_ARD[10] |
//   +----------------------------------------------+     |  9  D9    ARDUINO_IO[ 9]  AE15  GPIO_ARD[ 9] |
//                                                        | 10  D8    ARDUINO_IO[ 8]  AF17  GPIO_ARD[ 8] |
//   JP6 (analog, 6 pins)                                 +----------------------------------------------+
//   +----------------------------------------------+
//   |  1  A0    ADC_IN0  (LTC2308 ADC)             |     JP3 (digital, 8 pins)
//   |  2  A1    ADC_IN1  (LTC2308 ADC)             |     +----------------------------------------------+
//   |  3  A2    ADC_IN2  (LTC2308 ADC)             |     |  1  D7    ARDUINO_IO[ 7]  AH8   GPIO_ARD[ 7] |
//   |  4  A3    ADC_IN3  (LTC2308 ADC)             |     |  2  D6    ARDUINO_IO[ 6]  AG8   GPIO_ARD[ 6] |
//   |  5  A4    ADC_IN4  (LTC2308 ADC)             |     |  3  D5    ARDUINO_IO[ 5]  U13   GPIO_ARD[ 5] |
//   |  6  A5    ADC_IN5  (LTC2308 ADC)             |     |  4  D4    ARDUINO_IO[ 4]  U14   GPIO_ARD[ 4] |
//   +----------------------------------------------+     |  5  D3    ARDUINO_IO[ 3]  AG9   GPIO_ARD[ 3] |
//                                                        |  6  D2    ARDUINO_IO[ 2]  AG10  GPIO_ARD[ 2] |
//                                                        |  7  D1    ARDUINO_IO[ 1]  AF13  GPIO_ARD[ 1] |
//                                                        |  8  D0    ARDUINO_IO[ 0]  AG13  GPIO_ARD[ 0] |
//                                                        +----------------------------------------------+
//
//                                 <- Gigabit Ethernet RJ45 (board edge) ->
//
//-----------------------------------------------------------------------------

// ARDUINO_IO[n] is Arduino GPIO pin n (second apb_gpio, PLIC source 32+n).
generate
for (gi = 0; gi < GPIO_ARD_NR; gi = gi + 1) begin : g_gpio_ard_pin
    io_buf io_buf_gpio_ard (
        .datain  ( gpio_ard_out[gi]          ),
        .oe      ( gpio_ard_oe[gi]           ),
        .dataout ( gpio_ard_in[gi]           ),
        .dataio  ( ARDUINO_IO[gi]            ));
end
endgenerate


//=============================================================================
// 8)  UNUSED I/O
//=============================================================================

// GPIO_1 bits [25:0] belong to the GPIO pad buffers and [33:26] to the DTM;
// GPIO_0 bits [8:5], [25:10] and [35:33] to the LT24; ARDUINO_IO to the
// second GPIO. The debug references are driven and the remaining bits idled
// here.
assign GPIO_1[35]      = 1'b1;   // JP7 pin 40: 3.3V reference for an adapter's VTref
assign GPIO_1[34]      = 1'b0;   // JP7 pin 39: ground reference
assign GPIO_0[4:0]     = {5{1'bz}};
assign GPIO_0[9]       = 1'bz;
assign GPIO_0[32:26]   = {7{1'bz}};
assign ARDUINO_RESET_N =  1'hz;
assign ADC_CONVST      =  1'hz;
assign ADC_SCK         =  1'hz;
assign ADC_SDI         =  1'hz;


//=============================================================================
// 9)  LINT CLEANUP
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
wire [19:0] blnsr_haddr_hi_unused = blnsr_haddr[31:12];                  // Belenos register port: 4KB
wire [29-BLNS_VRAM_AW:0] blnsv_haddr_hi_unused = blnsv_haddr[31:BLNS_VRAM_AW+2];  // VRAM window: BLNS_VRAM_WORDS words


endmodule // arvern_fpga

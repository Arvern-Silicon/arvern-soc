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
// Module Description : arvern FPGA Top-level for the Sipeed Tang Console 138K
//                      (Tang Mega 138K SoM, GW5AST-LV138PG484A) with the Sipeed
//                      4.3-inch 480 x 272 RGB panel on the 40-pin LCD connector,
//                      or (BOARD_HDMI = 1) a monitor on the HDMI connector J13
//----------------------------------------------------------------------------

module arvern_fpga (

  //-----------------------------
  // USER CLOCK
  //-----------------------------
  input         SYS_CLK,                // 50 MHz oscillator

  //-----------------------------
  // USER INTERFACE
  //-----------------------------
  input   [1:0] KEY,                    // user buttons (active low, pulled up in the pad): [0] reset
  output  [1:0] LED,                    // SoM LEDs on the DONE / READY pins (active high)

  //-----------------------------
  // PMOD0: GENERAL PURPOSE (GPIO 0-7)
  //-----------------------------
  inout   [7:0] PMOD0,

  //-----------------------------
  // PMOD1: GENERAL PURPOSE (GPIO 8-15)
  //-----------------------------
  inout   [7:0] PMOD1,

  //-----------------------------
  // AUDIO: ONBOARD MAX98357A AMPLIFIER (the Pmod I2S2 plugs into PMOD1)
  //-----------------------------
  output        I2S_BCLK,
  output        I2S_LRCK,
  output        I2S_DOUT,
  output        PA_EN,                  // amplifier SD_MODE: low = shutdown (51K pull-down on the board)

  //-----------------------------
  // MICROSD SLOT (J22) IN SPI MODE (shared with the BL616 through an analog
  // switch the BL616 side selects)
  //-----------------------------
  output        SD_CLK,
  output        SD_DAT3,                // chip select, active low
  inout         SD_CMD,                 // MOSI
  input         SD_DAT0,                // MISO
  input         SD_DET,                 // card detect: low with a card in (FPGA pull-up)

  //-----------------------------
  // SDRAM1 HEADER (J10): 3.3V / GND REFERENCES on pins 1 / 2, DEBUG
  // TRANSPORT on pins 3-10 (index = header pin)
  //-----------------------------
  inout  [10:1] SDRAM1,

  //-----------------------------
  // USB-A PORT J15 (USB_A1): low-speed USB host for a HID device (gamepad,
  // keyboard, mouse); 15K pull-downs on the board
  //-----------------------------
  inout         USB1_DP,
  inout         USB1_DM,

  //-----------------------------
  // USB-A PORT J16 (USB_A2): second low-speed USB host, same pull-downs
  //-----------------------------
  inout         USB2_DP,
  inout         USB2_DM,

  //-----------------------------
  // RGB PANEL (40-pin LCD connector)
  //-----------------------------
  output        LCD_CLK,
  output        LCD_DE,
  output        LCD_HSYNC,
  output        LCD_VSYNC,
  output  [7:0] LCD_R,
  output  [7:0] LCD_G,
  output  [7:0] LCD_B,
  output        LCD_BL,                 // backlight enable

  //-----------------------------
  // HDMI CONNECTOR J13 (BOARD_HDMI = 1): TMDS lanes, hot-plug detect
  //-----------------------------
  output  [2:0] HDMI_D_P,               // data lanes 0..2 (blue, green, red)
  output  [2:0] HDMI_D_N,
  output        HDMI_CLK_P,             // clock lane
  output        HDMI_CLK_N,
  input         HDMI_HPD,               // hot-plug detect (high: a sink is attached)
  output        HDMI_PWR_SAV,           // TMDS line resistors on (high), off in the LCD build
  inout         HDMI_DDC_SCL,           // DDC (I2C to the monitor's EDID): open-drain,
  inout         HDMI_DDC_SDA,           // pulled up on the board

  //-----------------------------
  // UART TO THE ONBOARD BL616
  //-----------------------------
  input         UART_RXD,
  output        UART_TXD
);

//=============================================================================
// 1)  INTERNAL WIRES/REGISTERS/PARAMETERS DECLARATION
//=============================================================================

// Display: 0 = the RGB panel (Belenos DIF_TYPE 4, pixel clock clk_sys);
//          1 = HDMI on J13 (DIF_TYPE 5, 720 x 480 at 60 Hz: clk_pix 27.03 MHz is the
//              pixel clock, clk_ser 135.16 MHz the serialiser clock).
//         (clk_sys is 33.854 MHz in both builds).
parameter   BOARD_HDMI          =  0;
// HDMI build: 1 = the I2S stream (apb_i2s) also goes to the TV as HDMI audio
parameter   BOARD_HDMI_AUDIO    =  1;
// Panel build: 1 = the panel timing on its own pixel clock clk_pix = 11.285 MHz
// (second output of the system PLL, Belenos DPI_PCLK_SEP = 1, CLK_DIV = 0);
// 0 = on clk_sys with CLK_DIV = 2
parameter   BOARD_LCD_PCLK_SEP  =  1;

// clk_sys frequency in Hz, from the PLL settings (gowin/sys_pll.mod: 50 MHz
// x 16.25 / 24: 44.1 kHz audio is clk_sys / 768)
localparam  CLK_SYS_HZ          =  33854167;

// Clock & Resets
wire        clk_sys;
wire        clk_pix;
wire        clk_lcd_pix;
wire        clk_ser;
wire        presetn_pix;

// The apb_i2s stream to the HDMI audio input (board I/O AUDIO_OUT = HDMI)
wire        i2s_hdmi_bclk;
wire        i2s_hdmi_lrck;
wire        i2s_hdmi_sd;
wire        pll_lock;
wire        sys_pll_lock;
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

// DTM PHY nets (mapped to the SDRAM1 header; see the DTM section below)
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

// Belenos graphic controller: interrupt and display-interface nets
wire        blns_irq;

// PLIC external interrupts
wire        plic_irq_m_external;
wire        plic_irq_s_external;

// GPIO on the PMOD0 and PMOD1 headers (pins 0-7 and 8-15)
wire [15:0] gpio_pmod_in;
wire [15:0] gpio_pmod_out;
wire [15:0] gpio_pmod_oe;

// microSD command line (SPI MOSI) pad
wire        sd_cmd_out;
wire        sd_cmd_oe;
wire        sd_cmd_in;


//=============================================================================
// 2)  CLOCK AND RESET GENERATION
//=============================================================================

// System PLL: 50 MHz in, VCO 812.5 MHz, clk_sys = 33.854 MHz, the clock of the
// whole design (gowin/sys_pll.mod). The HDMI build adds a second PLL
// (gowin/hdmi_pll.mod): clk_ser = 135.16 MHz for the TMDS serialisers; the
// Belenos pixel clock clk_pix = 27.03 MHz is clk_ser divided by 5 in the
// high-speed clock divider, so that the OSER10s' PCLK edges coincide with
// FCLK edges. Belenos crosses between clk_sys and clk_pix (DPI_PCLK_SEP = 1).
generate
    if ((BOARD_HDMI == 0) && (BOARD_LCD_PCLK_SEP != 0)) begin : g_sys_pll_lcd
        sys_pll_lcd u_sys_pll (
            .clkin   ( SYS_CLK      ),
            .clkout0 ( clk_sys      ),
            .clkout1 ( clk_lcd_pix  ),
            .lock    ( sys_pll_lock )
        );
    end else begin : g_sys_pll
        sys_pll u_sys_pll (
            .clkin   ( SYS_CLK      ),
            .clkout0 ( clk_sys      ),
            .lock    ( sys_pll_lock )
        );
        assign clk_lcd_pix = 1'b0;
    end

    if (BOARD_HDMI != 0) begin : g_hdmi_pll
        wire hdmi_pll_lock;
        hdmi_pll u_hdmi_pll (
            .clkin   ( SYS_CLK       ),
            .clkout0 ( clk_ser       ),
            .lock    ( hdmi_pll_lock )
        );
        CLKDIV #(.DIV_MODE("5")) u_pix_div (
            .HCLKIN  ( clk_ser       ),
            .RESETN  ( hdmi_pll_lock ),
            .CALIB   ( 1'b0          ),
            .CLKOUT  ( clk_pix       )
        );
        assign pll_lock = sys_pll_lock & hdmi_pll_lock;
    end else begin : g_lcd_pll
        assign pll_lock = sys_pll_lock;
        assign clk_pix  = (BOARD_LCD_PCLK_SEP != 0) ? clk_lcd_pix : clk_sys;
        assign clk_ser  = 1'b0;
    end
endgenerate

// KEY[0] (active low on the board) and the PLL lock make the raw async reset
wire   reset_in_n = KEY[0] & pll_lock;

// Low-frequency timebase for the ACLINT MTIME counter.
//
// clk_lf is sampled AS DATA by the ACLINT's tick detector in BOTH LF_SYNC_EN
// modes, and with LF_SYNC_EN=1 that tick is MTIME's clock enable -- no tick
// means no time at all. Every clk_lf phase must therefore span at least two
// hclk_aon periods; the IP quotes R >= 10 unless the duty cycle is bounded.
// Toggling on a terminal count gives an exact 50/50 duty:
//   clk_lf = clk_sys / (2 * (CLK_LF_TC + 1))
// 2 -> /6 -> 5.642 MHz, each phase three hclk_aon periods. The same timebase
// serves the board and the simulation, so firmware has one set of mtime units
// (1 tick = 177 ns).
localparam [9:0] CLK_LF_TC = 10'd2;

reg  [9:0]  clk_lf_cnt;
reg         clk_lf_q;

always @(posedge clk_sys or negedge reset_in_n)
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
    .hclk_i        ( clk_sys       ),

    .resetn_lf_o   ( resetn_lf     ),
    .dbgresetn_o   ( dbgresetn     ),
    .hresetn_o     ( hresetn       ));

// Pixel-domain reset (HDMI build): hresetn through two clk_pix flops, so it
// asserts and releases on clk_pix
reg   [1:0] presetn_pix_q;

always @(posedge clk_pix or negedge reset_in_n)
  if (!reset_in_n) presetn_pix_q <= 2'b00;
  else             presetn_pix_q <= {presetn_pix_q[0], hresetn};

assign presetn_pix = presetn_pix_q[1];


//=============================================================================
// 4)  PERIPHERAL I/O MAPPING
//=============================================================================

// Board I/O: LEDs / Keys / Switches (active register outputs for testbench probing)
wire  [7:0] periph0_led;            // CPU-controlled LEDs
wire [31:0] periph0_reg_01;         // TTY data register
wire [31:0] periph0_reg_08;         // Key/Switch value register

assign      LED = periph0_led[1:0];

// USB hosts: USB-A J15 (usb0) and J16 (usb1), D+/D- pad read-back,
// driven value, output enable
wire        usb0_dp_in;
wire        usb0_dm_in;
wire        usb0_dp_out;
wire        usb0_dm_out;
wire        usb0_oe;
wire        usb1_dp_in;
wire        usb1_dm_in;
wire        usb1_dp_out;
wire        usb1_dm_out;
wire        usb1_oe;

io_buf io_buf_usb1_dp (     // USB-A J15 D+  -> M15
    .datain  ( usb0_dp_out        ),
    .oe      ( usb0_oe            ),
    .dataout ( usb0_dp_in         ),
    .dataio  ( USB1_DP            ));

io_buf io_buf_usb1_dm (     // USB-A J15 D-  -> M16
    .datain  ( usb0_dm_out        ),
    .oe      ( usb0_oe            ),
    .dataout ( usb0_dm_in         ),
    .dataio  ( USB1_DM            ));

io_buf io_buf_usb2_dp (     // USB-A J16 D+  -> H13
    .datain  ( usb1_dp_out        ),
    .oe      ( usb1_oe            ),
    .dataout ( usb1_dp_in         ),
    .dataio  ( USB2_DP            ));

io_buf io_buf_usb2_dm (     // USB-A J16 D-  -> G13
    .datain  ( usb1_dm_out        ),
    .oe      ( usb1_oe            ),
    .dataout ( usb1_dm_in         ),
    .dataio  ( USB2_DM            ));



//=============================================================================
// 5)  ARVERN CORE
//=============================================================================

// Memory sizes
parameter                ROM_SIZE       = 160*1024;    // program memory @ 0x20000000 (executable)
parameter                SRAM_NX_SIZE   = 128*1024;    // data SRAM      @ 0x80000000 (non-executable)
parameter                PMEM_B_APH_REG = 1;           // program memory data port: 1=registered address phase (one or two wait states per data access), 0=none

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
parameter   C_EXTENSION         =  2;               // Compressed instructions extension: 0=none, 1=Zca, 2=Zca+Zcb, 3=Zca+Zcb+Zcmp, 4=Zca+Zcb+Zcmp+Zcmt
parameter   M_EXTENSION         =  2;               // Integer Multiply/Divide extension: 0=none, 1=Zmmul, 2=M-extension
parameter   MUL_TYPE            =  1;               // Multiplier type: 1=1xCycle,  2=4xCycles,  3=16xCycles (valid only with Zmmul and M-extension)
parameter   DIV_TYPE            =  3;               // Divider type:    1=12xCycle, 2=17xCycles, 3=33xCycles (valid only with M-extension)
parameter   CCSR_EN             =  0;               // Enable Custom-CSR interface
parameter   SINGLE_CYCLE_BRANCH =  0;               // Taken-branch latency: 1=zero-bubble (max IPC, lower Fmax), 0=one-bubble (lower IPC, max Fmax)
parameter   ASYNC_RST_EN        =  1'b0;            // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

parameter   DEBUG_EN            =  1;               // External debug (RISC-V Debug 1.0): 0=absent, 1=present (DTM wrapper below)
parameter   DM_TRIGGER_NR       =  0;               // Sdtrig hardware triggers (0-8, only meaningful when DEBUG_EN=1)
parameter   DTM_TYPE            =  0;               // DTM transport picked at build time: 0=JTAG, 1=UART, 2=I2C, 3=cJTAG
parameter   IDCODE_BASE         =  28'h800_11F7;    // JTAG IDCODE[27:0]: mfg field [11:1] = Arvern Silicon's JEDEC
                                                    // The version field [31:28] is NOT here -- it is a port
                                                    // (idcode_version_i) so silicon can bump it by ECO. On FPGA
                                                    // there is no metal ECO, so it is simply strapped to 0 below.
                                                    // identity (fixed); part-number [27:12] = 0x8001 = this
                                                    // board (Tang Console 138K) in Arvern's own FPGA reference-board
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
    .hclk_i                    ( clk_sys                 ),
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
//   0x82000000. Both are decoded by the bus system. Built-in DPI driver
//   (DIF_TYPE=4) on the Sipeed 4.3-inch 480 x 272 RGB panel: panel timing,
//   image placement and scaling are Belenos registers (DIF window 0x180).
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

// DBI pins (absent with DIF_TYPE=4), the DPI driver and the BDI (internal)
wire              [15:0] lcd_d_out_unused;
wire                     lcd_d_oe_unused;
wire                     lcd_cs_n_unused;
wire                     lcd_rd_n_unused;
wire                     lcd_wr_n_unused;
wire                     lcd_dc_unused;
wire                     lcd_reset_n_unused;
wire                     lcd_on;
wire                     spi_sck_unused;
wire                     spi_mosi_unused;
wire                     spi_cs_n_unused;
wire                     spi_dc_unused;
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
wire              [23:0] lcd_rgb;
wire                     dpi_de;
wire                     dpi_hs;
wire                     dpi_vs;
wire              [29:0] tmds;
wire                     ddc_scl_in;        // HDMI DDC: pad levels, pull-down enables
wire                     ddc_sda_in;
wire                     ddc_scl_pd;
wire                     ddc_sda_pd;
wire                     lcd_clk_d0;
wire                     lcd_clk_d1;
wire                     lcd_clk_oe_unused;

// Belenos configuration (classic persona, sized for this device)
parameter   BLNS_VRAM_WORDS       =  69632;          // VRAM depth in 32-bit words (1 - 2^24): two 480 x 272 frames at 8 bpp, plus 17 KB off screen
localparam  BLNS_VRAM_AW          =  $clog2(BLNS_VRAM_WORDS); // VRAM word-address width, ceil(log2(BLNS_VRAM_WORDS)): derived
parameter   BLNS_VRAM_RD_REG_EN   =  0;              // VRAM read data registered: 0=used as the macro delivers it (M10K meets the board clock), 1=one more read cycle
parameter   BLNS_DIM_W            =  10;             // Width/height field width in bits (4-16): sizes up to 2^DIM_W - 1 pixels
parameter   BLNS_FRAME_NR         =  2;              // Number of frame pointers (1, 2 or 4)
parameter   BLNS_LUT_EN           =  1;              // Programmable colour LUT: 0=absent, 1=present
parameter   BLNS_LUT_BANKS        =  2;              // LUT banks of 256 entries (1 or 2, needs LUT_EN)
parameter   BLNS_GPU_EN           =  1;              // GPU: 0=absent, 1=present
parameter   BLNS_GPU_XY_EN        =  1;              // GPU x/y coordinates, clip window and affine blits (SET_DST XY, SET_XY, SET_CLIP, EXEC_TEXRECT, ...)
parameter   BLNS_GPU_GRAD_EN      =  1;              // GPU gradient fills (SET_GRAD)
parameter   BLNS_GPU_BLEND_EN     =  0;              // GPU alpha blending at 16 bpp (SET_ALPHA, EXPAND masks): off, as on the DE0 build
parameter   BLNS_GPU_TRI_EN       =  0;              // GPU triangles (SET_V0..V2, EXEC_FILL TRI), needs GPU_XY_EN: off, as on the DE0 build
parameter   BLNS_GPU_RUNAHEAD_EN  =  0;              // GPU run-ahead: decode and set up the next primitive while one draws
parameter   BLNS_GPU_REMAP_EN     =  1;              // GPU palette remap (SET_AUX REMAP_TABLE, REMAP fields)
parameter   BLNS_GPU_MPLANE_EN    =  1;              // GPU mask plane on copies (SET_AUX MASK_PLANE, COPY MPLANE)
parameter   BLNS_GPU_IMM_EN       =  1;              // GPU immediate-data source, SET_SRC IMM
parameter   BLNS_GPU_FIFO_AW      =  3;              // GPU command FIFO: 2^GPU_FIFO_AW words (8, as the classic persona)
parameter   BLNS_GPU_LINE_EN      =  1;              // GPU lines: solid and dashed (EXEC_FILL LINE), needs GPU_XY_EN
parameter   BLNS_CURSOR_EN        =  1;              // Hardware cursor: 32x32, 2 bpp, shape in VRAM
localparam  BLNS_DIF_TYPE         =  (BOARD_HDMI != 0) ? 5 : 4;  // Built-in display driver: 4=DPI (the RGB panel), 5=HDMI (DPI and TMDS encoder): from BOARD_HDMI
parameter   BLNS_DPI_LB_AW        =  10;             // DPI line buffers of 1024 source pixels (9-12)
parameter   BLNS_DPI_SCALER       =  2;              // DPI scaler: 0=none, 1=nearest neighbour, 2=nearest and bilinear (three line buffers)
localparam  BLNS_DPI_PCLK_SEP     =  ((BOARD_HDMI != 0) || (BOARD_LCD_PCLK_SEP != 0)) ? 1 : 0;  // DPI timing: on clk_pix (HDMI, or the panel with BOARD_LCD_PCLK_SEP), else on clk_sys with CLK_DIV
localparam  BLNS_DPI_AUDIO_EN     =  ((BOARD_HDMI != 0) && (BOARD_HDMI_AUDIO != 0)) ? 1 : 0;  // HDMI audio from the I2S stream: from BOARD_HDMI_AUDIO
localparam  BLNS_DPI_NB           =  (BLNS_DPI_SCALER == 2) ? 3 : 2;  // line buffers: derived
parameter   BLNS_PRIV_CHECK_EN    =  0;              // User-mode accesses on both ports: 0=allowed, 1=refused

wire    [BLNS_VRAM_AW-1:0] vram_addr;
wire     [BLNS_DPI_NB-1:0] dpi_lb_we;
wire  [BLNS_DPI_LB_AW-1:0] dpi_lb_waddr;
wire                [23:0] dpi_lb_wdata;
wire  [BLNS_DPI_LB_AW-1:0] dpi_lb_raddr;
wire  [BLNS_DPI_NB*24-1:0] dpi_lb_rdata;

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
           .DPI_LB_AW           ( BLNS_DPI_LB_AW         ),
           .DPI_SCALER          ( BLNS_DPI_SCALER        ),
           .DPI_PCLK_SEP        ( BLNS_DPI_PCLK_SEP      ),
           .DPI_AUDIO_EN        ( BLNS_DPI_AUDIO_EN      ),
           .PRIV_CHECK_EN       ( BLNS_PRIV_CHECK_EN     ),
           .ASYNC_RST_EN        ( ASYNC_RST_EN           ))  belenos_inst (

// AHB CLOCK & RESET
    .hclk_i                ( clk_sys                                   ),
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

// 8080 PANEL (absent)
    .lcd_d_i               ( 16'h0000                                  ),
    .lcd_d_o               ( lcd_d_out_unused                          ),
    .lcd_d_oe_o            ( lcd_d_oe_unused                           ),
    .lcd_cs_n_o            ( lcd_cs_n_unused                           ),
    .lcd_rd_n_o            ( lcd_rd_n_unused                           ),
    .lcd_wr_n_o            ( lcd_wr_n_unused                           ),
    .lcd_dc_o              ( lcd_dc_unused                             ),

// SPI PANEL (absent)
    .spi_sck_o             ( spi_sck_unused                            ),
    .spi_mosi_o            ( spi_mosi_unused                           ),
    .spi_miso_i            ( 1'b0                                      ),
    .spi_cs_n_o            ( spi_cs_n_unused                           ),
    .spi_dc_o              ( spi_dc_unused                             ),

// SHARED PANEL CONTROL (absent)
    .lcd_reset_n_o         ( lcd_reset_n_unused                        ),
    .lcd_on_o              ( lcd_on                                    ),
    .lcd_te_i              ( 1'b0                                      ),

// DPI PANEL (Sipeed 4.3-inch 480 x 272, RGB888, DE mode)
    .dpi_de_o              ( dpi_de                                    ),
    .dpi_hs_o              ( dpi_hs                                    ),
    .dpi_vs_o              ( dpi_vs                                    ),
    .dpi_rgb_o             ( lcd_rgb                                   ),
    .dpi_clk_d0_o          ( lcd_clk_d0                                ),
    .dpi_clk_d1_o          ( lcd_clk_d1                                ),
    .dpi_pclk_i            ( clk_pix                                   ),  // HDMI pixel clock (DPI_PCLK_SEP = 1)
    .dpi_presetn_i         ( presetn_pix                               ),
    .panel_detect_i        ( (BOARD_HDMI != 0) ? HDMI_HPD : 1'b1       ),  // HDMI hot-plug; no detect pin on the panel connector
    .tmds_o                ( tmds                                      ),
    .ddc_scl_i             ( ddc_scl_in                                ),  // HDMI DDC (J13), open-drain pads below
    .ddc_sda_i             ( ddc_sda_in                                ),
    .ddc_scl_pd_o          ( ddc_scl_pd                                ),
    .ddc_sda_pd_o          ( ddc_sda_pd                                ),
    .i2s_bclk_i            ( i2s_hdmi_bclk                             ),  // HDMI audio: the apb_i2s stream (AUDIO_OUT = HDMI)
    .i2s_lrck_i            ( i2s_hdmi_lrck                             ),
    .i2s_sd_i              ( i2s_hdmi_sd                               ),
    .dpi_lb_we_o           ( dpi_lb_we                                 ),
    .dpi_lb_waddr_o        ( dpi_lb_waddr                              ),
    .dpi_lb_wdata_o        ( dpi_lb_wdata                              ),
    .dpi_lb_raddr_o        ( dpi_lb_raddr                              ),
    .dpi_lb_rdata_i        ( dpi_lb_rdata                              ),

// BELENOS DISPLAY INTERFACE (taken by the built-in driver)
    .bdi_px_valid_o        ( bdi_px_valid_unused                       ),
    .bdi_px_data_o         ( bdi_px_data_unused                        ),
    .bdi_px_last_o         ( bdi_px_last_unused                        ),
    .bdi_px_ready_i        ( 1'b0                                      ),
    .bdi_refresh_trig_o    ( bdi_refresh_trig_unused                   ),
    .bdi_refresh_active_i  ( 1'b0                                      ),
    .bdi_width_o           ( bdi_width_unused                          ),
    .bdi_height_o          ( bdi_height_unused                         ),
    .bdi_reg_sel_o         ( bdi_reg_sel_unused                        ),
    .bdi_reg_addr_o        ( bdi_reg_addr_unused                       ),
    .bdi_reg_wr_o          ( bdi_reg_wr_unused                         ),
    .bdi_reg_wdata_o       ( bdi_reg_wdata_unused                      ),
    .bdi_reg_rdata_i       ( 32'h0000_0000                             ),
    .bdi_reg_ready_i       ( 1'b1                                      ),
    .bdi_irq_i             ( 1'b0                                      ),
    .bdi_busy_i            ( 1'b0                                      )
);

// VRAM: BLNS_VRAM_WORDS x 32, byte write enables, 1-cycle read latency
ram_32x38400 #(.ADDRW(BLNS_VRAM_AW), .WORDS(BLNS_VRAM_WORDS)) vram_inst (
    .clk_i                 ( clk_sys                                   ),
    .cen_i                 ( vram_cen                                  ),
    .wen_i                 ( vram_wen                                  ),
    .addr_i                ( vram_addr                                 ),
    .din_i                 ( vram_din                                  ),
    .dout_o                ( vram_dout                                 )
);

// LUT: 512 x 24 (two banks of 256 RGB888 entries), 1-cycle read latency
ram_24x512 lut_inst (
    .clk_i                 ( clk_sys                                   ),
    .cen_i                 ( lut_cen                                   ),
    .wen_i                 ( lut_wen                                   ),
    .addr_i                ( lut_addr                                  ),
    .din_i                 ( lut_din                                   ),
    .dout_o                ( lut_dout                                  )
);



// DPI line buffers: 1024 x 24 each, one-cycle read latency
dpi_line_buffer #(.NB(BLNS_DPI_NB), .AW(BLNS_DPI_LB_AW)) dpi_lb_inst (
    .wclk_i                ( clk_sys                                   ),
    .rclk_i                ( clk_pix                                   ),
    .we_i                  ( dpi_lb_we                                 ),
    .waddr_i               ( dpi_lb_waddr                              ),
    .wdata_i               ( dpi_lb_wdata                              ),
    .raddr_i               ( dpi_lb_raddr                              ),
    .rdata_o               ( dpi_lb_rdata                              )
);

// Panel pins: the colour, the backlight on DIF_CFG.LCD_ON and the clock
// through a DDR output register (its two halves from the driver); all low,
// backlight off, in the HDMI build
localparam  LCD_EN       = (BOARD_HDMI == 0);
assign      LCD_DE       = LCD_EN & dpi_de;
assign      LCD_HSYNC    = LCD_EN & dpi_hs;
assign      LCD_VSYNC    = LCD_EN & dpi_vs;
assign      LCD_R        = {8{LCD_EN}} & lcd_rgb[23:16];
assign      LCD_G        = {8{LCD_EN}} & lcd_rgb[15: 8];
assign      LCD_B        = {8{LCD_EN}} & lcd_rgb[ 7: 0];
assign      LCD_BL       = LCD_EN & lcd_on;
assign      HDMI_PWR_SAV = (BOARD_HDMI != 0) & lcd_on;   // TMDS termination with the link
ODDR u_lcd_clk_oddr (
    .Q0                    ( LCD_CLK                                   ),
    .Q1                    ( lcd_clk_oe_unused                         ),
    .D0                    ( LCD_EN & lcd_clk_d0                       ),
    .D1                    ( LCD_EN & lcd_clk_d1                       ),
    .TX                    ( 1'b0                                      ),
    .CLK                   ( (BOARD_LCD_PCLK_SEP != 0) ? clk_pix : clk_sys )
);

// HDMI J13: the Belenos TMDS symbols through 10:1 serialisers (pixel clock
// clk_pix, bit clock clk_ser); static lanes in the panel build. DPI_CFG.LCD_ON
// switches the link (and the TMDS termination) on and off, so software can
// make the signal appear again for a sink that waits for one.
hdmi_phy #(.EN(BOARD_HDMI)) u_hdmi_phy (
    .pclk_i                ( clk_pix                                   ),
    .fclk_i                ( clk_ser                                   ),
    .rst_i                 ( ~presetn_pix                              ),
    .en_i                  ( lcd_on                                    ),  // DPI_CFG.LCD_ON: link on/off
    .tmds_i                ( tmds                                      ),
    .d_p_o                 ( HDMI_D_P                                  ),
    .d_n_o                 ( HDMI_D_N                                  ),
    .clk_p_o               ( HDMI_CLK_P                                ),
    .clk_n_o               ( HDMI_CLK_N                                )
);

io_buf io_buf_ddc_scl (     // HDMI J13 DDC SCL (open-drain)  -> M13
    .datain  ( 1'b0         ),
    .oe      ( ddc_scl_pd   ),
    .dataout ( ddc_scl_in   ),
    .dataio  ( HDMI_DDC_SCL ));

io_buf io_buf_ddc_sda (     // HDMI J13 DDC SDA (open-drain)  -> L13
    .datain  ( 1'b0         ),
    .oe      ( ddc_sda_pd   ),
    .dataout ( ddc_sda_in   ),
    .dataio  ( HDMI_DDC_SDA ));

//=============================================================================
// 7)  BUS SYSTEM
//=============================================================================

ahb_bus_system #(.ROM_SIZE      ( ROM_SIZE                      ),
                 .SRAM_NX_SIZE  ( SRAM_NX_SIZE                  ),
                 .VRAM_WORDS    ( BLNS_VRAM_WORDS               ),
                 .SU_MODE_EN    ( SU_MODE_EN                    ),
                 .PMEM_B_APH_REG( PMEM_B_APH_REG                ),
                 .HCLK_HZ       ( CLK_SYS_HZ                    ),
                 .AUDIO_OUT_RST ((BOARD_HDMI != 0) ? 2'd0 : 2'd2),
                 .ASYNC_RST_EN  ( ASYNC_RST_EN                  )) ahb_bus_system_inst (

// AHB CLOCK & RESET
    .hclk_i                    ( clk_sys                   ),
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

// BOARD I/O -- LEDs / Keys / Switches
    .periph0_led_o             ( periph0_led               ),
    .periph0_pa_en_o           ( PA_EN                     ),
    .periph0_key_i             ( {KEY[1], 1'b1}            ),  // KEY[0] is the board reset (reset_in_n)

    .periph0_sw_i              ( 4'h0                      ),  // no switches on this board
    .periph0_sd_det_i          ( SD_DET                    ),
    .periph0_tty_data_o        ( periph0_reg_01            ),
    .periph0_key_sw_val_o      ( periph0_reg_08            ),

// USB HOSTS -- USB-A J15 (usb0) and J16 (usb1)
    .usb0_dp_i                 ( usb0_dp_in                ),
    .usb0_dm_i                 ( usb0_dm_in                ),
    .usb0_dp_o                 ( usb0_dp_out               ),
    .usb0_dm_o                 ( usb0_dm_out               ),
    .usb0_oe_o                 ( usb0_oe                   ),
    .usb1_dp_i                 ( usb1_dp_in                ),
    .usb1_dm_i                 ( usb1_dm_in                ),
    .usb1_dp_o                 ( usb1_dp_out               ),
    .usb1_dm_o                 ( usb1_dm_out               ),
    .usb1_oe_o                 ( usb1_oe                   ),

// ACLINT CLOCK & RESET
    .clk_lf_i                  ( clk_lf                    ),
    .resetn_lf_i               ( resetn_lf                 ),
    .hclk_aon_i                ( clk_sys                   ),

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

// GPIO (PMOD0 and PMOD1)
    .gpio_pmod_i               ( gpio_pmod_in              ),
    .gpio_pmod_o               ( gpio_pmod_out             ),
    .gpio_pmod_oe_o            ( gpio_pmod_oe              ),

// I2S (onboard amplifier)
    .i2s_bclk_o                ( I2S_BCLK                  ),
    .i2s_lrck_o                ( I2S_LRCK                  ),
    .i2s_dout_o                ( I2S_DOUT                  ),
    .i2s_hdmi_bclk_o           ( i2s_hdmi_bclk             ),
    .i2s_hdmi_lrck_o           ( i2s_hdmi_lrck             ),
    .i2s_hdmi_sd_o             ( i2s_hdmi_sd               ),

// SPI (microSD slot)
    .sd_clk_o                  ( SD_CLK                    ),
    .sd_cs_o                   ( SD_DAT3                   ),
    .sd_cmd_o                  ( sd_cmd_out                ),
    .sd_cmd_oe_o               ( sd_cmd_oe                 ),
    .sd_cmd_i                  ( sd_cmd_in                 ),
    .sd_dat0_i                 ( SD_DAT0                   )
);


//=============================================================================
// 7b) DEBUG TRANSPORT MODULE (selectable JTAG/UART/I2C/cJTAG wrapper) - DEBUG_EN only
//=============================================================================

generate
if (DEBUG_ON) begin : g_dtm

    // Synthesizable transport-selectable DTM. DTM_TYPE picks JTAG/UART/I2C/cJTAG at
    // build time (generate, not a runtime mux). Drives the core DMI (APB)
    // master side. clk_i MUST be the ungated system clock (clk_sys).
    arv_dtm #(.DTM_TYPE           ( DTM_TYPE      ),
              .IDCODE_BASE        ( IDCODE_BASE   ),
              .UART_RX_FIFO_DEPTH ( 128           ),
              .ARST_EN            ( ASYNC_RST_EN  )) u_dtm (

        .idcode_version_i ( 4'h0              ),   // no metal ECO on FPGA: version strapped to 0

        .clk_i            ( clk_sys           ),
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
// Board orientation (Tang Console 138K schematic and interactive BOM, rev
// 32001C). The dock seen from its PMOD / SDRAM-header side, PMODs on the
// right; "(back)" parts are on the other side, under that spot:
//
//             top edge: SDRAM0 header (J9); HDMI, microSD (back)
//          +----------------------------------------------------------------+
//          | KEY0 = reset (S1) / USB-C J17 (back): USB device, power        |
//          | LED1: FPGA configuration status (RGB)               PMOD1 (J7) |
//    left  | MODE jumpers JP4-JP6 (back): configuration   MODE button (SW2) | right
//          | LED2: charge / power; BL616 BOOT (SW3, back)                   |
//          | KEY1 (S2) / USB-C J21 (back): USB-JTAG + UART       PMOD0 (J6) |
//          |   (the BL616 debugger: programs the board)                     |
//          | pin 1 <---- SDRAM1 header (J10) ----> pin 40 (debug: 1-10)     |
//          +----------------------------------------------------------------+
//             bottom edge: SDRAM1 header (J10); two USB-A (back)
//
//   * PMOD1 (J7) and PMOD0 (J6) are right-angle 2x6 sockets side by side on
//     the right edge, opening outward, the mode button (SW2) between
//     them: RECONFIG_N, a dedicated pin (it reloads the FPGA from flash). In both, pins 1-4 (+3.3V / GND) are at the end facing the bottom
//     edge, IO0/IO1 (pins 11/12) at the end facing the top edge.
//   * The SDRAM1 header (J10) runs along the bottom edge: pins 1/2 at the left
//     end (KEY1 / USB-C J21), pins 39/40 at the right end (under PMOD0). Its
//     odd pins are the row along the board edge, the even pins the inner row.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// SDRAM1 header (J10) pin map: the debug connector on pins 1-10, at the end
// of the header next to the left board edge, the same layout as the debug
// pins of the DE0-Nano-SoC GPIO_1 header (JP7) pins 40-31. The next pair
// differs (pins 11/12 here are +5V / GND, JP7 pins 30/29 are GND / +3.3V): a
// debug cable shared between the boards stops at the 2x5.
//
// Pin 1 is driven high: the 3.3V reference for a debug adapter's VTref (the
// header has no 3.3V pin; the board leaves out R30/R32, the +3.3V / GND
// options of pins 29/30); pin 2 is driven low, a ground reference beside it.
// They are I/O pins, a few mA at most: references, not a supply. Never take
// VTref from pin 11: the adapter would drive 5V into the FPGA's 3.3V pins.
// Pins 3-10: DTM PHY (active only when DEBUG_EN=1; else outputs Hi-Z). Only
// one transport is wired at a time (selected by DTM_TYPE at build); cJTAG uses
// the JTAG TCK/TMS pins as TCKC/TMSC. There is no TRST: the TAP resets through
// TMS and the debug reset. Pins 11/12 are the header's +5V / GND.
// Pins 13-40 are not used by this design.
//                                                                                                                //
//            top end (pins 39/40): PMOD0 (J6), Mode button (SW2), PMOD1 (J7)                                     //
//            SoM user LEDs (LED[1:0])                 \/                                                         //
//          even (inner row)                                            odd (outer row, board edge)               //
//         +------------------------------------------+   +------------------------------------------+            //
//         | 40  (not used)                           | . | 39  (not used)                           |            //
//         | 38  (not used)                           | . | 37  (not used)                           |  --.       //
//         | 36  (not used)                           | . | 35  (not used)                           |    |       //
//         | 34  (not used)                           | . | 33  (not used)                           |    |       //
//         | 32  (not used)                           | . | 31  (not used)                           |    USB-A   //
//         | 30  (not used)                           | . | 29  (not used)                           |    J15     //
//         | 28  (not used)                           | . | 27  (not used)                           |    (back)  //
//         | 26  (not used)                           | . | 25  (not used)                           |    |       //
//         | 24  (not used)                           | . | 23  (not used)                           |  --'       //
//         | 22  (not used)                           | . | 21  (not used)                           |            //
//         | 20  (not used)                           | . | 19  (not used)                           |            //
//         | 18  (not used)                           | . | 17  (not used)                           |  --.       //
//         | 16  (not used)                           | . | 15  (not used)                           |    |       //
//         | 14  (not used)                           | . | 13  (not used)                           |    USB-A   //
//         | 12  GND                                  | . | 11  +5V                                  |    J16     //
//         | 10  SDRAM1[10]  V17  UART_RX             | . |  9  SDRAM1[9]   W17  UART_TX             |    (back)  //
//         |  8  SDRAM1[8]   R18  I2C_SCL             | . |  7  SDRAM1[7]   T18  I2C_SDA             |    |       //
//         |  6  SDRAM1[6]   P16  JTAG_TDO            | . |  5  SDRAM1[5]   R17  JTAG_TDI            |  --'       //
//         |  4  SDRAM1[4]   T21  JTAG_TCK/CJTAG_TCKC | . |  3  SDRAM1[3]   U21  JTAG_TMS/CJTAG_TMSC |            //
//         |  2  SDRAM1[2]   P19  0 (GND ref)         | . |  1  SDRAM1[1]   R19  1 (3.3V ref)        |            //
//         +------------------------------------------+   +------------------------------------------+            //
//                                                     /\                                                         //
//                                                     ||                                                         //
//            bottom end (pins 1/2): left board edge, USB-C J21, KEY1 (S2), KEY0 (S1), LCD Panel                  //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//

wire jtag_tdo_pad_rdata_unused;   // TDO is push-pull output: pad read-back unused
wire uart_tx_pad_rdata_unused;    // UART_TX is push-pull output: pad read-back unused

// UART_TX pad source: normally the DTM's uart_tx; in UART_LOOPBACK bring-up mode
// it is the raw UART_RX pad value (combinational passthrough) so a host loopback
// exercises adapter TX -> wire -> UART_RX pad (pin 10) -> fabric -> UART_TX pad
// (pin 9) -> wire -> adapter RX, i.e. the entire physical link minus the DTM. The
// pad is driven whenever the DTM is present OR loopback is on (so a DEBUG_EN=0
// wiring-check build still drives TX).
wire uart_tx_pad_din = UART_LOOPBACK ? uart_rx : uart_tx;
wire uart_tx_pad_oe  = DEBUG_ON || (UART_LOOPBACK != 0);

// Pin 4 carries JTAG TCK or cJTAG TCKC, pin 3 JTAG TMS or cJTAG TMSC: one pad
// each, its input feeding both transports' nets (the DTM consumes only the
// selected one).
io_buf io_buf_tck_tckc (    // JTAG_TCK / CJTAG_TCKC (input)  -> T21 / J10 pin 4
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( tck_pad_in                ),
    .dataio  ( SDRAM1[4]                 ));

assign jtag_tck   = tck_pad_in;
assign cjtag_tckc = tck_pad_in;

// TMSC is push-pull and bidirectional: the target drives only in the TDO phase
// while TCKC is low. The pad keeper (bus hold, cJTAG builds) sustains the level
// while nobody drives, which the OScan1 sampling contract depends on. The JTAG,
// UART and I2C transports never enable this driver.
wire tmsc_pad_do = cjtag_tmsc_o;
wire tmsc_pad_oe = DEBUG_ON && cjtag_tmsc_oe;

io_buf io_buf_tms_tmsc (    // JTAG_TMS (input) / CJTAG_TMSC (inout, bus-hold)  -> U21 / J10 pin 3
    .datain  ( tmsc_pad_do               ),
    .oe      ( tmsc_pad_oe               ),
    .dataout ( tms_pad_in                ),
    .dataio  ( SDRAM1[3]                 ));

assign jtag_tms     = tms_pad_in;
assign cjtag_tmsc_i = tms_pad_in;

io_buf io_buf_jtag_tdi (    // JTAG_TDI (input)  -> R17 / J10 pin 5
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( jtag_tdi                  ),
    .dataio  ( SDRAM1[5]                 ));

io_buf io_buf_jtag_tdo (    // JTAG_TDO (output, tri-stated by tdo_oe)  -> P16 / J10 pin 6
    .datain  ( jtag_tdo                  ),
    .oe      ( DEBUG_ON && jtag_tdo_oe   ),
    .dataout ( jtag_tdo_pad_rdata_unused ),
    .dataio  ( SDRAM1[6]                 ));

io_buf io_buf_i2c_scl (     // I2C_SCL (inout, open-drain)  -> R18 / J10 pin 8
    .datain  ( 1'b0                      ),
    .oe      ( DEBUG_ON && i2c_scl_pd    ),
    .dataout ( i2c_scl                   ),
    .dataio  ( SDRAM1[8]                 ));

io_buf io_buf_i2c_sda (     // I2C_SDA (inout, open-drain)  -> T18 / J10 pin 7
    .datain  ( 1'b0                      ),
    .oe      ( DEBUG_ON && i2c_sda_pd    ),
    .dataout ( i2c_sda                   ),
    .dataio  ( SDRAM1[7]                 ));

io_buf io_buf_uart_tx (     // UART_TX (output)  -> W17 / J10 pin 9
    .datain  ( uart_tx_pad_din           ),
    .oe      ( uart_tx_pad_oe            ),
    .dataout ( uart_tx_pad_rdata_unused  ),
    .dataio  ( SDRAM1[9]                 ));

io_buf io_buf_uart_rx (     // UART_RX (input)  -> V17 / J10 pin 10
    .datain  ( 1'b0                      ),
    .oe      ( 1'b0                      ),
    .dataout ( uart_rx                   ),
    .dataio  ( SDRAM1[10]                ));

//-----------------------------------------------------------------------------
// PMOD0 (J6) and PMOD1 (J7) pin map: the 16 pins of the GPIO
// apb_gpio_pmod_inst (APB completer at 0x10041000). PMODn_IOk is GPIO pin
// 8n + k, its direction set by the GPIO output enable; the pads are pulled up.
// Pin interrupts on PLIC sources 1 + GPIO pin (1-16).
//
// Placement (board orientation above): two right-angle 2x6 sockets side by
// side on the right board edge, opening outward, the MODE button (SW2)
// between them; PMOD1 (J7) towards the top edge, PMOD0 (J6) towards the
// bottom edge. Both have the same face, seen from outside the right edge
// with the board lying flat (top-edge end on the right):
//
//                bottom-edge end                         top-edge end
//    upper row:   1 +3.3V   3 GND   5 IO6   7 IO4   9 IO2  11 IO0
//    lower row:   2 +3.3V   4 GND   6 IO7   8 IO5  10 IO3  12 IO1
//
// Each row is the usual 12-pin PMOD row (four IOs, GND, +3.3V); Sipeed
// numbers the pins from the supply end.
//
//    socket pin     11    12     9    10     7     8     5     6   3,4   1,2
//    signal        IO0   IO1   IO2   IO3   IO4   IO5   IO6   IO7   GND  +3.3V
//    PMOD0 (J6)    V18   V19   G21   G22   F18   E18   C22   B22
//      GPIO          0     1     2     3     4     5     6     7
//    PMOD1 (J7)    W19   W20   F19   F20   E22   D22   E21   D21
//      GPIO          8     9    10    11    12    13    14    15
//-----------------------------------------------------------------------------
wire [7:0] pmod0_in;
wire [7:0] pmod1_in;

genvar gp;
generate
  for (gp = 0; gp < 8; gp = gp + 1) begin : g_pmod
    io_buf io_buf_pmod0 (
        .datain  ( gpio_pmod_out[gp]     ),
        .oe      ( gpio_pmod_oe[gp]      ),
        .dataout ( pmod0_in[gp]     ),
        .dataio  ( PMOD0[gp]        ));
    io_buf io_buf_pmod1 (
        .datain  ( gpio_pmod_out[gp + 8] ),
        .oe      ( gpio_pmod_oe[gp + 8]  ),
        .dataout ( pmod1_in[gp]     ),
        .dataio  ( PMOD1[gp]        ));
  end
endgenerate

assign gpio_pmod_in = {pmod1_in, pmod0_in};

// microSD command line: driven while the SPI controller enables IO0
io_buf io_buf_sd_cmd (      // SD_CMD (inout)  -> Y16
    .datain  ( sd_cmd_out                ),
    .oe      ( sd_cmd_oe                 ),
    .dataout ( sd_cmd_in                 ),
    .dataio  ( SD_CMD                    ));


//=============================================================================
// 8)  UNUSED I/O
//=============================================================================

assign SDRAM1[1]   = 1'b1;         // J10 pin 1: 3.3V reference for an adapter's VTref
assign SDRAM1[2]   = 1'b0;         // J10 pin 2: ground reference
assign UART_TXD    = 1'b1;           // UART to the BL616: idle


//=============================================================================
// 9)  LINT CLEANUP
//=============================================================================

// Unused signals: consumed by a "*_unused"-named wire (Verilator does not flag
// UNUSEDSIGNAL on any signal whose name contains "unused").
wire        dut_hclk_en_unused    = dut_hclk_en;      // FPGA: clock enable unused (always-on)
wire        system_hclk_en_unused = system_hclk_en;   // FPGA: clock enable unused (always-on)
wire        UART_RXD_unused       = UART_RXD;         // UART from the BL616: not used
wire [31:0] periph0_reg_01_unused = periph0_reg_01;   // TTY data register (probe only)
wire [31:0] periph0_reg_08_unused = periph0_reg_08;   // Key/Switch value register (probe only)
wire [19:0] blnsr_haddr_hi_unused = blnsr_haddr[31:12];                  // Belenos register port: 4KB
wire [29-BLNS_VRAM_AW:0] blnsv_haddr_hi_unused = blnsv_haddr[31:BLNS_VRAM_AW+2];  // VRAM window: BLNS_VRAM_WORDS words
wire  [7:2] periph0_led_unused   = periph0_led[7:2];                 // LED register bits without a LED


endmodule // arvern_fpga

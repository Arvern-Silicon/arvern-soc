//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ahb_bus_system
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ahb_bus_system.v
// Module Description : AHB system for arvern containing:
//
//                             - AHB interconnect
//                             - Program memory (executable)
//
//                             - Data SRAM (non-executable)
//                             - ACLINT
//                             - Belenos register and VRAM-window ports (the
//                               controller is in the top level)
//                             - PLIC
//                             - AHB-to-APB bridge (64KB at 0x10040000) with
//                               the board I/O, GPIO (GPIO_1 header) and GPIO
//                               (Arduino header) as APB4 completers
//----------------------------------------------------------------------------

module  ahb_bus_system #(

// PARAMETERs
//======================================
parameter                ROM_SIZE     = 8*1024,   // Size of the program memory instance (in Bytes)
parameter                SRAM_NX_SIZE = 8*1024,   // Size of the Non-executable SRAM memory instance (in Bytes)
parameter                VRAM_WORDS   = 38400,    // Belenos VRAM depth (32-bit words)
parameter [0:0]          FIXED_B_PRIO = 1'b0,     // Arbitration scheme inside the fused ROM/SRAM controllers:
                                                  //   1'b0 = round-robin (toggle priority, default)
                                                  //   1'b1 = fixed Port-B priority (data bus wins)
parameter                SU_MODE_EN   = 1,        // Core S-mode + U-mode present: S-mode interrupt sources in the ACLINT and PLIC
parameter                ASYNC_RST_EN = 1'b1      // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

) (

// AHB CLOCK & RESET
//======================================
input  wire              hclk_i,
input  wire              hresetn_i,
output wire              hclk_en_o,

// EXECUTABLE AHB BUS MANAGER INTERFACE
//========================================
input  wire       [31:0] m_x_haddr_i,
input  wire        [2:0] m_x_hburst_i,
input  wire              m_x_hmastlock_i,
input  wire        [3:0] m_x_hprot_i,
input  wire        [2:0] m_x_hsize_i,
input  wire              m_x_hsmode_i,
input  wire        [1:0] m_x_htrans_i,
input  wire       [31:0] m_x_hwdata_i,
input  wire              m_x_hwrite_i,

output wire       [31:0] m_x_hrdata_o,
output wire              m_x_hready_o,
output wire              m_x_hresp_o,

// NON-EXECUTABLE AHB MANAGER INTERFACES
//========================================
input  wire       [31:0] m_nx_haddr_i,
input  wire        [2:0] m_nx_hburst_i,
input  wire              m_nx_hmastlock_i,
input  wire        [3:0] m_nx_hprot_i,
input  wire        [2:0] m_nx_hsize_i,
input  wire              m_nx_hsmode_i,
input  wire              m_nx_hmaster_i,
input  wire        [1:0] m_nx_htrans_i,
input  wire       [31:0] m_nx_hwdata_i,
input  wire              m_nx_hwrite_i,

output wire       [31:0] m_nx_hrdata_o,
output wire              m_nx_hready_o,
output wire              m_nx_hresp_o,

// AHB PERIPHERAL #0 -- LED / KEY / SW
//========================================
output wire        [7:0] periph0_led_o,
input  wire        [1:0] periph0_key_i,
input  wire        [3:0] periph0_sw_i,
output wire       [31:0] periph0_tty_data_o,
output wire       [31:0] periph0_key_sw_val_o,

// ACLINT CLOCK & RESET
//========================================
input  wire              clk_lf_i,                // Always-on low-frequency clock for the MTIME counter (async to hclk_i)
input  wire              resetn_lf_i,             // Active-low LF-domain reset (sync-deasserted on clk_lf_i)
input  wire              hclk_aon_i,              // Always-on AHB-frequency clock (same source as hclk_i, never gated)

// ACLINT INTERRUPTS & ZICNTR TIME INTERFACE
//========================================
output wire              aclint_irq_m_software_o, // MSIP -> core irq_m_software_i
output wire              aclint_irq_s_software_o, // SSIP -> core irq_s_software_i
output wire              aclint_irq_m_timer_o,    // MTIP -> core irq_m_timer_i

input  wire              aclint_time_req_i,       // core time_req_o
output wire              aclint_time_gnt_o,       // -> core time_gnt_i
output wire       [63:0] aclint_time_val_o,       // -> core time_val_i

// BELENOS GRAPHIC CONTROLLER (in the top level): register port
//========================================
output wire              blnsr_hsel_o,            // select
output wire       [31:0] blnsr_haddr_o,           // address
output wire        [1:0] blnsr_htrans_o,          // transfer type
output wire              blnsr_hwrite_o,          // write
output wire        [2:0] blnsr_hsize_o,           // size
output wire        [3:0] blnsr_hprot_o,           // protection
output wire              blnsr_hsmode_o,          // S-mode access
output wire              blnsr_hready_o,          // bus ready
output wire       [31:0] blnsr_hwdata_o,          // write data
input  wire       [31:0] blnsr_hrdata_i,          // read data
input  wire              blnsr_hreadyout_i,       // slave ready
input  wire              blnsr_hresp_i,           // slave error

// BELENOS GRAPHIC CONTROLLER (in the top level): VRAM-window port
//========================================
output wire              blnsv_hsel_o,            // select
output wire       [31:0] blnsv_haddr_o,           // address
output wire        [1:0] blnsv_htrans_o,          // transfer type
output wire              blnsv_hwrite_o,          // write
output wire        [2:0] blnsv_hsize_o,           // size
output wire        [3:0] blnsv_hprot_o,           // protection
output wire              blnsv_hsmode_o,          // S-mode access
output wire              blnsv_hready_o,          // bus ready
output wire       [31:0] blnsv_hwdata_o,          // write data
input  wire       [31:0] blnsv_hrdata_i,          // read data
input  wire              blnsv_hreadyout_i,       // slave ready
input  wire              blnsv_hresp_i,           // slave error

input  wire              blns_hclk_en_i,          // Belenos clock-gate enable (into hclk_en_o)

// PLIC
//========================================
output wire              plic_irq_m_external_o,   // -> core irq_m_external_i
output wire              plic_irq_s_external_o,   // -> core irq_s_external_i

// GPIO
//========================================
input  wire       [25:0] gpio_i,                  // pin values (asynchronous)
output wire       [25:0] gpio_o,                  // pin output values
output wire       [25:0] gpio_oe_o,               // pin output enables, 1 = drive

// GPIO (Arduino header)
//========================================
input  wire       [15:0] gpio_ard_i,              // pin values (asynchronous)
output wire       [15:0] gpio_ard_o,              // pin output values
output wire       [15:0] gpio_ard_oe_o            // pin output enables, 1 = drive

);


//=============================================================================
// 1)  WIRE & REGISTER DEFINITION
//=============================================================================

// Local parameters
localparam               ROM_ADDRW      = $clog2(ROM_SIZE)-2;     // Address width of the program memory instance (32b words)

localparam               SRAM_NX_ADDRW  = $clog2(SRAM_NX_SIZE)-2; // Address width of the Non-executable SRAM memory instance (32b words)
localparam               SRAM_NX_HADDRW = $clog2(SRAM_NX_SIZE);   // Address width of the Non-executable SRAM AHB interface (8b words)

localparam               HAUSER_W       = 1;                      // Width of the HAUSER bus (min value is 1)

localparam               VRAM_SIZE      = VRAM_WORDS*4;           // Belenos VRAM window size (in Bytes)

// Arbiter Interface
wire                     m_nx_grant;
wire                     m_nx_request;

// Address Decoder Interface
wire               [6:0] s_x_decoder_1hot;
wire              [31:0] s_x_decoder_addr;
wire               [6:0] s_decoder_1hot;
wire              [31:0] s_decoder_addr;

// AHB Subordinate Interfaces
//   The program memory has no AHB subordinate interface: its controller is
//   fused inside the interconnect, which drives the macro through sram_*.
wire              [31:0] s_sram_nx_hrdata;
wire                     s_sram_nx_hreadyout;
wire                     s_sram_nx_hresp;
wire                     s_sram_nx_hsel;
wire              [31:0] s_sram_nx_haddr;
wire      [HAUSER_W-1:0] s_sram_nx_hauser;
wire               [2:0] s_sram_nx_hburst;
wire               [3:0] s_sram_nx_hmaster;
wire                     s_sram_nx_hmastlock;
wire               [3:0] s_sram_nx_hprot;
wire                     s_sram_nx_hready;
wire               [2:0] s_sram_nx_hsize;
wire               [1:0] s_sram_nx_htrans;
wire              [31:0] s_sram_nx_hwdata;
wire                     s_sram_nx_hwrite;

wire              [31:0] s_apb_hrdata;
wire                     s_apb_hreadyout;
wire                     s_apb_hresp;
wire                     s_apb_hsel;
wire              [31:0] s_apb_haddr;
wire      [HAUSER_W-1:0] s_apb_hauser;
wire               [2:0] s_apb_hburst;
wire               [3:0] s_apb_hmaster;
wire                     s_apb_hmastlock;
wire               [3:0] s_apb_hprot;
wire                     s_apb_hready;
wire               [2:0] s_apb_hsize;
wire               [1:0] s_apb_htrans;
wire              [31:0] s_apb_hwdata;
wire                     s_apb_hwrite;

wire              [31:0] s_aclint_hrdata;
wire                     s_aclint_hreadyout;
wire                     s_aclint_hresp;
wire                     s_aclint_hsel;
wire              [31:0] s_aclint_haddr;
wire      [HAUSER_W-1:0] s_aclint_hauser;
wire               [2:0] s_aclint_hburst;
wire               [3:0] s_aclint_hmaster;
wire                     s_aclint_hmastlock;
wire               [3:0] s_aclint_hprot;
wire                     s_aclint_hready;
wire               [2:0] s_aclint_hsize;
wire               [1:0] s_aclint_htrans;
wire              [31:0] s_aclint_hwdata;
wire                     s_aclint_hwrite;

wire              [31:0] s_blnsr_hrdata;
wire                     s_blnsr_hreadyout;
wire                     s_blnsr_hresp;
wire                     s_blnsr_hsel;
wire              [31:0] s_blnsr_haddr;
wire      [HAUSER_W-1:0] s_blnsr_hauser;
wire               [2:0] s_blnsr_hburst;
wire               [3:0] s_blnsr_hmaster;
wire                     s_blnsr_hmastlock;
wire               [3:0] s_blnsr_hprot;
wire                     s_blnsr_hready;
wire               [2:0] s_blnsr_hsize;
wire               [1:0] s_blnsr_htrans;
wire              [31:0] s_blnsr_hwdata;
wire                     s_blnsr_hwrite;

wire              [31:0] s_blnsv_hrdata;
wire                     s_blnsv_hreadyout;
wire                     s_blnsv_hresp;
wire                     s_blnsv_hsel;
wire              [31:0] s_blnsv_haddr;
wire      [HAUSER_W-1:0] s_blnsv_hauser;
wire               [2:0] s_blnsv_hburst;
wire               [3:0] s_blnsv_hmaster;
wire                     s_blnsv_hmastlock;
wire               [3:0] s_blnsv_hprot;
wire                     s_blnsv_hready;
wire               [2:0] s_blnsv_hsize;
wire               [1:0] s_blnsv_htrans;
wire              [31:0] s_blnsv_hwdata;
wire                     s_blnsv_hwrite;

wire              [31:0] s_plic_hrdata;
wire                     s_plic_hreadyout;
wire                     s_plic_hresp;
wire                     s_plic_hsel;
wire              [31:0] s_plic_haddr;
wire      [HAUSER_W-1:0] s_plic_hauser;
wire               [2:0] s_plic_hburst;
wire               [3:0] s_plic_hmaster;
wire                     s_plic_hmastlock;
wire               [3:0] s_plic_hprot;
wire                     s_plic_hready;
wire               [2:0] s_plic_hsize;
wire               [1:0] s_plic_htrans;
wire              [31:0] s_plic_hwdata;
wire                     s_plic_hwrite;



// Program-memory Interface (executable slot 0 @ 0x20000000 - writable
// init-at-config SRAM so a debugger can load firmware here over SBA).
//   The fused fabric drives a 30-bit word address; pmem_addr_full feeds the physical
//   macro after slicing down to ROM_ADDRW (= the size of the 0x20000000 region).
wire              [31:0] pmem_dout;
wire              [29:0] pmem_addr_full;
wire     [ROM_ADDRW-1:0] pmem_addr;
wire                     pmem_cen;
wire                     pmem_clk;
wire              [31:0] pmem_din;
wire               [3:0] pmem_wen;

// Absent ROM controller (NR_S_X_ROM=0): the interconnect drives these to 0; named
// here (not left empty) so lint stays clean, then reduced into a sink.
wire              [29:0] rom_addr_o_unused;
wire                     rom_cen_o_unused;
wire                     rom_clk_o_unused;
wire                     rom_ports_unused_ok = |{1'b0, rom_addr_o_unused, rom_cen_o_unused, rom_clk_o_unused, 1'b0};

// Non-executable SRAM Interface
wire              [31:0] sram_nx_dout;
wire [SRAM_NX_ADDRW-1:0] sram_nx_addr;
wire                     sram_nx_cen;
wire                     sram_nx_clk;
wire              [31:0] sram_nx_din;
wire               [3:0] sram_nx_wen;

// Architectural clock-gating
//   The fused interconnect's hclk_en covers the program-memory activity.
wire                     interconnect_hclk_en;
wire                     s_sram_nx_hclk_en;
wire                     s_apb_hclk_en;
wire                     apb_lks_pclk_en;
wire                     apb_gpio_pclk_en;
wire                     apb_gpio_ard_pclk_en;
wire                     s_aclint_hclk_en;
wire                     s_blns_hclk_en;
wire                     s_plic_hclk_en;

wire                     aclint_mtimer_wake_lf;

// PLIC interrupt sources: [1..26] GPIO pins 0..25, [27] KEY, [28] SW,
// [32..47] Arduino GPIO pins 0..15
wire                     periph0_irq_key;
wire                     periph0_irq_sw;
wire              [25:0] gpio_irq;
wire                     gpio_irq_any;
wire              [15:0] gpio_ard_irq;
wire                     gpio_ard_irq_any;
wire              [47:0] plic_irq_src;

// GPIO alternate-function ownership outputs (ALT_NR=0: always 0)
wire           [4*26-1:0] gpio_alt_act_unused;
wire           [4*16-1:0] gpio_ard_alt_act_unused;

//=============================================================================
// 2)  FUSED AHB INTERCONNECT
//=============================================================================
//
// The fused interconnect absorbs the program-memory controller; its macro is
// wired directly to it through the sram_* ports. Everything else, including
// the data SRAM, sits in the non-executable space.
//
ahb_interconnect_fused #(.NR_M             (1),                 // Number of non-executable AHB Managers
                         .NR_S_X_ROM       (0),                 // No ROM controllers (ROM-less executable space)
                         .NR_S_X_SRAM      (1),                 // One fused SRAM controller: pmem @0x20000000
                         .NR_S_NX          (6),                 // Number of AHB Subordinates in non-executable space (SRAM-NX + APB bridge + ACLINT + Belenos regs/VRAM + PLIC)
                         .M_NX_HMASTER_TAG (4'h8),              // Data port tag (data_hmaster_o) on HMASTER[3]
                         .HAUSER_W         (HAUSER_W),          // Width of the HAUSER bus (min value is 1)
                         .FIXED_B_PRIO     (FIXED_B_PRIO),      // Arbitration scheme: 0=round-robin, 1=fixed Port-B priority
                         .ASYNC_RST_EN     (ASYNC_RST_EN)       // Reset architecture: 1=async active-low (default), 0=synchronous
                        )               ahb_interconnect_inst (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),

    .hclk_en_o             ( interconnect_hclk_en                      ),

// EXECUTABLE AHB BUS MANAGER INTERFACE
    .m_x_haddr_i           ( m_x_haddr_i                               ),
    .m_x_hauser_i          ( m_x_hsmode_i                              ),
    .m_x_hburst_i          ( m_x_hburst_i                              ),
    .m_x_hmastlock_i       ( m_x_hmastlock_i                           ),
    .m_x_hprot_i           ( m_x_hprot_i                               ),
    .m_x_hsize_i           ( m_x_hsize_i                               ),
    .m_x_htrans_i          ( m_x_htrans_i                              ),
    .m_x_hwdata_i          ( m_x_hwdata_i                              ),
    .m_x_hwrite_i          ( m_x_hwrite_i                              ),
    .m_x_hrdata_o          ( m_x_hrdata_o                              ),
    .m_x_hready_o          ( m_x_hready_o                              ),
    .m_x_hresp_o           ( m_x_hresp_o                               ),

// NON-EXECUTABLE AHB MANAGER INTERFACES
    .m_nx_haddr_i          ( m_nx_haddr_i                              ),
    .m_nx_hauser_i         ( m_nx_hsmode_i                             ),
    .m_nx_hburst_i         ( m_nx_hburst_i                             ),
    .m_nx_hmaster_i        ({m_nx_hmaster_i, 3'b000}                   ),
    .m_nx_hmastlock_i      ( m_nx_hmastlock_i                          ),
    .m_nx_hprot_i          ( m_nx_hprot_i                              ),
    .m_nx_hsize_i          ( m_nx_hsize_i                              ),
    .m_nx_htrans_i         ( m_nx_htrans_i                             ),
    .m_nx_hwdata_i         ( m_nx_hwdata_i                             ),
    .m_nx_hwrite_i         ( m_nx_hwrite_i                             ),
    .m_nx_hrdata_o         ( m_nx_hrdata_o                             ),
    .m_nx_hready_o         ( m_nx_hready_o                             ),
    .m_nx_hresp_o          ( m_nx_hresp_o                              ),

// ARBITER INTERFACE for NON-EXECUTABLE MANAGERS
    .m_nx_grant_i          ( m_nx_grant                                ),
    .m_nx_request_o        ( m_nx_request                              ),

// ADDRESS DECODER INTERFACES (FOR ALL SUBORDINATES)
    .s_decoder_1hot_i      ( s_decoder_1hot                            ),
    .s_decoder_addr_o      ( s_decoder_addr                            ),

// ADDRESS DECODER INTERFACES (FOR EXECUTABLE SUBORDINATES ONLY)
    .s_x_decoder_1hot_i    ( s_x_decoder_1hot[0]                       ),
    .s_x_decoder_addr_o    ( s_x_decoder_addr                          ),

// FUSED ROM CONTROLLER MEMORY INTERFACE  (NR_S_X_ROM=0 -> none; rom_dout_i is the
//   unused padded input, the rom_*_o outputs are driven to 0 internally and sunk here)
    .rom_dout_i            ( 32'b0                                     ),
    .rom_addr_o            ( rom_addr_o_unused                         ),
    .rom_cen_o             ( rom_cen_o_unused                          ),
    .rom_clk_o             ( rom_clk_o_unused                          ),

// FUSED SRAM CONTROLLER MEMORY INTERFACE (one slot: pmem @0x20000000, decoder bit [0])
    .sram_dout_i           ( pmem_dout                                 ),
    .sram_addr_o           ( pmem_addr_full                            ),
    .sram_cen_o            ( pmem_cen                                  ),
    .sram_clk_o            ( pmem_clk                                  ),
    .sram_din_o            ( pmem_din                                  ),
    .sram_wen_o            ( pmem_wen                                  ),

// NON-EXECUTABLE AHB SUBORDINATE INTERFACES
    .s_nx_hrdata_i         ({s_plic_hrdata,     s_blnsv_hrdata,    s_blnsr_hrdata,    s_aclint_hrdata,    s_apb_hrdata,    s_sram_nx_hrdata    }),
    .s_nx_hreadyout_i      ({s_plic_hreadyout,  s_blnsv_hreadyout, s_blnsr_hreadyout, s_aclint_hreadyout, s_apb_hreadyout, s_sram_nx_hreadyout }),
    .s_nx_hresp_i          ({s_plic_hresp,      s_blnsv_hresp,     s_blnsr_hresp,     s_aclint_hresp,     s_apb_hresp,     s_sram_nx_hresp     }),
    .s_nx_haddr_o          ({s_plic_haddr,      s_blnsv_haddr,     s_blnsr_haddr,     s_aclint_haddr,     s_apb_haddr,     s_sram_nx_haddr     }),
    .s_nx_hauser_o         ({s_plic_hauser,     s_blnsv_hauser,    s_blnsr_hauser,    s_aclint_hauser,    s_apb_hauser,    s_sram_nx_hauser    }),
    .s_nx_hburst_o         ({s_plic_hburst,     s_blnsv_hburst,    s_blnsr_hburst,    s_aclint_hburst,    s_apb_hburst,    s_sram_nx_hburst    }),
    .s_nx_hmaster_o        ({s_plic_hmaster,    s_blnsv_hmaster,   s_blnsr_hmaster,   s_aclint_hmaster,   s_apb_hmaster,   s_sram_nx_hmaster   }),
    .s_nx_hmastlock_o      ({s_plic_hmastlock,  s_blnsv_hmastlock, s_blnsr_hmastlock, s_aclint_hmastlock, s_apb_hmastlock, s_sram_nx_hmastlock }),
    .s_nx_hprot_o          ({s_plic_hprot,      s_blnsv_hprot,     s_blnsr_hprot,     s_aclint_hprot,     s_apb_hprot,     s_sram_nx_hprot     }),
    .s_nx_hready_o         ({s_plic_hready,     s_blnsv_hready,    s_blnsr_hready,    s_aclint_hready,    s_apb_hready,    s_sram_nx_hready    }),
    .s_nx_hsel_o           ({s_plic_hsel,       s_blnsv_hsel,      s_blnsr_hsel,      s_aclint_hsel,      s_apb_hsel,      s_sram_nx_hsel      }),
    .s_nx_hsize_o          ({s_plic_hsize,      s_blnsv_hsize,     s_blnsr_hsize,     s_aclint_hsize,     s_apb_hsize,     s_sram_nx_hsize     }),
    .s_nx_htrans_o         ({s_plic_htrans,     s_blnsv_htrans,    s_blnsr_htrans,    s_aclint_htrans,    s_apb_htrans,    s_sram_nx_htrans    }),
    .s_nx_hwdata_o         ({s_plic_hwdata,     s_blnsv_hwdata,    s_blnsr_hwdata,    s_aclint_hwdata,    s_apb_hwdata,    s_sram_nx_hwdata    }),
    .s_nx_hwrite_o         ({s_plic_hwrite,     s_blnsv_hwrite,    s_blnsr_hwrite,    s_aclint_hwrite,    s_apb_hwrite,    s_sram_nx_hwrite    })
);

// Slice the fused fabric's 30-bit word address down to the macro's
// physical address width.
assign pmem_addr   = pmem_addr_full  [ROM_ADDRW-1:0];


//=============================================================================
// 3)  ADDRESS DECODER
//=============================================================================

// No arbitration, only one NX master
assign m_nx_grant = m_nx_request;

ahb_decoder #(.ROM_SIZE(ROM_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .VRAM_SIZE(VRAM_SIZE)) ahb_decoder_x_inst (
    .decoder_addr_i        ( s_x_decoder_addr                          ),
    .decoder_1hot_o        ( s_x_decoder_1hot                          )
);

ahb_decoder #(.ROM_SIZE(ROM_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE), .VRAM_SIZE(VRAM_SIZE)) ahb_decoder_inst   (

    .decoder_addr_i        ( s_decoder_addr                            ),
    .decoder_1hot_o        ( s_decoder_1hot                            )
);


//=============================================================================
// 4)  PROGRAM MEMORY (executable slot 0 @ 0x20000000)
//=============================================================================
//   Writable init-at-config SRAM (pmem_32kb) so a debugger can
//   load firmware here over SBA while the board still self-boots the preloaded
//   image. The AHB-side controller is fused inside ahb_interconnect_fused; only
//   the physical macro lives here.

pmem_48kb #(.MEM_ADDRW(ROM_ADDRW), .ASYNC_RST_EN(ASYNC_RST_EN)) pmem_inst0 (

// OUTPUTs
    .pmem_dout_o           ( pmem_dout                                 ),

// INPUTs
    .pmem_addr_i           ( pmem_addr                                 ),
    .pmem_cen_i            ( pmem_cen                                  ),
    .pmem_clk_i            ( pmem_clk                                  ),
    .pmem_rst_i            ( hresetn_i                                 ),
    .pmem_din_i            ( pmem_din                                  ),
    .pmem_wen_i            ( pmem_wen                                  )
);


//=============================================================================
// 5) DATA SRAM (non-executable @ 0x80000000)
//=============================================================================

// The controller takes a power-of-two size: the address width of SRAM_NX_SIZE rounded up
// (the decoder bounds the region at SRAM_NX_SIZE, the addresses above it never come)
ahb_sram_controller #(.MEM_SIZE(1 << $clog2(SRAM_NX_SIZE)), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_sram_nx_ctrl_inst (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_sram_nx_hclk_en                         ),

// AHB INTERFACE
    .haddr_i               ( s_sram_nx_haddr[SRAM_NX_HADDRW-1:0]       ),
    .hready_i              ( s_sram_nx_hready                          ),
    .hsize_i               ( s_sram_nx_hsize                           ),
    .htrans_i              ( s_sram_nx_htrans                          ),
    .hwdata_i              ( s_sram_nx_hwdata                          ),
    .hwrite_i              ( s_sram_nx_hwrite                          ),
    .hsel_i                ( s_sram_nx_hsel                            ),
    .hrdata_o              ( s_sram_nx_hrdata                          ),
    .hreadyout_o           ( s_sram_nx_hreadyout                       ),
    .hresp_o               ( s_sram_nx_hresp                           ),

// SRAM INTERFACE
    .sram_dout_i           ( sram_nx_dout                              ),
    .sram_addr_o           ( sram_nx_addr                              ),
    .sram_cen_o            ( sram_nx_cen                               ),
    .sram_clk_o            ( sram_nx_clk                               ),
    .sram_din_o            ( sram_nx_din                               ),
    .sram_wen_o            ( sram_nx_wen                               )
 );

sram_48kb #(.MEM_ADDRW(SRAM_NX_ADDRW), .MEM_BANKS(SRAM_NX_SIZE / 8192), .ASYNC_RST_EN(ASYNC_RST_EN)) sram_nx_inst (

// OUTPUTs
    .sram_dout_o           ( sram_nx_dout                              ),

// INPUTs
    .sram_addr_i           ( sram_nx_addr                              ),
    .sram_cen_i            ( sram_nx_cen                               ),
    .sram_clk_i            ( sram_nx_clk                               ),
    .sram_rst_i            ( hresetn_i                                 ),
    .sram_din_i            ( sram_nx_din                               ),
    .sram_wen_i            ( sram_nx_wen                               )
);


//=============================================================================
// 6) AHB-TO-APB BRIDGE
//=============================================================================
//
//   64KB at 0x10040000 (decoder bit 2). Three APB4 completers, 4KB each:
//     0x10040000  board I/O                  (psel[0])
//     0x10041000  GPIO (GPIO_1 header)       (psel[1])
//     0x10042000  GPIO (Arduino header)      (psel[2])
//   The rest of the window answers with the AHB ERROR response. PCLK = HCLK.
//
localparam               APB_NR         = 3;

wire        [APB_NR-1:0] apb_psel;
wire              [15:0] apb_paddr;
wire                     apb_penable;
wire                     apb_pwrite;
wire              [31:0] apb_pwdata;
wire               [3:0] apb_pstrb;
wire               [2:0] apb_pprot;
wire                     apb_psmode;
wire               [3:0] apb_pmaster;
wire     [APB_NR*32-1:0] apb_prdata;
wire        [APB_NR-1:0] apb_pready;
wire        [APB_NR-1:0] apb_pslverr;

ahb_apb_bridge #(.SLV_NR       (APB_NR),
                 .ADDRW        (16),
                 .SLV_BASE     ({16'h2000, 16'h1000, 16'h0000}),
                 .SLV_AW       ({5'd12,    5'd12,    5'd12   }),
                 .ASYNC_RST_EN (ASYNC_RST_EN)) ahb_apb_bridge_inst (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_apb_hclk_en                             ),

// AHB INTERFACE
    .haddr_i               ( s_apb_haddr[15:0]                         ),
    .hmaster_i             ( s_apb_hmaster                             ),
    .hprot_i               ( s_apb_hprot                               ),
    .hready_i              ( s_apb_hready                              ),
    .hsize_i               ( s_apb_hsize                               ),
    .hsmode_i              ( s_apb_hauser[0]                           ),
    .htrans_i              ( s_apb_htrans                              ),
    .hwdata_i              ( s_apb_hwdata                              ),
    .hwrite_i              ( s_apb_hwrite                              ),
    .hsel_i                ( s_apb_hsel                                ),
    .hrdata_o              ( s_apb_hrdata                              ),
    .hreadyout_o           ( s_apb_hreadyout                           ),
    .hresp_o               ( s_apb_hresp                               ),

// APB4 REQUESTER
    .psel_o                ( apb_psel                                  ),
    .paddr_o               ( apb_paddr                                 ),
    .penable_o             ( apb_penable                               ),
    .pwrite_o              ( apb_pwrite                                ),
    .pwdata_o              ( apb_pwdata                                ),
    .pstrb_o               ( apb_pstrb                                 ),
    .pprot_o               ( apb_pprot                                 ),
    .psmode_o              ( apb_psmode                                ),
    .pmaster_o             ( apb_pmaster                               ),
    .prdata_i              ( apb_prdata                                ),
    .pready_i              ( apb_pready                                ),
    .pslverr_i             ( apb_pslverr                               )
);

// Every completer decodes the low 7 bits of its 4KB window (the register
// window repeats across it)
wire               [8:0] apb_paddr_15_7_unused       = apb_paddr[15:7];


// Board I/O: APB completer 0
apb_led_key_sw #(.ASYNC_RST_EN(ASYNC_RST_EN)) apb_led_key_sw_inst (

// APB CLOCK & RESET
    .pclk_i                ( hclk_i                                    ),
    .presetn_i             ( hresetn_i                                 ),
    .pclk_en_o             ( apb_lks_pclk_en                           ),

// APB INTERFACE
    .psel_i                ( apb_psel[0]                               ),
    .penable_i             ( apb_penable                               ),
    .paddr_i               ( apb_paddr[6:0]                            ),
    .pwrite_i              ( apb_pwrite                                ),
    .pwdata_i              ( apb_pwdata                                ),
    .pstrb_i               ( apb_pstrb                                 ),
    .pprot_i               ( apb_pprot                                 ),
    .psmode_i              ( apb_psmode                                ),
    .prdata_o              ( apb_prdata[31:0]                          ),
    .pready_o              ( apb_pready[0]                             ),
    .pslverr_o             ( apb_pslverr[0]                            ),

// BOARD I/O
    .led_o                 ( periph0_led_o                             ),
    .key_i                 ( periph0_key_i                             ),
    .sw_i                  ( periph0_sw_i                              ),

// ACTIVE REGISTERS (exposed for testbench probing)
    .tty_data_o            ( periph0_tty_data_o                        ),
    .key_sw_val_o          ( periph0_key_sw_val_o                      ),

// INTERRUPT OUTPUTS
    .irq_key_o             ( periph0_irq_key                           ),
    .irq_sw_o              ( periph0_irq_sw                            )
 );

//=============================================================================
// 7) ACLINT (RISC-V Advanced Core-Local Interruptor)
//=============================================================================
//
//   64KB AHB-Lite slave (decoder bit 3, base 0x02000000).  Provides the
//   machine/supervisor software interrupts (MSIP/SSIP), the machine timer
//   interrupt (MTIP) and the Zicntr `time` snapshot to the core.  PRIV_CHECK_EN
//   is 0 (fabric-policed); hprot_i/hsmode_i are accepted but ignored.
//
ahb_aclint #(.SU_MODE_EN    (SU_MODE_EN),              // S-mode software IRQ (SSWI) with the core's S-mode
             .NUM_HARTS     (1),                       // Single hart
             .PRIV_CHECK_EN (0),                       // Fabric-policed; hprot_i/hsmode_i accepted but ignored
             .LF_SYNC_EN    (1),                       // here the MTIME counter lives (0: real clk_lf_i clock domain, 1: hclk_aon_i clock domain)
             .ASYNC_RST_EN  (ASYNC_RST_EN))      ahb_aclint_inst (

// AHB CLOCK, RESET & WAKEUP
    .hclk_i                ( hclk_i                                    ),
    .hclk_aon_i            ( hclk_aon_i                                ),
    .hclk_aon_en_i         ( 1'b1                                      ),  // hclk_aon_i is free-running in this example (no deep-sleep oscillator controller)
    .scan_mode_i           ( 1'b0                                      ),  // functional mode; no DFT insertion at this level
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_aclint_hclk_en                          ),
    .mtimer_wake_lf_o      ( aclint_mtimer_wake_lf                     ),

// LOW-FREQUENCY CLOCK & RESET
    .clk_lf_i              ( clk_lf_i                                  ),
    .resetn_lf_i           ( resetn_lf_i                               ),

// AHB-LITE SLAVE INTERFACE
    .hsel_i                ( s_aclint_hsel                             ),
    .haddr_i               ( s_aclint_haddr[15:0]                      ),
    .hwrite_i              ( s_aclint_hwrite                           ),
    .hsize_i               ( s_aclint_hsize                            ),
    .htrans_i              ( s_aclint_htrans                           ),
    .hprot_i               ( s_aclint_hprot                            ),
    .hsmode_i              ( s_aclint_hauser[0]                        ),
    .hready_i              ( s_aclint_hready                           ),
    .hwdata_i              ( s_aclint_hwdata                           ),
    .hrdata_o              ( s_aclint_hrdata                           ),
    .hreadyout_o           ( s_aclint_hreadyout                        ),
    .hresp_o               ( s_aclint_hresp                            ),

// PER-HART INTERRUPTS (hclk DOMAIN)
    .irq_m_software_o      ( aclint_irq_m_software_o                   ),
    .irq_m_timer_o         ( aclint_irq_m_timer_o                      ),
    .irq_s_software_o      ( aclint_irq_s_software_o                   ),

// ZICNTR TIME INTERFACE
    .time_req_i            ( aclint_time_req_i                         ),
    .time_gnt_o            ( aclint_time_gnt_o                         ),
    .time_val_o            ( aclint_time_val_o                         )
);


//=============================================================================
// 8) BELENOS PORTS
//=============================================================================
//
//   Register port: decoder bit 4, 4KB at 0x10050000.
//   VRAM window:   decoder bit 5, VRAM_WORDS*4 bytes at 0x82000000.
//   Belenos itself, with its VRAM and LUT macros, is in the top level.
//
assign blnsr_hsel_o       = s_blnsr_hsel;
assign blnsr_haddr_o      = s_blnsr_haddr;
assign blnsr_htrans_o     = s_blnsr_htrans;
assign blnsr_hwrite_o     = s_blnsr_hwrite;
assign blnsr_hsize_o      = s_blnsr_hsize;
assign blnsr_hprot_o      = s_blnsr_hprot;
assign blnsr_hsmode_o     = s_blnsr_hauser[0];
assign blnsr_hready_o     = s_blnsr_hready;
assign blnsr_hwdata_o     = s_blnsr_hwdata;
assign s_blnsr_hrdata     = blnsr_hrdata_i;
assign s_blnsr_hreadyout  = blnsr_hreadyout_i;
assign s_blnsr_hresp      = blnsr_hresp_i;

assign blnsv_hsel_o       = s_blnsv_hsel;
assign blnsv_haddr_o      = s_blnsv_haddr;
assign blnsv_htrans_o     = s_blnsv_htrans;
assign blnsv_hwrite_o     = s_blnsv_hwrite;
assign blnsv_hsize_o      = s_blnsv_hsize;
assign blnsv_hprot_o      = s_blnsv_hprot;
assign blnsv_hsmode_o     = s_blnsv_hauser[0];
assign blnsv_hready_o     = s_blnsv_hready;
assign blnsv_hwdata_o     = s_blnsv_hwdata;
assign s_blnsv_hrdata     = blnsv_hrdata_i;
assign s_blnsv_hreadyout  = blnsv_hreadyout_i;
assign s_blnsv_hresp      = blnsv_hresp_i;

assign s_blns_hclk_en     = blns_hclk_en_i;


//=============================================================================
// 9) PLIC (RISC-V Platform-Level Interrupt Controller)
//=============================================================================
//
//   4MB AHB-Lite slave (decoder bit 6, base 0x0C000000). Context 0 drives the
//   hart's M-mode external interrupt, context 1 its S-mode one. KEY and SW
//   reach the hart only through the PLIC.
//
assign plic_irq_src = {gpio_ard_irq,                        // [47:32] Arduino GPIO pins 15..0
                       3'b000,                              // [31:29] unused
                       periph0_irq_sw,                      // [28]    SW
                       periph0_irq_key,                     // [27]    KEY
                       gpio_irq,                            // [26:1]  GPIO pins 25..0
                       1'b0};                               // [0]     reserved

ahb_plic #(.NUM_SOURCES   (47),                        // Sources 1..47
           .NUM_HARTS     (1),                         // Single hart
           .SU_MODE_EN    (SU_MODE_EN),                // S-mode context with the core's S-mode
           .PRIO_BITS     (3),                         // Priorities 0..7
           .PRIV_CHECK_EN (1),                         // M/S/U access policy from hprot_i[1] + hsmode_i
           .ASYNC_RST_EN  (ASYNC_RST_EN))      ahb_plic_inst (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_plic_hclk_en                            ),

// AHB-LITE SLAVE INTERFACE
    .hsel_i                ( s_plic_hsel                               ),
    .haddr_i               ( s_plic_haddr[21:0]                        ),
    .hwrite_i              ( s_plic_hwrite                             ),
    .hsize_i               ( s_plic_hsize                              ),
    .htrans_i              ( s_plic_htrans                             ),
    .hprot_i               ( s_plic_hprot                              ),
    .hsmode_i              ( s_plic_hauser[0]                          ),
    .hready_i              ( s_plic_hready                             ),
    .hwdata_i              ( s_plic_hwdata                             ),
    .hrdata_o              ( s_plic_hrdata                             ),
    .hreadyout_o           ( s_plic_hreadyout                          ),
    .hresp_o               ( s_plic_hresp                              ),

// INTERRUPT SOURCES
    .irq_src_i             ( plic_irq_src                              ),

// PER-HART INTERRUPT OUTPUTS
    .irq_m_external_o      ( plic_irq_m_external_o                     ),
    .irq_s_external_o      ( plic_irq_s_external_o                     )
);


//=============================================================================
// 10) GPIO
//=============================================================================
//
//   26 pins, APB completer 1: 4KB at 0x10041000; the 128-byte register
//   window repeats across it. Fabric-policed (PRIV_CHECK_EN=0).
//
wire        gpio_wake;

wire              [25:0] gpio_hdr_ie;                 // input enables: FPGA pads keep their receivers on

apb_gpio #(.GPIO_NR       (26),                        // GPIO_1 header pins 1-28
           .ADDRW         (7),                         // 128-byte register window
           .SYNC_EN       (1),                         // Asynchronous pad inputs
           .MDELEG_RST    (9'h10F),                    // MDELEG: Machine mode only, PSLVERR
           .INP_CGATE_EN  (0),                         // No clock gate on the FPGA clock tree
           .ASYNC_RST_EN  (ASYNC_RST_EN))      apb_gpio_hdr_inst (

// APB CLOCK & RESET
    .pclk_i                ( hclk_i                                    ),
    .pclk_aon_i            ( hclk_aon_i                                ),  // input path: pins watched while the bus clock is gated
    .presetn_i             ( hresetn_i                                 ),
    .pclk_en_o             ( apb_gpio_pclk_en                          ),
    .scan_mode_i           ( 1'b0                                      ),  // functional mode; no DFT insertion at this level

// APB INTERFACE
    .psel_i                ( apb_psel[1]                               ),
    .penable_i             ( apb_penable                               ),
    .paddr_i               ( apb_paddr[6:0]                            ),
    .pwrite_i              ( apb_pwrite                                ),
    .pwdata_i              ( apb_pwdata                                ),
    .pstrb_i               ( apb_pstrb                                 ),
    .pprot_i               ( apb_pprot                                 ),
    .psmode_i              ( apb_psmode                                ),
    .prdata_o              ( apb_prdata[63:32]                         ),
    .pready_o              ( apb_pready[1]                             ),
    .pslverr_o             ( apb_pslverr[1]                            ),

// PINS
    .gpio_i                ( gpio_i                                    ),
    .gpio_o                ( gpio_o                                    ),
    .gpio_oe_o             ( gpio_oe_o                                 ),
    .gpio_ie_o             ( gpio_hdr_ie                                 ),  // FPGA pads: input receivers always on

// INTERRUPTS
    .irq_o                 ( gpio_irq                                  ),
    .irq_any_o             ( gpio_irq_any                              ),

// ALTERNATE FUNCTIONS (ALT_NR=0: none)
    .alt0_o_i              ( 26'h0                                     ),
    .alt0_oe_i             ( 26'h0                                     ),
    .alt0_act_o            ( gpio_alt_act_unused[25:0]                 ),
    .alt1_o_i              ( 26'h0                                     ),
    .alt1_oe_i             ( 26'h0                                     ),
    .alt1_act_o            ( gpio_alt_act_unused[51:26]                ),
    .alt2_o_i              ( 26'h0                                     ),
    .alt2_oe_i             ( 26'h0                                     ),
    .alt2_act_o            ( gpio_alt_act_unused[77:52]                ),
    .alt3_o_i              ( 26'h0                                     ),
    .alt3_oe_i             ( 26'h0                                     ),
    .alt3_act_o            ( gpio_alt_act_unused[103:78]               ),

// WAKE (no oscillator controller on this board)
    .gpio_wake_o           ( gpio_wake                                 )
);
wire        gpio_wake_unused = gpio_wake;


//=============================================================================
// 10b) GPIO (Arduino header)
//=============================================================================
//
//   16 pins, ARDUINO_IO[15:0], APB completer 2: 4KB at 0x10042000; the
//   128-byte register window repeats across it. Fabric-policed.
//
wire        gpio_ard_wake;

wire              [15:0] gpio_ard_ie;                 // input enables: FPGA pads keep their receivers on

apb_gpio #(.GPIO_NR       (16),                        // Arduino header D0-D13, SDA, SCL
           .ADDRW         (7),                         // 128-byte register window
           .SYNC_EN       (1),                         // Asynchronous pad inputs
           .MDELEG_RST    (9'h10F),                    // MDELEG: Machine mode only, PSLVERR
           .INP_CGATE_EN  (0),                         // No clock gate on the FPGA clock tree
           .ASYNC_RST_EN  (ASYNC_RST_EN))      apb_gpio_ard_inst (

// APB CLOCK & RESET
    .pclk_i                ( hclk_i                                    ),
    .pclk_aon_i            ( hclk_aon_i                                ),  // input path: pins watched while the bus clock is gated
    .presetn_i             ( hresetn_i                                 ),
    .pclk_en_o             ( apb_gpio_ard_pclk_en                      ),
    .scan_mode_i           ( 1'b0                                      ),  // functional mode; no DFT insertion at this level

// APB INTERFACE
    .psel_i                ( apb_psel[2]                               ),
    .penable_i             ( apb_penable                               ),
    .paddr_i               ( apb_paddr[6:0]                            ),
    .pwrite_i              ( apb_pwrite                                ),
    .pwdata_i              ( apb_pwdata                                ),
    .pstrb_i               ( apb_pstrb                                 ),
    .pprot_i               ( apb_pprot                                 ),
    .psmode_i              ( apb_psmode                                ),
    .prdata_o              ( apb_prdata[95:64]                         ),
    .pready_o              ( apb_pready[2]                             ),
    .pslverr_o             ( apb_pslverr[2]                            ),

// PINS
    .gpio_i                ( gpio_ard_i                                    ),
    .gpio_o                ( gpio_ard_o                                    ),
    .gpio_oe_o             ( gpio_ard_oe_o                                 ),
    .gpio_ie_o             ( gpio_ard_ie                                 ),  // FPGA pads: input receivers always on

// INTERRUPTS
    .irq_o                 ( gpio_ard_irq                                  ),
    .irq_any_o             ( gpio_ard_irq_any                              ),

// ALTERNATE FUNCTIONS (ALT_NR=0: none)
    .alt0_o_i              ( 16'h0                                     ),
    .alt0_oe_i             ( 16'h0                                     ),
    .alt0_act_o            ( gpio_ard_alt_act_unused[15:0]             ),
    .alt1_o_i              ( 16'h0                                     ),
    .alt1_oe_i             ( 16'h0                                     ),
    .alt1_act_o            ( gpio_ard_alt_act_unused[31:16]            ),
    .alt2_o_i              ( 16'h0                                     ),
    .alt2_oe_i             ( 16'h0                                     ),
    .alt2_act_o            ( gpio_ard_alt_act_unused[47:32]            ),
    .alt3_o_i              ( 16'h0                                     ),
    .alt3_oe_i             ( 16'h0                                     ),
    .alt3_act_o            ( gpio_ard_alt_act_unused[63:48]            ),

// WAKE (no oscillator controller on this board)
    .gpio_wake_o           ( gpio_ard_wake                                 )
);
wire        gpio_ard_wake_unused = gpio_ard_wake;


//=============================================================================
// 11) GLOBAL CLOCK ENABLE
//=============================================================================

assign  hclk_en_o       =   interconnect_hclk_en  |
                            s_sram_nx_hclk_en     |
                            s_apb_hclk_en         |
                            apb_lks_pclk_en       |
                            apb_gpio_pclk_en      |
                            apb_gpio_ard_pclk_en  |
                            s_aclint_hclk_en      |
                            s_blns_hclk_en        |
                            s_plic_hclk_en        ;


//=============================================================================
// 12) LINT CLEANUP
//=============================================================================
//
//   Signals whose name contains "unused" are exempt from the lint UNUSEDSIGNAL
//   check.  The wires below consume signals (or signal slices) that are
//   intentionally not used in this FPGA build, and provide a sink for the
//   ahb_aclint mtimer_wake_lf_o output pin (no deep-sleep power controller in
//   this example).
//

// Unused wakeup signal from ACLINT (no deep-sleep power controller in this example)
wire                     aclint_mtimer_wake_lf_unused  = aclint_mtimer_wake_lf;

// Unused decoder/address slices
wire               [5:0] s_x_decoder_1hot_6_1_unused   = s_x_decoder_1hot[6:1];
wire              [16:0] s_sram_nx_haddr_31_15_unused  = s_sram_nx_haddr[31:15];
wire              [15:0] s_apb_haddr_31_16_unused  = s_apb_haddr[31:16];
wire              [15:0] s_aclint_haddr_31_16_unused   = s_aclint_haddr[31:16];
wire              [16:0] pmem_addr_full_29_13_unused   = pmem_addr_full[29:13];
wire               [9:0] s_plic_haddr_31_22_unused     = s_plic_haddr[31:22];

// Combined GPIO interrupt (the PLIC takes the per-pin lines)
wire                     gpio_irq_any_unused           = gpio_irq_any;
wire                     gpio_ard_irq_any_unused       = gpio_ard_irq_any;

// Fully-unused subordinate sideband fields (sram_nx)
wire      [HAUSER_W-1:0] s_sram_nx_hauser_unused       = s_sram_nx_hauser;
wire               [2:0] s_sram_nx_hburst_unused       = s_sram_nx_hburst;
wire               [3:0] s_sram_nx_hmaster_unused      = s_sram_nx_hmaster;
wire                     s_sram_nx_hmastlock_unused    = s_sram_nx_hmastlock;
wire               [3:0] s_sram_nx_hprot_unused        = s_sram_nx_hprot;

// Fully-unused subordinate sideband fields (APB bridge)
wire               [2:0] s_apb_hburst_unused       = s_apb_hburst;
wire               [3:0] apb_pmaster_unused        = apb_pmaster;   // no APB completer reads the manager ID
wire              [25:0] gpio_hdr_ie_unused       = gpio_hdr_ie;   // FPGA pads keep their receivers on
wire              [15:0] gpio_ard_ie_unused       = gpio_ard_ie;   // FPGA pads keep their receivers on
wire                     s_apb_hmastlock_unused    = s_apb_hmastlock;

// Fully-unused subordinate sideband fields (aclint)
wire               [2:0] s_aclint_hburst_unused       = s_aclint_hburst;
wire               [3:0] s_aclint_hmaster_unused      = s_aclint_hmaster;
wire                     s_aclint_hmastlock_unused    = s_aclint_hmastlock;

// Fully-unused subordinate sideband fields (Belenos register port / VRAM window)
wire               [2:0] s_blnsr_hburst_unused        = s_blnsr_hburst;
wire               [3:0] s_blnsr_hmaster_unused       = s_blnsr_hmaster;
wire                     s_blnsr_hmastlock_unused     = s_blnsr_hmastlock;
wire               [2:0] s_blnsv_hburst_unused        = s_blnsv_hburst;
wire               [3:0] s_blnsv_hmaster_unused       = s_blnsv_hmaster;
wire                     s_blnsv_hmastlock_unused     = s_blnsv_hmastlock;

// Fully-unused subordinate sideband fields (PLIC)
wire               [2:0] s_plic_hburst_unused         = s_plic_hburst;
wire               [3:0] s_plic_hmaster_unused        = s_plic_hmaster;
wire                     s_plic_hmastlock_unused      = s_plic_hmastlock;


endmodule

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
//                              - AHB interconnect
//                              - ROM
//                              - SRAM X
//
//                              - SRAM NX
//                              - Periph #0
//                              - Periph #1
//                              - Periph #2
//----------------------------------------------------------------------------

module  ahb_bus_system #(

// PARAMETERs
//======================================
    parameter                ROM_SIZE     = 8*1024,  // Size of the ROM memory instance (in Bytes)
    parameter                SRAM_X_SIZE  = 8*1024,  // Size of the Executable SRAM memory instance (in Bytes)
    parameter                SRAM_NX_SIZE = 8*1024,  // Size of the Non-executable SRAM memory instance (in Bytes)
    parameter                ASYNC_RST_EN = 1'b1     // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

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
    input  wire        [1:0] m_nx_htrans_i,
    input  wire       [31:0] m_nx_hwdata_i,
    input  wire              m_nx_hwrite_i,

    output wire       [31:0] m_nx_hrdata_o,
    output wire              m_nx_hready_o,
    output wire              m_nx_hresp_o,

// AHB PERIPHERAL #0
//========================================
    input  wire       [31:0] periph0_reg_08_i,
    input  wire       [31:0] periph0_reg_09_i,
    input  wire       [31:0] periph0_reg_10_i,
    input  wire       [31:0] periph0_reg_11_i,
    input  wire       [31:0] periph0_reg_12_i,
    input  wire       [31:0] periph0_reg_13_i,
    input  wire       [31:0] periph0_reg_14_i,
    input  wire       [31:0] periph0_reg_15_i,

    output wire       [31:0] periph0_reg_00_o,
    output wire       [31:0] periph0_reg_01_o,
    output wire       [31:0] periph0_reg_02_o,
    output wire       [31:0] periph0_reg_03_o,
    output wire       [31:0] periph0_reg_04_o,
    output wire       [31:0] periph0_reg_05_o,
    output wire       [31:0] periph0_reg_06_o,
    output wire       [31:0] periph0_reg_07_o,

// AHB PERIPHERAL #1
//========================================
    input  wire       [31:0] periph1_reg_08_i,
    input  wire       [31:0] periph1_reg_09_i,
    input  wire       [31:0] periph1_reg_10_i,
    input  wire       [31:0] periph1_reg_11_i,
    input  wire       [31:0] periph1_reg_12_i,
    input  wire       [31:0] periph1_reg_13_i,
    input  wire       [31:0] periph1_reg_14_i,
    input  wire       [31:0] periph1_reg_15_i,

    output wire       [31:0] periph1_reg_00_o,
    output wire       [31:0] periph1_reg_01_o,
    output wire       [31:0] periph1_reg_02_o,
    output wire       [31:0] periph1_reg_03_o,
    output wire       [31:0] periph1_reg_04_o,
    output wire       [31:0] periph1_reg_05_o,
    output wire       [31:0] periph1_reg_06_o,
    output wire       [31:0] periph1_reg_07_o,

// AHB PERIPHERAL #2
//========================================
    input  wire       [31:0] periph2_reg_08_i,
    input  wire       [31:0] periph2_reg_09_i,
    input  wire       [31:0] periph2_reg_10_i,
    input  wire       [31:0] periph2_reg_11_i,
    input  wire       [31:0] periph2_reg_12_i,
    input  wire       [31:0] periph2_reg_13_i,
    input  wire       [31:0] periph2_reg_14_i,
    input  wire       [31:0] periph2_reg_15_i,

    output wire       [31:0] periph2_reg_00_o,
    output wire       [31:0] periph2_reg_01_o,
    output wire       [31:0] periph2_reg_02_o,
    output wire       [31:0] periph2_reg_03_o,
    output wire       [31:0] periph2_reg_04_o,
    output wire       [31:0] periph2_reg_05_o,
    output wire       [31:0] periph2_reg_06_o,
    output wire       [31:0] periph2_reg_07_o,

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
    output wire       [63:0] aclint_time_val_o        // -> core time_val_i

);


//=============================================================================
// 1)  WIRE & REGISTER DEFINITION
//=============================================================================

// Local parameters
localparam               ROM_ADDRW      = $clog2(ROM_SIZE)-2;     // Address width of the ROM memory instance (32b words)
localparam               ROM_HADDRW     = $clog2(ROM_SIZE);       // Address width of the ROM AHB interface (8b words)

localparam               SRAM_X_ADDRW   = $clog2(SRAM_X_SIZE)-2;  // Address width of the Executable SRAM memory instance (32b words)
localparam               SRAM_X_HADDRW  = $clog2(SRAM_X_SIZE);    // Address width of the Executable SRAM AHB interface (8b words)

localparam               SRAM_NX_ADDRW  = $clog2(SRAM_NX_SIZE)-2; // Address width of the Non-executable SRAM memory instance (32b words)
localparam               SRAM_NX_HADDRW = $clog2(SRAM_NX_SIZE);   // Address width of the Non-executable SRAM AHB interface (8b words)

localparam               HAUSER_W       = 1;                      // Width of the HAUSER bus (min value is 1)

// Arbiter Interface
wire                     m_nx_grant;
wire                     m_nx_request;
wire               [1:0] m_grant;
wire               [1:0] m_request;

// Address Decoder Interface
wire               [6:0] s_x_decoder_1hot;
wire              [31:0] s_x_decoder_addr;
wire               [6:0] s_decoder_1hot;
wire              [31:0] s_decoder_addr;

// AHB Subordinate Interfaces
`ifndef FUSED
wire              [31:0] s_rom_hrdata;
wire                     s_rom_hreadyout;
wire                     s_rom_hresp;
wire                     s_rom_hsel;
wire              [31:0] s_rom_haddr;
wire      [HAUSER_W-1:0] s_rom_hauser;
wire               [2:0] s_rom_hburst;
wire               [3:0] s_rom_hmaster;
wire                     s_rom_hmastlock;
wire               [3:0] s_rom_hprot;
wire                     s_rom_hready;
wire               [2:0] s_rom_hsize;
wire               [1:0] s_rom_htrans;
wire              [31:0] s_rom_hwdata;
wire                     s_rom_hwrite;

wire              [31:0] s_sram_x_hrdata;
wire                     s_sram_x_hreadyout;
wire                     s_sram_x_hresp;
wire                     s_sram_x_hsel;
wire              [31:0] s_sram_x_haddr;
wire      [HAUSER_W-1:0] s_sram_x_hauser;
wire               [2:0] s_sram_x_hburst;
wire               [3:0] s_sram_x_hmaster;
wire                     s_sram_x_hmastlock;
wire               [3:0] s_sram_x_hprot;
wire                     s_sram_x_hready;
wire               [2:0] s_sram_x_hsize;
wire               [1:0] s_sram_x_htrans;
wire              [31:0] s_sram_x_hwdata;
wire                     s_sram_x_hwrite;
`endif

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

wire              [31:0] s_periph0_hrdata;
wire                     s_periph0_hreadyout;
wire                     s_periph0_hresp;
wire                     s_periph0_hsel;
wire              [31:0] s_periph0_haddr;
wire      [HAUSER_W-1:0] s_periph0_hauser;
wire               [2:0] s_periph0_hburst;
wire               [3:0] s_periph0_hmaster;
wire                     s_periph0_hmastlock;
wire               [3:0] s_periph0_hprot;
wire                     s_periph0_hready;
wire               [2:0] s_periph0_hsize;
wire               [1:0] s_periph0_htrans;
wire              [31:0] s_periph0_hwdata;
wire                     s_periph0_hwrite;

wire              [31:0] s_periph1_hrdata;
wire                     s_periph1_hreadyout;
wire                     s_periph1_hresp;
wire                     s_periph1_hsel;
wire              [31:0] s_periph1_haddr;
wire      [HAUSER_W-1:0] s_periph1_hauser;
wire               [2:0] s_periph1_hburst;
wire               [3:0] s_periph1_hmaster;
wire                     s_periph1_hmastlock;
wire               [3:0] s_periph1_hprot;
wire                     s_periph1_hready;
wire               [2:0] s_periph1_hsize;
wire               [1:0] s_periph1_htrans;
wire              [31:0] s_periph1_hwdata;
wire                     s_periph1_hwrite;

wire              [31:0] s_periph2_hrdata;
wire                     s_periph2_hreadyout;
wire                     s_periph2_hresp;
wire                     s_periph2_hsel;
wire              [31:0] s_periph2_haddr;
wire      [HAUSER_W-1:0] s_periph2_hauser;
wire               [2:0] s_periph2_hburst;
wire               [3:0] s_periph2_hmaster;
wire                     s_periph2_hmastlock;
wire               [3:0] s_periph2_hprot;
wire                     s_periph2_hready;
wire               [2:0] s_periph2_hsize;
wire               [1:0] s_periph2_htrans;
wire              [31:0] s_periph2_hwdata;
wire                     s_periph2_hwrite;

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

// ROM Interface
wire              [31:0] rom0_dout;
wire     [ROM_ADDRW-1:0] rom0_addr;
wire                     rom0_cen;
wire                     rom0_clk;
wire              [29:0] rom0_addr_full_w;

// Executable SRAM Interface
wire              [31:0] sram_x_dout;
wire  [SRAM_X_ADDRW-1:0] sram_x_addr;
wire                     sram_x_cen;
wire                     sram_x_clk;
wire              [31:0] sram_x_din;
wire               [3:0] sram_x_wen;
wire              [29:0] sram_x_addr_full_w;

// Non-executable SRAM Interface
wire              [31:0] sram_nx_dout;
wire [SRAM_NX_ADDRW-1:0] sram_nx_addr;
wire                     sram_nx_cen;
wire                     sram_nx_clk;
wire              [31:0] sram_nx_din;
wire               [3:0] sram_nx_wen;

// Architectural clock-gating
wire                     interconnect_hclk_en;
wire                     s_rom_hclk_en;
wire                     s_sram_x_hclk_en;
wire                     s_sram_nx_hclk_en;
wire                     s_periph0_hclk_en;
wire                     s_periph1_hclk_en;
wire                     s_periph2_hclk_en;
wire                     s_aclint_hclk_en;


//=============================================================================
// 2)  HIGH-PERF AHB INTERCONNECT
//=============================================================================
`ifdef HIPERF

ahb_interconnect_hiperf #(.NR_M        (1),           // Number of non-executable AHB Managers
                          .NR_S_X      (2),           // Number of AHB Subordinates in executable space
                          .NR_S_NX     (5),           // Number of AHB Subordinates in non-executable space (SRAM-NX + 3 periph + ACLINT)
                          .HAUSER_W    (HAUSER_W),    // Width of the HAUSER bus (min value is 1)
                          .ASYNC_RST_EN(ASYNC_RST_EN) // 1=async active-low reset, 0=synchronous reset
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
    .s_x_decoder_1hot_i    ( s_x_decoder_1hot[1:0]                     ),
    .s_x_decoder_addr_o    ( s_x_decoder_addr                          ),

// EXECUTABLE AHB SUBORDINATE INTERFACES
    .s_x_hrdata_i          ({s_sram_x_hrdata,     s_rom_hrdata        }),
    .s_x_hreadyout_i       ({s_sram_x_hreadyout,  s_rom_hreadyout     }),
    .s_x_hresp_i           ({s_sram_x_hresp,      s_rom_hresp         }),

    .s_x_haddr_o           ({s_sram_x_haddr,      s_rom_haddr         }),
    .s_x_hauser_o          ({s_sram_x_hauser,     s_rom_hauser        }),
    .s_x_hburst_o          ({s_sram_x_hburst,     s_rom_hburst        }),
    .s_x_hmaster_o         ({s_sram_x_hmaster,    s_rom_hmaster       }),
    .s_x_hmastlock_o       ({s_sram_x_hmastlock,  s_rom_hmastlock     }),
    .s_x_hprot_o           ({s_sram_x_hprot,      s_rom_hprot         }),
    .s_x_hready_o          ({s_sram_x_hready,     s_rom_hready        }),
    .s_x_hsel_o            ({s_sram_x_hsel,       s_rom_hsel          }),
    .s_x_hsize_o           ({s_sram_x_hsize,      s_rom_hsize         }),
    .s_x_htrans_o          ({s_sram_x_htrans,     s_rom_htrans        }),
    .s_x_hwdata_o          ({s_sram_x_hwdata,     s_rom_hwdata        }),
    .s_x_hwrite_o          ({s_sram_x_hwrite,     s_rom_hwrite        }),

// NON-EXECUTABLE AHB SUBORDINATE INTERFACES
    .s_nx_hrdata_i         ({s_aclint_hrdata,    s_periph2_hrdata,    s_periph1_hrdata,    s_periph0_hrdata,    s_sram_nx_hrdata    }),
    .s_nx_hreadyout_i      ({s_aclint_hreadyout, s_periph2_hreadyout, s_periph1_hreadyout, s_periph0_hreadyout, s_sram_nx_hreadyout }),
    .s_nx_hresp_i          ({s_aclint_hresp,     s_periph2_hresp,     s_periph1_hresp,     s_periph0_hresp,     s_sram_nx_hresp     }),
    .s_nx_haddr_o          ({s_aclint_haddr,     s_periph2_haddr,     s_periph1_haddr,     s_periph0_haddr,     s_sram_nx_haddr     }),
    .s_nx_hauser_o         ({s_aclint_hauser,    s_periph2_hauser,    s_periph1_hauser,    s_periph0_hauser,    s_sram_nx_hauser    }),
    .s_nx_hburst_o         ({s_aclint_hburst,    s_periph2_hburst,    s_periph1_hburst,    s_periph0_hburst,    s_sram_nx_hburst    }),
    .s_nx_hmaster_o        ({s_aclint_hmaster,   s_periph2_hmaster,   s_periph1_hmaster,   s_periph0_hmaster,   s_sram_nx_hmaster   }),
    .s_nx_hmastlock_o      ({s_aclint_hmastlock, s_periph2_hmastlock, s_periph1_hmastlock, s_periph0_hmastlock, s_sram_nx_hmastlock }),
    .s_nx_hprot_o          ({s_aclint_hprot,     s_periph2_hprot,     s_periph1_hprot,     s_periph0_hprot,     s_sram_nx_hprot     }),
    .s_nx_hready_o         ({s_aclint_hready,    s_periph2_hready,    s_periph1_hready,    s_periph0_hready,    s_sram_nx_hready    }),
    .s_nx_hsel_o           ({s_aclint_hsel,      s_periph2_hsel,      s_periph1_hsel,      s_periph0_hsel,      s_sram_nx_hsel      }),
    .s_nx_hsize_o          ({s_aclint_hsize,     s_periph2_hsize,     s_periph1_hsize,     s_periph0_hsize,     s_sram_nx_hsize     }),
    .s_nx_htrans_o         ({s_aclint_htrans,    s_periph2_htrans,    s_periph1_htrans,    s_periph0_htrans,    s_sram_nx_htrans    }),
    .s_nx_hwdata_o         ({s_aclint_hwdata,    s_periph2_hwdata,    s_periph1_hwdata,    s_periph0_hwdata,    s_sram_nx_hwdata    }),
    .s_nx_hwrite_o         ({s_aclint_hwrite,    s_periph2_hwrite,    s_periph1_hwrite,    s_periph0_hwrite,    s_sram_nx_hwrite    })
);

//=============================================================================
// 2b) FUSED AHB INTERCONNECT
//=============================================================================
`elsif FUSED

ahb_interconnect_fused #(.NR_M         (1),           // Number of non-executable AHB Managers
                         .NR_S_X_ROM   (1),           // Number of fused ROM controllers
                         .NR_S_X_SRAM  (1),           // Number of fused SRAM controllers
                         .NR_S_NX      (5),           // Number of non-executable subordinates (SRAM-NX + 3 periph + ACLINT)
                         .HAUSER_W     (HAUSER_W),    // Width of the HAUSER bus (min value is 1)
                         .FIXED_B_PRIO (1'b1) ,       // Arbitration scheme for fused ROM/SRAM controllers (0=round-robin, 1=fixed Port-B priority
                         .ASYNC_RST_EN (ASYNC_RST_EN) // 1=async active-low reset, 0=synchronous reset
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
    .s_x_decoder_1hot_i    ( s_x_decoder_1hot[1:0]                     ),
    .s_x_decoder_addr_o    ( s_x_decoder_addr                          ),

// FUSED ROM CONTROLLER MEMORY INTERFACES
    .rom_dout_i            ( rom0_dout                                 ),
    .rom_addr_o            ( rom0_addr_full_w                          ),
    .rom_cen_o             ( rom0_cen                                  ),
    .rom_clk_o             ( rom0_clk                                  ),

// FUSED SRAM CONTROLLER MEMORY INTERFACES
    .sram_dout_i           ( sram_x_dout                               ),
    .sram_addr_o           ( sram_x_addr_full_w                        ),
    .sram_cen_o            ( sram_x_cen                                ),
    .sram_clk_o            ( sram_x_clk                                ),
    .sram_din_o            ( sram_x_din                                ),
    .sram_wen_o            ( sram_x_wen                                ),

// NON-EXECUTABLE AHB SUBORDINATE INTERFACES
    .s_nx_hrdata_i         ({s_aclint_hrdata,    s_periph2_hrdata,    s_periph1_hrdata,    s_periph0_hrdata,    s_sram_nx_hrdata    }),
    .s_nx_hreadyout_i      ({s_aclint_hreadyout, s_periph2_hreadyout, s_periph1_hreadyout, s_periph0_hreadyout, s_sram_nx_hreadyout }),
    .s_nx_hresp_i          ({s_aclint_hresp,     s_periph2_hresp,     s_periph1_hresp,     s_periph0_hresp,     s_sram_nx_hresp     }),
    .s_nx_haddr_o          ({s_aclint_haddr,     s_periph2_haddr,     s_periph1_haddr,     s_periph0_haddr,     s_sram_nx_haddr     }),
    .s_nx_hauser_o         ({s_aclint_hauser,    s_periph2_hauser,    s_periph1_hauser,    s_periph0_hauser,    s_sram_nx_hauser    }),
    .s_nx_hburst_o         ({s_aclint_hburst,    s_periph2_hburst,    s_periph1_hburst,    s_periph0_hburst,    s_sram_nx_hburst    }),
    .s_nx_hmaster_o        ({s_aclint_hmaster,   s_periph2_hmaster,   s_periph1_hmaster,   s_periph0_hmaster,   s_sram_nx_hmaster   }),
    .s_nx_hmastlock_o      ({s_aclint_hmastlock, s_periph2_hmastlock, s_periph1_hmastlock, s_periph0_hmastlock, s_sram_nx_hmastlock }),
    .s_nx_hprot_o          ({s_aclint_hprot,     s_periph2_hprot,     s_periph1_hprot,     s_periph0_hprot,     s_sram_nx_hprot     }),
    .s_nx_hready_o         ({s_aclint_hready,    s_periph2_hready,    s_periph1_hready,    s_periph0_hready,    s_sram_nx_hready    }),
    .s_nx_hsel_o           ({s_aclint_hsel,      s_periph2_hsel,      s_periph1_hsel,      s_periph0_hsel,      s_sram_nx_hsel      }),
    .s_nx_hsize_o          ({s_aclint_hsize,     s_periph2_hsize,     s_periph1_hsize,     s_periph0_hsize,     s_sram_nx_hsize     }),
    .s_nx_htrans_o         ({s_aclint_htrans,    s_periph2_htrans,    s_periph1_htrans,    s_periph0_htrans,    s_sram_nx_htrans    }),
    .s_nx_hwdata_o         ({s_aclint_hwdata,    s_periph2_hwdata,    s_periph1_hwdata,    s_periph0_hwdata,    s_sram_nx_hwdata    }),
    .s_nx_hwrite_o         ({s_aclint_hwrite,    s_periph2_hwrite,    s_periph1_hwrite,    s_periph0_hwrite,    s_sram_nx_hwrite    })
);

// Slice the 30-bit fused-controller address output down to the on-chip memory
// address widths.  rom_cen_o / rom_clk_o / sram_cen_o / sram_clk_o / sram_din_o
// / sram_wen_o are all per-slot 1-bit wires (NR_S_X_ROM = NR_S_X_SRAM = 1) and
// connect directly to the SRAM/ROM macros above.
assign rom0_addr   = rom0_addr_full_w  [ROM_ADDRW-1:0];
assign sram_x_addr = sram_x_addr_full_w[SRAM_X_ADDRW-1:0];

// The fused interconnect folds the ROM/SRAM-X controller clock-enable into its
// own hclk_en_o, so the per-subordinate enables are unused at chip level.
assign s_rom_hclk_en    = 1'b0;
assign s_sram_x_hclk_en = 1'b0;

//=============================================================================
// 3)  GENERIC AHB INTERCONNECT
//=============================================================================
`else

ahb_interconnect_generic #(.NR_M        (2),           // Number of AHB Managers
                           .NR_S        (7),           // Number of AHB Subordinates (ROM + SRAM-X + SRAM-NX + 3 periph + ACLINT)
                           .HAUSER_W    (HAUSER_W),    // Width of the HAUSER bus (min value is 1)
                           .ASYNC_RST_EN(ASYNC_RST_EN) // 1=async active-low reset, 0=synchronous reset
)                                        ahb_interconnect_generic_inst (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),

    .hclk_en_o             ( interconnect_hclk_en                      ),

// AHB MANAGER INTERFACES
    .m_haddr_i             ({m_nx_haddr_i,       m_x_haddr_i          }),
    .m_hauser_i            ({m_nx_hsmode_i,      m_x_hsmode_i         }),
    .m_hburst_i            ({m_nx_hburst_i,      m_x_hburst_i         }),
    .m_hmastlock_i         ({m_nx_hmastlock_i,   m_x_hmastlock_i      }),
    .m_hprot_i             ({m_nx_hprot_i,       m_x_hprot_i          }),
    .m_hsize_i             ({m_nx_hsize_i,       m_x_hsize_i          }),
    .m_htrans_i            ({m_nx_htrans_i,      m_x_htrans_i         }),
    .m_hwdata_i            ({m_nx_hwdata_i,      m_x_hwdata_i         }),
    .m_hwrite_i            ({m_nx_hwrite_i,      m_x_hwrite_i         }),

    .m_hrdata_o            ({m_nx_hrdata_o,      m_x_hrdata_o         }),
    .m_hready_o            ({m_nx_hready_o,      m_x_hready_o         }),
    .m_hresp_o             ({m_nx_hresp_o,       m_x_hresp_o          }),

// ARBITER INTERFACES
    .m_grant_i             ( m_grant                                   ),
    .m_request_o           ( m_request                                 ),

// ADDRESS DECODER INTERFACES
    .s_decoder_1hot_i      ( s_decoder_1hot                            ),
    .s_decoder_addr_o      ( s_decoder_addr                            ),

// AHB SUBORDINATE INTERFACES
    .s_hrdata_i            ({s_aclint_hrdata,    s_periph2_hrdata,    s_periph1_hrdata,    s_periph0_hrdata,    s_sram_nx_hrdata,    s_sram_x_hrdata,     s_rom_hrdata    }),
    .s_hreadyout_i         ({s_aclint_hreadyout, s_periph2_hreadyout, s_periph1_hreadyout, s_periph0_hreadyout, s_sram_nx_hreadyout, s_sram_x_hreadyout,  s_rom_hreadyout }),
    .s_hresp_i             ({s_aclint_hresp,     s_periph2_hresp,     s_periph1_hresp,     s_periph0_hresp,     s_sram_nx_hresp,     s_sram_x_hresp,      s_rom_hresp     }),
    .s_haddr_o             ({s_aclint_haddr,     s_periph2_haddr,     s_periph1_haddr,     s_periph0_haddr,     s_sram_nx_haddr,     s_sram_x_haddr,      s_rom_haddr     }),
    .s_hauser_o            ({s_aclint_hauser,    s_periph2_hauser,    s_periph1_hauser,    s_periph0_hauser,    s_sram_nx_hauser,    s_sram_x_hauser,     s_rom_hauser    }),
    .s_hburst_o            ({s_aclint_hburst,    s_periph2_hburst,    s_periph1_hburst,    s_periph0_hburst,    s_sram_nx_hburst,    s_sram_x_hburst,     s_rom_hburst    }),
    .s_hmaster_o           ({s_aclint_hmaster,   s_periph2_hmaster,   s_periph1_hmaster,   s_periph0_hmaster,   s_sram_nx_hmaster,   s_sram_x_hmaster,    s_rom_hmaster   }),
    .s_hmastlock_o         ({s_aclint_hmastlock, s_periph2_hmastlock, s_periph1_hmastlock, s_periph0_hmastlock, s_sram_nx_hmastlock, s_sram_x_hmastlock,  s_rom_hmastlock }),
    .s_hprot_o             ({s_aclint_hprot,     s_periph2_hprot,     s_periph1_hprot,     s_periph0_hprot,     s_sram_nx_hprot,     s_sram_x_hprot,      s_rom_hprot     }),
    .s_hready_o            ({s_aclint_hready,    s_periph2_hready,    s_periph1_hready,    s_periph0_hready,    s_sram_nx_hready,    s_sram_x_hready,     s_rom_hready    }),
    .s_hsel_o              ({s_aclint_hsel,      s_periph2_hsel,      s_periph1_hsel,      s_periph0_hsel,      s_sram_nx_hsel,      s_sram_x_hsel,       s_rom_hsel      }),
    .s_hsize_o             ({s_aclint_hsize,     s_periph2_hsize,     s_periph1_hsize,     s_periph0_hsize,     s_sram_nx_hsize,     s_sram_x_hsize,      s_rom_hsize     }),
    .s_htrans_o            ({s_aclint_htrans,    s_periph2_htrans,    s_periph1_htrans,    s_periph0_htrans,    s_sram_nx_htrans,    s_sram_x_htrans,     s_rom_htrans    }),
    .s_hwdata_o            ({s_aclint_hwdata,    s_periph2_hwdata,    s_periph1_hwdata,    s_periph0_hwdata,    s_sram_nx_hwdata,    s_sram_x_hwdata,     s_rom_hwdata    }),
    .s_hwrite_o            ({s_aclint_hwrite,    s_periph2_hwrite,    s_periph1_hwrite,    s_periph0_hwrite,    s_sram_nx_hwrite,    s_sram_x_hwrite,     s_rom_hwrite    })
 );

`endif


//=============================================================================
// 4)  ADDRESS DECODER
//=============================================================================

`ifdef HIPERF
// No arbitration, only one NX master
assign m_nx_grant = m_nx_request;

ahb_decoder #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE)) ahb_decoder_x_inst (
    .decoder_addr_i        ( s_x_decoder_addr                          ),
    .decoder_1hot_o        ( s_x_decoder_1hot                          )
);
`elsif FUSED
// No arbitration, only one NX master
assign m_nx_grant = m_nx_request;

ahb_decoder #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE)) ahb_decoder_x_inst (
    .decoder_addr_i        ( s_x_decoder_addr                          ),
    .decoder_1hot_o        ( s_x_decoder_1hot                          )
);
`else

ahb_arbiter #(.ASYNC_RST_EN(ASYNC_RST_EN)) ahb_arbiter_inst (

    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .request_i             ( m_request                                 ),
    .grant_o               ( m_grant                                   )
);

`endif

ahb_decoder #(.ROM_SIZE(ROM_SIZE), .SRAM_X_SIZE(SRAM_X_SIZE), .SRAM_NX_SIZE(SRAM_NX_SIZE)) ahb_decoder_inst   (

    .decoder_addr_i        ( s_decoder_addr                            ),
    .decoder_1hot_o        ( s_decoder_1hot                            )
);


//=============================================================================
// 5)  ROM MEMORY
//=============================================================================

// In FUSED mode the ROM controller is embedded inside ahb_interconnect_fused
// and drives rom0_addr/rom0_cen/rom0_clk directly (see section 2b).
`ifndef FUSED
ahb_rom_controller #(.MEM_SIZE(ROM_SIZE), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_rom_ctrl_inst0 (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_rom_hclk_en                             ),

// AHB INTERFACE
    .haddr_i               ( s_rom_haddr[ROM_HADDRW-1:0]               ),
    .hready_i              ( s_rom_hready                              ),
    .hsize_i               ( s_rom_hsize                               ),
    .htrans_i              ( s_rom_htrans                              ),
    .hwdata_i              ( s_rom_hwdata                              ),
    .hwrite_i              ( s_rom_hwrite                              ),
    .hsel_i                ( s_rom_hsel                                ),
    .hrdata_o              ( s_rom_hrdata                              ),
    .hreadyout_o           ( s_rom_hreadyout                           ),
    .hresp_o               ( s_rom_hresp                               ),

// ROM INTERFACE
    .rom_dout_i            ( rom0_dout                                 ),
    .rom_addr_o            ( rom0_addr                                 ),
    .rom_cen_o             ( rom0_cen                                  ),
    .rom_clk_o             ( rom0_clk                                  )
 );
`endif

rom_32kb #(ROM_ADDRW) rom_inst0 (

// OUTPUTs
    .rom_dout_o            ( rom0_dout                                 ),

// INPUTs
    .rom_addr_i            ( rom0_addr                                 ),
    .rom_cen_i             ( rom0_cen                                  ),
    .rom_clk_i             ( rom0_clk                                  ),
    .rom_rst_i             ( hresetn_i                                 )
);


//=============================================================================
// 6) EXECUTABLE SRAM MEMORY
//=============================================================================

// In FUSED mode the SRAM-X controller is embedded inside ahb_interconnect_fused
// and drives sram_x_addr/cen/clk/din/wen directly (see section 2b).
`ifndef FUSED
ahb_sram_controller #(.MEM_SIZE(SRAM_X_SIZE), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_sram_x_ctrl_inst (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_sram_x_hclk_en                          ),

// AHB INTERFACE
    .haddr_i               ( s_sram_x_haddr[SRAM_X_HADDRW-1:0]         ),
    .hready_i              ( s_sram_x_hready                           ),
    .hsize_i               ( s_sram_x_hsize                            ),
    .htrans_i              ( s_sram_x_htrans                           ),
    .hwdata_i              ( s_sram_x_hwdata                           ),
    .hwrite_i              ( s_sram_x_hwrite                           ),
    .hsel_i                ( s_sram_x_hsel                             ),
    .hrdata_o              ( s_sram_x_hrdata                           ),
    .hreadyout_o           ( s_sram_x_hreadyout                        ),
    .hresp_o               ( s_sram_x_hresp                            ),

// SRAM INTERFACE
    .sram_dout_i           ( sram_x_dout                               ),
    .sram_addr_o           ( sram_x_addr                               ),
    .sram_cen_o            ( sram_x_cen                                ),
    .sram_clk_o            ( sram_x_clk                                ),
    .sram_din_o            ( sram_x_din                                ),
    .sram_wen_o            ( sram_x_wen                                )
 );
`endif

sram_32kb #(SRAM_X_ADDRW) sram_x_inst (

// OUTPUTs
    .sram_dout_o           ( sram_x_dout                               ),

// INPUTs
    .sram_addr_i           ( sram_x_addr                               ),
    .sram_cen_i            ( sram_x_cen                                ),
    .sram_clk_i            ( sram_x_clk                                ),
    .sram_rst_i            ( hresetn_i                                 ),
    .sram_din_i            ( sram_x_din                                ),
    .sram_wen_i            ( sram_x_wen                                )
);

//=============================================================================
// 7) NON-EXECUTABLE SRAM MEMORY
//=============================================================================

ahb_sram_controller #(.MEM_SIZE(SRAM_NX_SIZE), .ASYNC_RST_EN(ASYNC_RST_EN)) ahb_sram_nx_ctrl_inst (

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

sram_32kb #(SRAM_NX_ADDRW) sram_nx_inst (

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
// 8) AHB PERIPHERAL #0
//=============================================================================

ahb_periph_example #(.ASYNC_RST_EN(ASYNC_RST_EN)) ahb_periph_example_inst0 (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_periph0_hclk_en                         ),

// AHB INTERFACE
    .haddr_i               ( s_periph0_haddr[6:0]                      ),
    .hprot_i               ( s_periph0_hprot                           ),
    .hready_i              ( s_periph0_hready                          ),
    .hsize_i               ( s_periph0_hsize                           ),
    .hsmode_i              ( s_periph0_hauser[0]                       ),
    .htrans_i              ( s_periph0_htrans                          ),
    .hwdata_i              ( s_periph0_hwdata                          ),
    .hwrite_i              ( s_periph0_hwrite                          ),
    .hsel_i                ( s_periph0_hsel                            ),
    .hrdata_o              ( s_periph0_hrdata                          ),
    .hreadyout_o           ( s_periph0_hreadyout                       ),
    .hresp_o               ( s_periph0_hresp                           ),

// REGISTERS (FOR PROBING)
    .register_00_o         ( periph0_reg_00_o                          ),
    .register_01_o         ( periph0_reg_01_o                          ),
    .register_02_o         ( periph0_reg_02_o                          ),
    .register_03_o         ( periph0_reg_03_o                          ),
    .register_04_o         ( periph0_reg_04_o                          ),
    .register_05_o         ( periph0_reg_05_o                          ),
    .register_06_o         ( periph0_reg_06_o                          ),
    .register_07_o         ( periph0_reg_07_o                          ),

    .register_08_i         ( periph0_reg_08_i                          ),
    .register_09_i         ( periph0_reg_09_i                          ),
    .register_10_i         ( periph0_reg_10_i                          ),
    .register_11_i         ( periph0_reg_11_i                          ),
    .register_12_i         ( periph0_reg_12_i                          ),
    .register_13_i         ( periph0_reg_13_i                          ),
    .register_14_i         ( periph0_reg_14_i                          ),
    .register_15_i         ( periph0_reg_15_i                          )
 );

//=============================================================================
// 9) AHB PERIPHERAL #1
//=============================================================================

ahb_periph_example #(.ASYNC_RST_EN(ASYNC_RST_EN)) ahb_periph_example_inst1 (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_periph1_hclk_en                         ),

// AHB INTERFACE
    .haddr_i               ( s_periph1_haddr[6:0]                      ),
    .hprot_i               ( s_periph1_hprot                           ),
    .hready_i              ( s_periph1_hready                          ),
    .hsize_i               ( s_periph1_hsize                           ),
    .hsmode_i              ( s_periph1_hauser[0]                       ),
    .htrans_i              ( s_periph1_htrans                          ),
    .hwdata_i              ( s_periph1_hwdata                          ),
    .hwrite_i              ( s_periph1_hwrite                          ),
    .hsel_i                ( s_periph1_hsel                            ),
    .hrdata_o              ( s_periph1_hrdata                          ),
    .hreadyout_o           ( s_periph1_hreadyout                       ),
    .hresp_o               ( s_periph1_hresp                           ),

// REGISTERS (FOR PROBING)
    .register_00_o         ( periph1_reg_00_o                          ),
    .register_01_o         ( periph1_reg_01_o                          ),
    .register_02_o         ( periph1_reg_02_o                          ),
    .register_03_o         ( periph1_reg_03_o                          ),
    .register_04_o         ( periph1_reg_04_o                          ),
    .register_05_o         ( periph1_reg_05_o                          ),
    .register_06_o         ( periph1_reg_06_o                          ),
    .register_07_o         ( periph1_reg_07_o                          ),

    .register_08_i         ( periph1_reg_08_i                          ),
    .register_09_i         ( periph1_reg_09_i                          ),
    .register_10_i         ( periph1_reg_10_i                          ),
    .register_11_i         ( periph1_reg_11_i                          ),
    .register_12_i         ( periph1_reg_12_i                          ),
    .register_13_i         ( periph1_reg_13_i                          ),
    .register_14_i         ( periph1_reg_14_i                          ),
    .register_15_i         ( periph1_reg_15_i                          )
 );

//=============================================================================
// 10) AHB PERIPHERAL #2
//=============================================================================

ahb_periph_example #(.ASYNC_RST_EN(ASYNC_RST_EN)) ahb_periph_example_inst2 (

// AHB CLOCK & RESET
    .hclk_i                ( hclk_i                                    ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_periph2_hclk_en                         ),

// AHB INTERFACE
    .haddr_i               ( s_periph2_haddr[6:0]                      ),
    .hprot_i               ( s_periph2_hprot                           ),
    .hready_i              ( s_periph2_hready                          ),
    .hsize_i               ( s_periph2_hsize                           ),
    .hsmode_i              ( s_periph2_hauser[0]                       ),
    .htrans_i              ( s_periph2_htrans                          ),
    .hwdata_i              ( s_periph2_hwdata                          ),
    .hwrite_i              ( s_periph2_hwrite                          ),
    .hsel_i                ( s_periph2_hsel                            ),
    .hrdata_o              ( s_periph2_hrdata                          ),
    .hreadyout_o           ( s_periph2_hreadyout                       ),
    .hresp_o               ( s_periph2_hresp                           ),

// REGISTERS (FOR PROBING)
    .register_00_o         ( periph2_reg_00_o                          ),
    .register_01_o         ( periph2_reg_01_o                          ),
    .register_02_o         ( periph2_reg_02_o                          ),
    .register_03_o         ( periph2_reg_03_o                          ),
    .register_04_o         ( periph2_reg_04_o                          ),
    .register_05_o         ( periph2_reg_05_o                          ),
    .register_06_o         ( periph2_reg_06_o                          ),
    .register_07_o         ( periph2_reg_07_o                          ),

    .register_08_i         ( periph2_reg_08_i                          ),
    .register_09_i         ( periph2_reg_09_i                          ),
    .register_10_i         ( periph2_reg_10_i                          ),
    .register_11_i         ( periph2_reg_11_i                          ),
    .register_12_i         ( periph2_reg_12_i                          ),
    .register_13_i         ( periph2_reg_13_i                          ),
    .register_14_i         ( periph2_reg_14_i                          ),
    .register_15_i         ( periph2_reg_15_i                          )
 );

//=============================================================================
// 11) ACLINT (RISC-V Advanced Core-Local Interruptor)
//=============================================================================
//
//   64KB AHB-Lite slave (decoder bit 6, base 0x02000000).  Provides the
//   machine/supervisor software interrupts (MSIP/SSIP), the machine timer
//   interrupt (MTIP) and the Zicntr `time` snapshot to the core.  PRIV_CHECK_EN
//   is 0 (fabric-policed); hprot_i/hsmode_i are accepted but ignored.
//
ahb_aclint #(.SU_MODE_EN    (1),                       // S-mode software IRQ (SSWI) present; match core SU_MODE_EN
             .NUM_HARTS     (1),                       // Single hart
             .PRIV_CHECK_EN (0),                       // Fabric-policed; hprot_i/hsmode_i accepted but ignored
             .ASYNC_RST_EN  (ASYNC_RST_EN)) ahb_aclint_inst ( // 1=async active-low reset, 0=synchronous reset

// AHB CLOCK, RESET & WAKEUP
    .hclk_i                ( hclk_i                                    ),
    .hclk_aon_i            ( hclk_aon_i                                ),
    .hresetn_i             ( hresetn_i                                 ),
    .hclk_en_o             ( s_aclint_hclk_en                          ),
    .mtimer_wake_lf_o      (                                           ), // No deep-sleep power controller in this example

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
// 12) GLOBAL CLOCK ENABLE
//=============================================================================

assign  hclk_en_o       =   interconnect_hclk_en  |
                            s_rom_hclk_en         |
                            s_sram_x_hclk_en      |
                            s_sram_nx_hclk_en     |
                            s_periph0_hclk_en     |
                            s_periph1_hclk_en     |
                            s_periph2_hclk_en     |
                            s_aclint_hclk_en      ;



endmodule

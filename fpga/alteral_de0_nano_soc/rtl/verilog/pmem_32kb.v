//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    pmem_32kb
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : pmem_32kb.v
// Module Description : 32KB writable PROGRAM MEMORY using direct altsyncram
//                     instantiation (MegaWizard-style) with init_file string
//                     literals. 4 x 8KB banks, each initialised from its MIF at
//                     configuration time AND run-time writable.
//
//   This is rom_32kb's structure (4 unrolled banks so each init_file is a literal,
//   as Quartus requires) with the write port from sram_8kb_wrapper grafted in: the
//   banks boot the firmware image (pmem_bankN.mif) exactly like the ROM did, but a
//   debugger (or the core) can now overwrite them via the data bus / SBA. This is
//   what lets the host tool load firmware to 0x20000000 while the board still
//   self-boots the preloaded image.
//----------------------------------------------------------------------------

module pmem_32kb (

// OUTPUTs
    pmem_dout_o,                    // Data output

// INPUTs
    pmem_addr_i,                    // Address
    pmem_cen_i,                     // Chip enable (low active)
    pmem_clk_i,                     // Clock
    pmem_rst_i,                     // Output buffer reset
    pmem_din_i,                     // Data input
    pmem_wen_i                      // Byte write enable (low active)
);

// PARAMETERs
//============
parameter MEM_ADDRW    =  13;           // Width of the address bus
parameter ASYNC_RST_EN = 1'b1;          // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

// OUTPUTs
//============
output         [31:0] pmem_dout_o;   // Data output

// INPUTs
//============
input [MEM_ADDRW-1:0] pmem_addr_i;   // Address
input                 pmem_cen_i;    // Chip enable (low active)
input                 pmem_clk_i;    // Clock
input                 pmem_rst_i;    // Output buffer reset
input          [31:0] pmem_din_i;    // Data input
input           [3:0] pmem_wen_i;    // Byte write enable (low active)


// PROGRAM-MEMORY INSTANCES
//==========================
// Direct altsyncram instantiation (MegaWizard-style) with init_file as string
// literals in each bank's defparam. Quartus requires init_file to be a literal
// in the same module as the altsyncram instantiation, hence the 4 unrolled banks.

    localparam PMEM_ADDRW = 11;                 // 2048 words (8KB) per bank

    // Active-high chip enables per bank (active when chip selected AND bank matches)
    wire cen_0 = pmem_cen_i | (pmem_addr_i[MEM_ADDRW-1:PMEM_ADDRW] != 2'b00);
    wire cen_1 = pmem_cen_i | (pmem_addr_i[MEM_ADDRW-1:PMEM_ADDRW] != 2'b01);
    wire cen_2 = pmem_cen_i | (pmem_addr_i[MEM_ADDRW-1:PMEM_ADDRW] != 2'b10);
    wire cen_3 = pmem_cen_i | (pmem_addr_i[MEM_ADDRW-1:PMEM_ADDRW] != 2'b11);

    // Active-high byte enables and a per-bank write strobe (write when the bank is
    // selected and at least one byte lane is enabled). Same encoding as sram_8kb_wrapper.
    wire [3:0] byten = ~pmem_wen_i;
    wire       any_w = ~(&pmem_wen_i);
    wire       wren_0 = ~cen_0 & any_w;
    wire       wren_1 = ~cen_1 & any_w;
    wire       wren_2 = ~cen_2 & any_w;
    wire       wren_3 = ~cen_3 & any_w;

    wire [31:0] pmem_dout_0;
    wire [31:0] pmem_dout_1;
    wire [31:0] pmem_dout_2;
    wire [31:0] pmem_dout_3;

    // Tie-offs for unused altsyncram outputs (per-bank).
    wire [2:0] eccstatus_bank0_unused;
    wire       q_b_bank0_unused;
    wire [2:0] eccstatus_bank1_unused;
    wire       q_b_bank1_unused;
    wire [2:0] eccstatus_bank2_unused;
    wire       q_b_bank2_unused;
    wire [2:0] eccstatus_bank3_unused;
    wire       q_b_bank3_unused;

    //--------- Bank 0 ---------
    altsyncram altsyncram_bank0 (
        .address_a         ( pmem_addr_i[PMEM_ADDRW-1:0] ),
        .byteena_a         ( byten                     ),
        .clock0            ( pmem_clk_i                ),
        .clocken0          ( ~cen_0                    ),
        .data_a            ( pmem_din_i                ),
        .wren_a            ( wren_0                    ),
        .q_a               ( pmem_dout_0               ),
        .aclr0             ( 1'b0                      ),
        .aclr1             ( 1'b0                      ),
        .address_b         ( 1'b1                      ),
        .addressstall_a    ( 1'b0                      ),
        .addressstall_b    ( 1'b0                      ),
        .byteena_b         ( 1'b1                      ),
        .clock1            ( 1'b1                      ),
        .clocken1          ( 1'b1                      ),
        .clocken2          ( 1'b1                      ),
        .clocken3          ( 1'b1                      ),
        .data_b            ( 1'b1                      ),
        .eccstatus         ( eccstatus_bank0_unused    ),
        .q_b               ( q_b_bank0_unused          ),
        .rden_a            ( 1'b1                      ),
        .rden_b            ( 1'b1                      ),
        .wren_b            ( 1'b0                      )
    );
    defparam
        altsyncram_bank0.byte_size                          = 8,
        altsyncram_bank0.clock_enable_input_a               = "NORMAL",
        altsyncram_bank0.clock_enable_output_a              = "BYPASS",
        altsyncram_bank0.init_file                          = "pmem_bank0.mif",
        altsyncram_bank0.intended_device_family             = "Cyclone V",
        altsyncram_bank0.lpm_hint                           = "ENABLE_RUNTIME_MOD=NO",
        altsyncram_bank0.lpm_type                           = "altsyncram",
        altsyncram_bank0.numwords_a                         = 2048,
        altsyncram_bank0.operation_mode                     = "SINGLE_PORT",
        altsyncram_bank0.outdata_aclr_a                     = "NONE",
        altsyncram_bank0.outdata_reg_a                      = "UNREGISTERED",
        altsyncram_bank0.power_up_uninitialized             = "FALSE",
        altsyncram_bank0.read_during_write_mode_port_a      = "NEW_DATA_NO_NBE_READ",
        altsyncram_bank0.widthad_a                          = PMEM_ADDRW,
        altsyncram_bank0.width_a                            = 32,
        altsyncram_bank0.width_byteena_a                    = 4;

    //--------- Bank 1 ---------
    altsyncram altsyncram_bank1 (
        .address_a         ( pmem_addr_i[PMEM_ADDRW-1:0] ),
        .byteena_a         ( byten                     ),
        .clock0            ( pmem_clk_i                ),
        .clocken0          ( ~cen_1                    ),
        .data_a            ( pmem_din_i                ),
        .wren_a            ( wren_1                    ),
        .q_a               ( pmem_dout_1               ),
        .aclr0             ( 1'b0                      ),
        .aclr1             ( 1'b0                      ),
        .address_b         ( 1'b1                      ),
        .addressstall_a    ( 1'b0                      ),
        .addressstall_b    ( 1'b0                      ),
        .byteena_b         ( 1'b1                      ),
        .clock1            ( 1'b1                      ),
        .clocken1          ( 1'b1                      ),
        .clocken2          ( 1'b1                      ),
        .clocken3          ( 1'b1                      ),
        .data_b            ( 1'b1                      ),
        .eccstatus         ( eccstatus_bank1_unused    ),
        .q_b               ( q_b_bank1_unused          ),
        .rden_a            ( 1'b1                      ),
        .rden_b            ( 1'b1                      ),
        .wren_b            ( 1'b0                      )
    );
    defparam
        altsyncram_bank1.byte_size                          = 8,
        altsyncram_bank1.clock_enable_input_a               = "NORMAL",
        altsyncram_bank1.clock_enable_output_a              = "BYPASS",
        altsyncram_bank1.init_file                          = "pmem_bank1.mif",
        altsyncram_bank1.intended_device_family             = "Cyclone V",
        altsyncram_bank1.lpm_hint                           = "ENABLE_RUNTIME_MOD=NO",
        altsyncram_bank1.lpm_type                           = "altsyncram",
        altsyncram_bank1.numwords_a                         = 2048,
        altsyncram_bank1.operation_mode                     = "SINGLE_PORT",
        altsyncram_bank1.outdata_aclr_a                     = "NONE",
        altsyncram_bank1.outdata_reg_a                      = "UNREGISTERED",
        altsyncram_bank1.power_up_uninitialized             = "FALSE",
        altsyncram_bank1.read_during_write_mode_port_a      = "NEW_DATA_NO_NBE_READ",
        altsyncram_bank1.widthad_a                          = PMEM_ADDRW,
        altsyncram_bank1.width_a                            = 32,
        altsyncram_bank1.width_byteena_a                    = 4;

    //--------- Bank 2 ---------
    altsyncram altsyncram_bank2 (
        .address_a         ( pmem_addr_i[PMEM_ADDRW-1:0] ),
        .byteena_a         ( byten                     ),
        .clock0            ( pmem_clk_i                ),
        .clocken0          ( ~cen_2                    ),
        .data_a            ( pmem_din_i                ),
        .wren_a            ( wren_2                    ),
        .q_a               ( pmem_dout_2               ),
        .aclr0             ( 1'b0                      ),
        .aclr1             ( 1'b0                      ),
        .address_b         ( 1'b1                      ),
        .addressstall_a    ( 1'b0                      ),
        .addressstall_b    ( 1'b0                      ),
        .byteena_b         ( 1'b1                      ),
        .clock1            ( 1'b1                      ),
        .clocken1          ( 1'b1                      ),
        .clocken2          ( 1'b1                      ),
        .clocken3          ( 1'b1                      ),
        .data_b            ( 1'b1                      ),
        .eccstatus         ( eccstatus_bank2_unused    ),
        .q_b               ( q_b_bank2_unused          ),
        .rden_a            ( 1'b1                      ),
        .rden_b            ( 1'b1                      ),
        .wren_b            ( 1'b0                      )
    );
    defparam
        altsyncram_bank2.byte_size                          = 8,
        altsyncram_bank2.clock_enable_input_a               = "NORMAL",
        altsyncram_bank2.clock_enable_output_a              = "BYPASS",
        altsyncram_bank2.init_file                          = "pmem_bank2.mif",
        altsyncram_bank2.intended_device_family             = "Cyclone V",
        altsyncram_bank2.lpm_hint                           = "ENABLE_RUNTIME_MOD=NO",
        altsyncram_bank2.lpm_type                           = "altsyncram",
        altsyncram_bank2.numwords_a                         = 2048,
        altsyncram_bank2.operation_mode                     = "SINGLE_PORT",
        altsyncram_bank2.outdata_aclr_a                     = "NONE",
        altsyncram_bank2.outdata_reg_a                      = "UNREGISTERED",
        altsyncram_bank2.power_up_uninitialized             = "FALSE",
        altsyncram_bank2.read_during_write_mode_port_a      = "NEW_DATA_NO_NBE_READ",
        altsyncram_bank2.widthad_a                          = PMEM_ADDRW,
        altsyncram_bank2.width_a                            = 32,
        altsyncram_bank2.width_byteena_a                    = 4;

    //--------- Bank 3 ---------
    altsyncram altsyncram_bank3 (
        .address_a         ( pmem_addr_i[PMEM_ADDRW-1:0] ),
        .byteena_a         ( byten                     ),
        .clock0            ( pmem_clk_i                ),
        .clocken0          ( ~cen_3                    ),
        .data_a            ( pmem_din_i                ),
        .wren_a            ( wren_3                    ),
        .q_a               ( pmem_dout_3               ),
        .aclr0             ( 1'b0                      ),
        .aclr1             ( 1'b0                      ),
        .address_b         ( 1'b1                      ),
        .addressstall_a    ( 1'b0                      ),
        .addressstall_b    ( 1'b0                      ),
        .byteena_b         ( 1'b1                      ),
        .clock1            ( 1'b1                      ),
        .clocken1          ( 1'b1                      ),
        .clocken2          ( 1'b1                      ),
        .clocken3          ( 1'b1                      ),
        .data_b            ( 1'b1                      ),
        .eccstatus         ( eccstatus_bank3_unused    ),
        .q_b               ( q_b_bank3_unused          ),
        .rden_a            ( 1'b1                      ),
        .rden_b            ( 1'b1                      ),
        .wren_b            ( 1'b0                      )
    );
    defparam
        altsyncram_bank3.byte_size                          = 8,
        altsyncram_bank3.clock_enable_input_a               = "NORMAL",
        altsyncram_bank3.clock_enable_output_a              = "BYPASS",
        altsyncram_bank3.init_file                          = "pmem_bank3.mif",
        altsyncram_bank3.intended_device_family             = "Cyclone V",
        altsyncram_bank3.lpm_hint                           = "ENABLE_RUNTIME_MOD=NO",
        altsyncram_bank3.lpm_type                           = "altsyncram",
        altsyncram_bank3.numwords_a                         = 2048,
        altsyncram_bank3.operation_mode                     = "SINGLE_PORT",
        altsyncram_bank3.outdata_aclr_a                     = "NONE",
        altsyncram_bank3.outdata_reg_a                      = "UNREGISTERED",
        altsyncram_bank3.power_up_uninitialized             = "FALSE",
        altsyncram_bank3.read_during_write_mode_port_a      = "NEW_DATA_NO_NBE_READ",
        altsyncram_bank3.widthad_a                          = PMEM_ADDRW,
        altsyncram_bank3.width_a                            = 32,
        altsyncram_bank3.width_byteena_a                    = 4;

    // Bank select registered to match altsyncram's internal output register.
    // Uses the shared arv_ipdff primitive so the reset architecture honors
    // ASYNC_RST_EN uniformly with the rest of the design.
    wire [MEM_ADDRW-PMEM_ADDRW-1:0] pmem_bank_sel;
    arv_ipdff #(.WIDTH(MEM_ADDRW-PMEM_ADDRW), .ARST_EN(ASYNC_RST_EN)) u_pmem_bank_sel (
        .clk_i  (pmem_clk_i),
        .rst_n_i(pmem_rst_i),
        .en_i   (~pmem_cen_i),
        .d_i    (pmem_addr_i[MEM_ADDRW-1:PMEM_ADDRW]),
        .q_o    (pmem_bank_sel));

    // Output mux
    reg [31:0] pmem_dout_mux;
    always @(*)
        case (pmem_bank_sel)
            2'b00:   pmem_dout_mux = pmem_dout_0;
            2'b01:   pmem_dout_mux = pmem_dout_1;
            2'b10:   pmem_dout_mux = pmem_dout_2;
            2'b11:   pmem_dout_mux = pmem_dout_3;
            default: pmem_dout_mux = 32'h00000000;
        endcase

    assign pmem_dout_o = pmem_dout_mux;


endmodule // pmem_32kb

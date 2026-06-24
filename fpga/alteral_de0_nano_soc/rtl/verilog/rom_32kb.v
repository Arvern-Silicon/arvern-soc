//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    rom_32kb
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : rom_32kb.v
// Module Description : 32KB ROM using direct altsyncram instantiation
//                     (MegaWizard-style) with init_file string literals.
//                     4 x 8KB banks, each with its own MIF initialization file.
//----------------------------------------------------------------------------

module rom_32kb (

// OUTPUTs
    rom_dout_o,                     // ROM data output

// INPUTs
    rom_addr_i,                     // ROM address
    rom_cen_i,                      // ROM chip enable (low active)
    rom_clk_i,                      // ROM clock
    rom_rst_i                       // ROM output buffer reset
);

// PARAMETERs
//============
parameter MEM_ADDRW    =  13;           // Width of the address bus
parameter ASYNC_RST_EN = 1'b1;          // Reset architecture: 1=async active-low reset (default), 0=synchronous reset

// OUTPUTs
//============
output         [31:0] rom_dout_o;    // ROM data output

// INPUTs
//============
input [MEM_ADDRW-1:0] rom_addr_i;    // ROM address
input                 rom_cen_i;     // ROM chip enable (low active)
input                 rom_clk_i;     // ROM clock
input                 rom_rst_i;     // ROM output buffer reset


// ROM INSTANCES
//================
// Direct altsyncram instantiation (MegaWizard-style) with init_file as
// string literals in each bank's defparam. Quartus requires init_file to
// be a literal in the same module as the altsyncram instantiation.

    localparam ROM_ADDRW = 11;                  // 2048 words (8KB) per bank

    // Active-high chip enables per bank (active when chip selected AND bank matches)
    wire cen_0 = rom_cen_i | (rom_addr_i[MEM_ADDRW-1:ROM_ADDRW] != 2'b00);
    wire cen_1 = rom_cen_i | (rom_addr_i[MEM_ADDRW-1:ROM_ADDRW] != 2'b01);
    wire cen_2 = rom_cen_i | (rom_addr_i[MEM_ADDRW-1:ROM_ADDRW] != 2'b10);
    wire cen_3 = rom_cen_i | (rom_addr_i[MEM_ADDRW-1:ROM_ADDRW] != 2'b11);

    wire [31:0] rom_dout_0;
    wire [31:0] rom_dout_1;
    wire [31:0] rom_dout_2;
    wire [31:0] rom_dout_3;

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
        .address_a         ( rom_addr_i[ROM_ADDRW-1:0] ),
        .byteena_a         ( 4'b1111                   ),
        .clock0            ( rom_clk_i                 ),
        .clocken0          ( ~cen_0                    ),
        .data_a            ( 32'h00000000              ),
        .wren_a            ( 1'b0                      ),
        .q_a               ( rom_dout_0                ),
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
        altsyncram_bank0.widthad_a                          = ROM_ADDRW,
        altsyncram_bank0.width_a                            = 32,
        altsyncram_bank0.width_byteena_a                    = 4;

    //--------- Bank 1 ---------
    altsyncram altsyncram_bank1 (
        .address_a         ( rom_addr_i[ROM_ADDRW-1:0] ),
        .byteena_a         ( 4'b1111                   ),
        .clock0            ( rom_clk_i                 ),
        .clocken0          ( ~cen_1                    ),
        .data_a            ( 32'h00000000              ),
        .wren_a            ( 1'b0                      ),
        .q_a               ( rom_dout_1                ),
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
        altsyncram_bank1.widthad_a                          = ROM_ADDRW,
        altsyncram_bank1.width_a                            = 32,
        altsyncram_bank1.width_byteena_a                    = 4;

    //--------- Bank 2 ---------
    altsyncram altsyncram_bank2 (
        .address_a         ( rom_addr_i[ROM_ADDRW-1:0] ),
        .byteena_a         ( 4'b1111                   ),
        .clock0            ( rom_clk_i                 ),
        .clocken0          ( ~cen_2                    ),
        .data_a            ( 32'h00000000              ),
        .wren_a            ( 1'b0                      ),
        .q_a               ( rom_dout_2                ),
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
        altsyncram_bank2.widthad_a                          = ROM_ADDRW,
        altsyncram_bank2.width_a                            = 32,
        altsyncram_bank2.width_byteena_a                    = 4;

    //--------- Bank 3 ---------
    altsyncram altsyncram_bank3 (
        .address_a         ( rom_addr_i[ROM_ADDRW-1:0] ),
        .byteena_a         ( 4'b1111                   ),
        .clock0            ( rom_clk_i                 ),
        .clocken0          ( ~cen_3                    ),
        .data_a            ( 32'h00000000              ),
        .wren_a            ( 1'b0                      ),
        .q_a               ( rom_dout_3                ),
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
        altsyncram_bank3.widthad_a                          = ROM_ADDRW,
        altsyncram_bank3.width_a                            = 32,
        altsyncram_bank3.width_byteena_a                    = 4;

    // Bank select registered to match altsyncram's internal output register.
    // Uses the shared arv_ipdff primitive so the reset architecture honors
    // ASYNC_RST_EN uniformly with the rest of the design.
    wire [MEM_ADDRW-ROM_ADDRW-1:0] rom_bank_sel;
    arv_ipdff #(.WIDTH(MEM_ADDRW-ROM_ADDRW), .ARST_EN(ASYNC_RST_EN)) u_rom_bank_sel (
        .clk_i  (rom_clk_i),
        .rst_n_i(rom_rst_i),
        .en_i   (~rom_cen_i),
        .d_i    (rom_addr_i[MEM_ADDRW-1:ROM_ADDRW]),
        .q_o    (rom_bank_sel));

    // Output mux
    reg [31:0] rom_dout_mux;
    always @(*)
        case (rom_bank_sel)
            2'b00:   rom_dout_mux = rom_dout_0;
            2'b01:   rom_dout_mux = rom_dout_1;
            2'b10:   rom_dout_mux = rom_dout_2;
            2'b11:   rom_dout_mux = rom_dout_3;
            default: rom_dout_mux = 32'h00000000;
        endcase

    assign rom_dout_o = rom_dout_mux;


endmodule // rom_32kb

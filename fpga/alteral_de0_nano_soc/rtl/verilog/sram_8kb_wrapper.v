//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    sram_8kb_wrapper
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : sram_8kb_wrapper.v
// Module Description : 8KB SRAM wrapper for Altera FPGAs using altsyncram.
//                     Matches the sram_8kb_wrapper interface used by ASIC
//                     technology libraries (32-bit, 2048 words, byte-enable,
//                     registered output).
//----------------------------------------------------------------------------

module sram_8kb_wrapper (

// OUTPUTs
    dout_o,                          // Data output

// INPUTs
    addr_i,                          // Address
    cen_i,                           // Chip enable (low active)
    clk_i,                           // Clock
    din_i,                           // Data input
    rst_i,                           // Reset (low active)
    wen_i                            // Byte write enable (low active)
);

// PARAMETERs
//============
parameter ADDRW     = 11;            // Address width: 2048 words = 8KB
parameter INIT_FILE = "UNUSED";      // Optional MIF file for memory initialization

// OUTPUTs
//============
output         [31:0] dout_o;        // Data output

// INPUTs
//============
input    [ADDRW-1:0] addr_i;         // Address
input                cen_i;          // Chip enable (low active)
input                clk_i;          // Clock
input         [31:0] din_i;          // Data input
input                rst_i;          // Reset (low active)
input          [3:0] wen_i;          // Byte write enable (low active)


// Active-high write enable and byte enables
wire       wren  = ~cen_i & ~(&wen_i);
wire [3:0] byten = ~wen_i;

// Clock enable (active when chip is selected)
wire       clken = ~cen_i;

// Lint cleanup
wire       rst_i_unused = rst_i;

// Tie-off for unused altsyncram outputs (per-instance).
wire [2:0] eccstatus_inst_unused;
wire       q_b_inst_unused;


altsyncram altsyncram_inst (
    .address_a         ( addr_i                ),
    .byteena_a         ( byten                 ),
    .clock0            ( clk_i                 ),
    .clocken0          ( clken                 ),
    .data_a            ( din_i                 ),
    .wren_a            ( wren                  ),
    .q_a               ( dout_o                ),

    // Unused ports
    .aclr0             ( 1'b0                  ),
    .aclr1             ( 1'b0                  ),
    .address_b         ( 1'b1                  ),
    .addressstall_a    ( 1'b0                  ),
    .addressstall_b    ( 1'b0                  ),
    .byteena_b         ( 1'b1                  ),
    .clock1            ( 1'b1                  ),
    .clocken1          ( 1'b1                  ),
    .clocken2          ( 1'b1                  ),
    .clocken3          ( 1'b1                  ),
    .data_b            ( 1'b1                  ),
    .eccstatus         ( eccstatus_inst_unused ),
    .q_b               ( q_b_inst_unused       ),
    .rden_a            ( 1'b1                  ),
    .rden_b            ( 1'b1                  ),
    .wren_b            ( 1'b0                  )
);

defparam
    altsyncram_inst.byte_size                  = 8,
    altsyncram_inst.clock_enable_input_a       = "NORMAL",
    altsyncram_inst.clock_enable_output_a      = "BYPASS",
    altsyncram_inst.init_file                  = INIT_FILE,
    altsyncram_inst.intended_device_family     = "Cyclone V",
    altsyncram_inst.lpm_hint                   = "ENABLE_RUNTIME_MOD=NO",
    altsyncram_inst.lpm_type                   = "altsyncram",
    altsyncram_inst.numwords_a                 = 2**ADDRW,
    altsyncram_inst.operation_mode             = "SINGLE_PORT",
    altsyncram_inst.outdata_aclr_a             = "NONE",
    altsyncram_inst.outdata_reg_a              = "UNREGISTERED",
    altsyncram_inst.power_up_uninitialized     = "FALSE",
    altsyncram_inst.read_during_write_mode_port_a = "NEW_DATA_NO_NBE_READ",
    altsyncram_inst.widthad_a                  = ADDRW,
    altsyncram_inst.width_a                    = 32,
    altsyncram_inst.width_byteena_a            = 4;


endmodule // sram_8kb_wrapper

//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    ram_32x38400
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : ram_32x38400.v
// Module Description : Belenos VRAM for Altera FPGAs (altsyncram, M10K).
//                      Single port, 38400 x 32 by default (320 x 240 at
//                      16 bpp), active-low chip enable and byte write
//                      enables, 1-cycle read latency.
//----------------------------------------------------------------------------
`default_nettype none

module  ram_32x38400 #(
    parameter               ADDRW = 16,      // address width
    parameter               WORDS = 38400    // depth in 32-bit words (<= 2^ADDRW)
) (
    input  wire             clk_i,           // clock
    input  wire             cen_i,           // chip enable (active low)
    input  wire       [3:0] wen_i,           // byte write enables (active low, all 1 = read)
    input  wire [ADDRW-1:0] addr_i,          // word address
    input  wire      [31:0] din_i,           // write data
    output wire      [31:0] dout_o           // read data
);

wire                        wren   = ~cen_i & ~(&wen_i);
wire                  [3:0] byten  = ~wen_i;
wire                        clken  = ~cen_i;

wire                  [2:0] eccstatus_unused;
wire                        q_b_unused;

altsyncram altsyncram_inst (
    .address_a         ( addr_i            ),
    .byteena_a         ( byten             ),
    .clock0            ( clk_i             ),
    .clocken0          ( clken             ),
    .data_a            ( din_i             ),
    .wren_a            ( wren              ),
    .q_a               ( dout_o            ),

    .aclr0             ( 1'b0              ),
    .aclr1             ( 1'b0              ),
    .address_b         ( 1'b1              ),
    .addressstall_a    ( 1'b0              ),
    .addressstall_b    ( 1'b0              ),
    .byteena_b         ( 1'b1              ),
    .clock1            ( 1'b1              ),
    .clocken1          ( 1'b1              ),
    .clocken2          ( 1'b1              ),
    .clocken3          ( 1'b1              ),
    .data_b            ( 1'b1              ),
    .eccstatus         ( eccstatus_unused  ),
    .q_b               ( q_b_unused        ),
    .rden_a            ( 1'b1              ),
    .rden_b            ( 1'b1              ),
    .wren_b            ( 1'b0              )
);

defparam
    altsyncram_inst.byte_size                     = 8,
    altsyncram_inst.clock_enable_input_a          = "NORMAL",
    altsyncram_inst.clock_enable_output_a         = "BYPASS",
    altsyncram_inst.init_file                     = "UNUSED",
    altsyncram_inst.intended_device_family        = "Cyclone V",
    altsyncram_inst.lpm_hint                      = "ENABLE_RUNTIME_MOD=NO",
    altsyncram_inst.lpm_type                      = "altsyncram",
    altsyncram_inst.numwords_a                    = WORDS,
    altsyncram_inst.operation_mode                = "SINGLE_PORT",
    altsyncram_inst.outdata_aclr_a                = "NONE",
    altsyncram_inst.outdata_reg_a                 = "UNREGISTERED",
    altsyncram_inst.power_up_uninitialized        = "FALSE",
    altsyncram_inst.read_during_write_mode_port_a = "NEW_DATA_NO_NBE_READ",
    altsyncram_inst.widthad_a                     = ADDRW,
    altsyncram_inst.width_a                       = 32,
    altsyncram_inst.width_byteena_a               = 4;


endmodule // ram_32x38400
`default_nettype wire

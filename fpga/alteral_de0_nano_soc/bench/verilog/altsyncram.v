//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    altsyncram
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : altsyncram.v
// Module Description : Minimal behavioral model of Altera altsyncram for
//                     simulation with iverilog. Supports SINGLE_PORT mode
//                     with byte-enable, registered output, and clock enable.
//----------------------------------------------------------------------------

module altsyncram (
    address_a,
    byteena_a,
    clock0,
    clocken0,
    data_a,
    wren_a,
    q_a,

    // Unused ports (active connections in RTL, ignored here)
    aclr0,
    aclr1,
    address_b,
    addressstall_a,
    addressstall_b,
    byteena_b,
    clock1,
    clocken1,
    clocken2,
    clocken3,
    data_b,
    eccstatus,
    q_b,
    rden_a,
    rden_b,
    wren_b
);

// Parameters (matching Quartus defparams)
parameter byte_size                          = 8;
parameter clock_enable_input_a               = "NORMAL";
parameter clock_enable_output_a              = "BYPASS";
parameter init_file                          = "UNUSED";
parameter intended_device_family             = "Cyclone V";
parameter lpm_hint                           = "ENABLE_RUNTIME_MOD=NO";
parameter lpm_type                           = "altsyncram";
parameter numwords_a                         = 2048;
parameter operation_mode                     = "SINGLE_PORT";
parameter outdata_aclr_a                     = "NONE";
parameter outdata_reg_a                      = "UNREGISTERED";
parameter power_up_uninitialized             = "FALSE";
parameter read_during_write_mode_port_a      = "NEW_DATA_NO_NBE_READ";
parameter widthad_a                          = 11;
parameter width_a                            = 32;
parameter width_byteena_a                    = 4;

// Port A
input  [widthad_a-1:0]      address_a;
input  [width_byteena_a-1:0] byteena_a;
input                        clock0;
input                        clocken0;
input  [width_a-1:0]        data_a;
input                        wren_a;
output [width_a-1:0]        q_a;

// Unused ports
input                        aclr0;
input                        aclr1;
input                        address_b;
input                        addressstall_a;
input                        addressstall_b;
input                        byteena_b;
input                        clock1;
input                        clocken1;
input                        clocken2;
input                        clocken3;
input                        data_b;
output                 [2:0] eccstatus;
output                       q_b;
input                        rden_a;
input                        rden_b;
input                        wren_b;

// Memory array
reg [width_a-1:0] mem [0:numwords_a-1];

// Output register (used when outdata_reg_a == "CLOCK0")
reg [width_a-1:0] q_a_reg;

integer idx;

initial begin
    for (idx = 0; idx < numwords_a; idx = idx + 1)
        mem[idx] = {width_a{1'b0}};
    q_a_reg = {width_a{1'b0}};
end

always @(posedge clock0) begin
    if (clocken0) begin
        // Write with byte enables
        if (wren_a) begin
            if (byteena_a[0]) mem[address_a][ 7: 0] <= data_a[ 7: 0];
            if (byteena_a[1]) mem[address_a][15: 8] <= data_a[15: 8];
            if (byteena_a[2]) mem[address_a][23:16] <= data_a[23:16];
            if (byteena_a[3]) mem[address_a][31:24] <= data_a[31:24];
        end

        // Registered read output (for CLOCK0 mode)
        q_a_reg <= mem[address_a];
    end
end

// Output: registered or combinational depending on outdata_reg_a
//assign q_a = (outdata_reg_a == "CLOCK0") ? q_a_reg : mem[address_a];
assign q_a = q_a_reg;

// Unused outputs
assign eccstatus = 3'b000;
assign q_b       = 1'b0;

//-------------------------------------------------
// Lint cleanup
//-------------------------------------------------
// Unused dual-port / optional input ports — this model implements SINGLE_PORT
// mode, so these inputs are accepted for interface compatibility but unused.
wire        aclr0_unused          = aclr0;
wire        aclr1_unused          = aclr1;
wire        address_b_unused      = address_b;
wire        addressstall_a_unused = addressstall_a;
wire        addressstall_b_unused = addressstall_b;
wire        byteena_b_unused      = byteena_b;
wire        clock1_unused         = clock1;
wire        clocken1_unused       = clocken1;
wire        clocken2_unused       = clocken2;
wire        clocken3_unused       = clocken3;
wire        data_b_unused         = data_b;
wire        rden_a_unused         = rden_a;
wire        rden_b_unused         = rden_b;
wire        wren_b_unused         = wren_b;

endmodule // altsyncram

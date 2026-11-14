//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tb_arvern_fpga
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tb_arvern_fpga.v
// Module Description : arvern FPGA testbench for DE0-Nano-SoC
//----------------------------------------------------------------------------
`include "timescale.v"

module tb_arvern_fpga;

//
// Wire & Register definition
//------------------------------

// User Clocks
reg               FPGA_CLK1_50;
reg               FPGA_CLK2_50;
reg               FPGA_CLK3_50;

// User Interface (FPGA)
reg         [1:0] KEY;
reg         [3:0] SW;
wire        [7:0] LED;

// GPIO
wire       [35:0] GPIO_0;
wire       [35:0] GPIO_1;
reg        [25:0] gpio_tb_val;      // bench drive value per GPIO pin (GPIO_1[25:0])
reg        [25:0] gpio_tb_en;       // bench drives the pin while set and the DUT does not

// Arduino Digital Interface
wire       [15:0] ARDUINO_IO;
wire              ARDUINO_RESET_N;
reg        [15:0] ard_tb_val;       // bench drive value per Arduino GPIO pin (ARDUINO_IO[15:0])
reg        [15:0] ard_tb_en;        // bench drives the pin while set and the DUT does not

// ADC
wire              ADC_CONVST;
wire              ADC_SCK;
wire              ADC_SDI;
reg               ADC_SDO;

// Testbench variables
integer           idx;
integer           error;
reg               stimulus_done;

// Temporary ROM image for loading
reg        [31:0] full_rom [0:12287];  // 48KB = 12288 x 32-bit words


//
// Include files
//------------------------------

// Verilog stimulus
`include "stimulus.v"


//
// Initialize Program Memory
//------------------------------

initial
  begin
     // Load flat memory image then distribute to 8KB banks
     // Program memory uses pmem_48kb → 6x altsyncram banks (2048 words each),
     // init-at-config AND writable.
     for (idx=0; idx < 12288; idx=idx+1) full_rom[idx] = 32'h00000000;

     #10 $readmemh("./pmem.mem", full_rom);

     for (idx=0; idx < 2048; idx=idx+1)
       begin
          dut.ahb_bus_system_inst.pmem_inst0.altsyncram_bank0.mem[idx] = full_rom[idx];
          dut.ahb_bus_system_inst.pmem_inst0.altsyncram_bank1.mem[idx] = full_rom[idx + 2048];
          dut.ahb_bus_system_inst.pmem_inst0.altsyncram_bank2.mem[idx] = full_rom[idx + 4096];
          dut.ahb_bus_system_inst.pmem_inst0.altsyncram_bank3.mem[idx] = full_rom[idx + 6144];
          dut.ahb_bus_system_inst.pmem_inst0.altsyncram_bank4.mem[idx] = full_rom[idx + 8192];
          dut.ahb_bus_system_inst.pmem_inst0.altsyncram_bank5.mem[idx] = full_rom[idx + 10240];
       end
  end


//
// Generate Clock & Reset
//------------------------------
initial
  begin
     FPGA_CLK1_50 = 1'b0;
     forever #10.0 FPGA_CLK1_50 <= ~FPGA_CLK1_50; // 50 MHz
  end

initial
  begin
     FPGA_CLK2_50 = 1'b0;
     forever #10.0 FPGA_CLK2_50 <= ~FPGA_CLK2_50; // 50 MHz
  end

initial
  begin
     FPGA_CLK3_50 = 1'b0;
     forever #10.0 FPGA_CLK3_50 <= ~FPGA_CLK3_50; // 50 MHz
  end

// Reset via KEY[0] (active low on DE0-Nano-SoC)
initial
  begin
     KEY[0]      = 1'b1;
     #100 KEY[0] = 1'b0;
     #600 KEY[0] = 1'b1;
  end


//
// Global initialization
//------------------------------
initial
  begin
     error              = 0;
     stimulus_done      = 0;

     KEY[1]             = 1'b1;     // Keys/Buttons

     SW[0]              = 1'b0;     // Switches
     SW[1]              = 1'b0;
     SW[2]              = 1'b0;
     SW[3]              = 1'b0;

     ADC_SDO            = 1'b1;     // ADC

     gpio_tb_val        = 26'h0;    // GPIO pins: released, pulled down
     gpio_tb_en         = 26'h0;
     ard_tb_val         = 16'h0;    // Arduino GPIO pins: released, pulled down
     ard_tb_en          = 16'h0;
  end


//
// GPIO pins (GPIO_1[25:0]): a pull-down on every pin and a bench driver that
// lets go of a pin while the DUT drives it
//----------------------------------
genvar gp;
generate
  for (gp = 0; gp < 26; gp = gp + 1) begin : g_gpio_pin
     pulldown pd_gpio (GPIO_1[gp]);
     assign GPIO_1[gp] = (gpio_tb_en[gp] & ~dut.gpio_oe[gp]) ? gpio_tb_val[gp] : 1'bz;
  end
  for (gp = 0; gp < 16; gp = gp + 1) begin : g_gpio_ard_pin
     pulldown pd_gpio_ard (ARDUINO_IO[gp]);
     assign ARDUINO_IO[gp] = (ard_tb_en[gp] & ~dut.gpio_ard_oe[gp]) ? ard_tb_val[gp] : 1'bz;
  end
endgenerate


//
// Virtual board (Verilator, ARV_VSCREEN): the window may drive the switches
// and keys instead of the stimulus
//----------------------------------
wire        [1:0] key_dut;
wire        [3:0] sw_dut;

`ifdef ARV_VSCREEN
vscreen u_vscreen (
     .clk_i           ( FPGA_CLK1_50    ),
     .led_i           ( LED             ),
     .key_tb_i        ( KEY             ),
     .sw_tb_i         ( SW              ),
     .key_o           ( key_dut         ),
     .sw_o            ( sw_dut          )
);
`else
assign key_dut = KEY;
assign sw_dut  = SW;
`endif


//
// arvern FPGA Instance
//----------------------------------

arvern_fpga dut (

     // USER CLOCKS
     .FPGA_CLK1_50    ( FPGA_CLK1_50    ),
     .FPGA_CLK2_50    ( FPGA_CLK2_50    ),
     .FPGA_CLK3_50    ( FPGA_CLK3_50    ),

     // USER INTERFACE (FPGA)
     .KEY             ( key_dut         ),
     .LED             ( LED             ),
     .SW              ( sw_dut          ),

     // GPIO
     .GPIO_0          ( GPIO_0          ),
     .GPIO_1          ( GPIO_1          ),

     // ARDUINO DIGITAL INTERFACE
     .ARDUINO_IO      ( ARDUINO_IO      ),
     .ARDUINO_RESET_N ( ARDUINO_RESET_N ),

     // ADC
     .ADC_CONVST      ( ADC_CONVST      ),
     .ADC_SCK         ( ADC_SCK         ),
     .ADC_SDI         ( ADC_SDI         ),
     .ADC_SDO         ( ADC_SDO         )
);


//
// Probes (CPU registers and instruction decode for waveforms/trace)
//----------------------------------

probes_instructions probes_instructions ();
probes_cpu          probes_cpu();
probes_cpu_alt      probes_cpu_alt();
probes_gpu          probes_gpu();                  // Belenos GPU trace (slipstream.log), cycles as asphalt.log


//
// Generate Waveform
//----------------------------------------
initial
  begin
   `ifdef NODUMP
   `else
     `ifdef VPD_FILE
        $vcdplusfile("tb_arvern_fpga.vpd");
        $vcdpluson();
     `else
       `ifdef TRN_FILE
          $recordfile ("tb_arvern_fpga.trn");
          $recordvars;
       `else
          $dumpfile("tb_arvern_fpga.vcd");
          $dumpvars(0, tb_arvern_fpga);
       `endif
     `endif
   `endif
  end


//
// End of simulation
//----------------------------------------

initial // Timeout
  begin
   `ifdef NO_TIMEOUT
   `else
     `ifdef TIMEOUT_MS                 // a test's own limit, in ms
       #(`TIMEOUT_MS * 64'd1000000);
     `else
     `ifdef VERY_LONG_TIMEOUT
       #500000000;
     `else
     `ifdef LONG_TIMEOUT
       #50000000;
     `else
       #5000000;
     `endif
     `endif
     `endif
       $display(" ===============================================");
       $display("|               SIMULATION FAILED               |");
       $display("|              (simulation Timeout)             |");
       $display(" ===============================================");
       $finish;
   `endif
  end

// VRAM traffic, for comparing builds: window reads that a next-word read
// buffer would serve (same word, or the next one after the previous read, no
// write to it in between), and the other requesters' grants
`define TB_VARB dut.belenos_inst.u_core.u_vram_arb
integer vs_cyc, vs_wrd, vs_wrd_same, vs_wrd_next, vs_wwr, vs_gpu_rd, vs_gpu_wr, vs_bk, vs_pb_hit, vs_pb_rd, vs_pb_wait;
reg     [23:0] vs_prev;
reg            vs_prev_v;
reg            vs_next_v;
initial begin
   vs_cyc = 0; vs_wrd = 0; vs_wrd_same = 0; vs_wrd_next = 0; vs_wwr = 0;
   vs_gpu_rd = 0; vs_gpu_wr = 0; vs_bk = 0; vs_pb_hit = 0; vs_pb_rd = 0; vs_pb_wait = 0; vs_prev_v = 1'b0; vs_next_v = 1'b0; vs_prev = 24'h0;
end
always @(posedge dut.ahb_bus_system_inst.hclk_i)
  if (dut.hresetn === 1'b1) begin
     vs_cyc = vs_cyc + 1;
     if ((`TB_VARB.gnt_o[0] === 1'b1) && (`TB_VARB.we_i[0] !== 1'b1)) begin
        vs_wrd = vs_wrd + 1;
        if (vs_prev_v && (`TB_VARB.addr_i[15:0] == vs_prev))         vs_wrd_same = vs_wrd_same + 1;
        if (vs_next_v && (`TB_VARB.addr_i[15:0] == vs_prev + 24'd1)) vs_wrd_next = vs_wrd_next + 1;
        vs_prev   = `TB_VARB.addr_i[15:0];
        vs_prev_v = 1'b1;
        vs_next_v = 1'b1;
     end
     if ((`TB_VARB.gnt_o[0] === 1'b1) && (`TB_VARB.we_i[0] === 1'b1)) vs_wwr = vs_wwr + 1;
     if ((`TB_VARB.vram_wen_o !== 4'hF) && (`TB_VARB.vram_addr_o == vs_prev))          vs_prev_v = 1'b0;
     if ((`TB_VARB.vram_wen_o !== 4'hF) && (`TB_VARB.vram_addr_o == vs_prev + 24'd1)) vs_next_v = 1'b0;
     if (dut.belenos_inst.u_core.u_ahb_vram.pb_hit === 1'b1)                vs_pb_hit = vs_pb_hit + 1;
     if (`TB_VARB.gnt_o[3] === 1'b1)                                     vs_pb_rd  = vs_pb_rd + 1;
     if (dut.belenos_inst.u_core.u_ahb_vram.wait_done === 1'b1)             vs_pb_wait = vs_pb_wait + 1;
     if ((`TB_VARB.gnt_o[1] === 1'b1) && (`TB_VARB.we_i[1] !== 1'b1)) vs_gpu_rd = vs_gpu_rd + 1;
     if ((`TB_VARB.gnt_o[1] === 1'b1) && (`TB_VARB.we_i[1] === 1'b1)) vs_gpu_wr = vs_gpu_wr + 1;
     if (`TB_VARB.gnt_o[2] === 1'b1)                                     vs_bk     = vs_bk + 1;
  end

initial // Normal end of test
  begin
     #10;
     @(posedge stimulus_done);

     $display("VRAMSTAT: cyc=%0d win_rd=%0d win_rd_same=%0d win_rd_next=%0d buf_hit=%0d buf_wait=%0d read_ahead=%0d win_wr=%0d gpu_rd=%0d gpu_wr=%0d bk=%0d",
              vs_cyc, vs_wrd, vs_wrd_same, vs_wrd_next, vs_pb_hit, vs_pb_wait, vs_pb_rd, vs_wwr, vs_gpu_rd, vs_gpu_wr, vs_bk);

`ifndef NOTRACE
     probes_instructions.trace_flush_and_close;
`endif
     probes_gpu.close;

     $display(" ===============================================");
     if (error!=0)
       begin
          $display("|               SIMULATION FAILED               |");
          $display("|     (some verilog stimulus checks failed)     |");
       end
     else
       begin
          $display("|               SIMULATION PASSED               |");
       end
     $display(" ===============================================");
     $display("");
     $finish;
  end


//
// Tasks Definition
//------------------------------

   task tb_error;
      input [65*8:0] error_string;
      begin
         $display("ERROR: %s %t", error_string, $time);
         error = error+1;
      end
   endtask

endmodule

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

// Arduino Digital Interface
wire       [15:0] ARDUINO_IO;
wire              ARDUINO_RESET_N;

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
reg        [31:0] full_rom [0:8191];   // 32KB = 8192 x 32-bit words


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
     // ROM uses rom_32kb → 4x sram_8kb_wrapper → altsyncram (2048 words each)
     for (idx=0; idx < 8192; idx=idx+1) full_rom[idx] = 32'h00000000;

     #10 $readmemh("./pmem.mem", full_rom);

     for (idx=0; idx < 2048; idx=idx+1)
       begin
          dut.ahb_bus_system_inst.rom_inst0.altsyncram_bank0.mem[idx] = full_rom[idx];
          dut.ahb_bus_system_inst.rom_inst0.altsyncram_bank1.mem[idx] = full_rom[idx + 2048];
          dut.ahb_bus_system_inst.rom_inst0.altsyncram_bank2.mem[idx] = full_rom[idx + 4096];
          dut.ahb_bus_system_inst.rom_inst0.altsyncram_bank3.mem[idx] = full_rom[idx + 6144];
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
  end


//
// arvern FPGA Instance
//----------------------------------

arvern_fpga dut (

     // USER CLOCKS
     .FPGA_CLK1_50    ( FPGA_CLK1_50    ),
     .FPGA_CLK2_50    ( FPGA_CLK2_50    ),
     .FPGA_CLK3_50    ( FPGA_CLK3_50    ),

     // USER INTERFACE (FPGA)
     .KEY             ( KEY             ),
     .LED             ( LED             ),
     .SW              ( SW              ),

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
     `ifdef VERY_LONG_TIMEOUT
       #500000000;
     `else
     `ifdef LONG_TIMEOUT
       #50000000;
     `else
       #5000000;
     `endif
     `endif
       $display(" ===============================================");
       $display("|               SIMULATION FAILED               |");
       $display("|              (simulation Timeout)             |");
       $display(" ===============================================");
       $finish;
   `endif
  end

initial // Normal end of test
  begin
     #10;
     @(posedge stimulus_done);

     probes_instructions.trace_flush_and_close;

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

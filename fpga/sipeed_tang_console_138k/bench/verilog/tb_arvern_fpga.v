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
// Module Description : arvern FPGA testbench for the Tang Console 138K
//----------------------------------------------------------------------------
`include "timescale.v"

module tb_arvern_fpga;

//
// Wire & Register definition
//------------------------------

// User clock
reg               SYS_CLK;

// User interface
reg         [1:0] KEY;
wire        [1:0] LED_PIN;          // the two SoM LEDs
wire        [7:0] LED;              // the LED register, as the firmware wrote it

// PMOD0 / PMOD1 (GPIO 0-15) and the SDRAM1 header (3.3V / GND references on
// pins 1 / 2, debug transport on pins 3-10): idle, pulled up as by the pads
wire        [7:0] PMOD0;
wire        [7:0] PMOD1;
wire       [10:1] SDRAM1;

// USB-A J15 (USB1_*) and J16 (USB2_*): the board's 15K pull-downs and one
// low-speed device model per port (usb_dev0 / usb_dev1, detached until a
// stimulus attaches it)
wire              USB1_DP;
wire              USB1_DM;
wire              USB2_DP;
wire              USB2_DM;
wire              usb_dev0_attached;
wire              usb_dev1_attached;
wire       [31:0] usb_dev0_err;
wire       [31:0] usb_dev1_err;

// RGB panel
wire              LCD_CLK;
wire              LCD_DE;
wire              LCD_HSYNC;
wire              LCD_VSYNC;
wire        [7:0] LCD_R;
wire        [7:0] LCD_G;
wire        [7:0] LCD_B;
wire              LCD_BL;

// HDMI J13 (BOARD_HDMI build: define BOARD_HDMI)
`ifdef BOARD_HDMI
localparam        TB_HDMI = 1;
`else
localparam        TB_HDMI = 0;
`endif
// Program-memory data-port wait state (define TB_PMEM_B_APH_REG=0 to remove it)
`ifdef TB_PMEM_B_APH_REG
localparam        TB_PMEM_APH = `TB_PMEM_B_APH_REG;
`else
localparam        TB_PMEM_APH = 1;
`endif
// Panel build on its own pixel clock, as the board's (define TB_LCD_PCLK_SYS:
// the panel timing on clk_sys instead)
`ifdef TB_LCD_PCLK_SYS
localparam        TB_LCD_PSEP = 0;
`else
localparam        TB_LCD_PSEP = 1;
`endif
wire        [2:0] HDMI_D_P;
wire        [2:0] HDMI_D_N;
wire              HDMI_CLK_P;
wire              HDMI_CLK_N;
tri1              HDMI_DDC_SCL;          // DDC: pulled up on the board, the test EDID behind it
tri1              HDMI_DDC_SDA;
wire              rx_pclk;
wire              rx_de;
wire              rx_hs;
wire              rx_vs;
wire        [7:0] rx_r;
wire        [7:0] rx_g;
wire        [7:0] rx_b;

// Onboard audio amplifier (no model: the pins are observed only)
wire              I2S_BCLK;
wire              I2S_LRCK;
wire              I2S_DOUT;
wire              PA_EN;

// microSD slot (no card model here: a stimulus drives SD_DAT0 and SD_DET;
// the 10K board pull-ups on CMD and DAT0, the FPGA pull-up on the detect pin)
wire              SD_CLK;
wire              SD_DAT3;
wire              SD_CMD;
wire              SD_DAT0;
wire              SD_DET;

// UART to the BL616
reg               UART_RXD;
wire              UART_TXD;

// Testbench variables
integer           idx;
integer           error;
reg               stimulus_done;


//
// Include files
//------------------------------

// Verilog stimulus
`include "stimulus.v"


//
// Generate Clock & Reset
//------------------------------
// The program memory boots pmem.mem itself (pmem_bram INIT_FILE, $readmemh).
initial
  begin
     SYS_CLK = 1'b0;
     forever #10.0 SYS_CLK <= ~SYS_CLK; // 50 MHz
  end

// Reset via KEY[0] (active low)
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

     KEY[1]             = 1'b1;     // user key released
     UART_RXD           = 1'b1;     // UART idle
  end

genvar gp;
generate
  for (gp = 0; gp < 8; gp = gp + 1) begin : g_pmod
     pullup pu_pmod0 (PMOD0[gp]);
     pullup pu_pmod1 (PMOD1[gp]);
  end
  for (gp = 1; gp <= 10; gp = gp + 1) begin : g_sdram1
     pullup pu_sdram1 (SDRAM1[gp]);
  end
endgenerate
pullup   pu_sd_cmd  (SD_CMD);
pullup   pu_sd_dat0 (SD_DAT0);
pullup   pu_sd_det  (SD_DET);
pulldown pd_usb1_dp (USB1_DP);
pulldown pd_usb1_dm (USB1_DM);
pulldown pd_usb2_dp (USB2_DP);
pulldown pd_usb2_dm (USB2_DM);


//
// RGB panel (Sipeed 4.3-inch, 480 x 272), or in the HDMI build a 720 x 480
// monitor: the TMDS lanes decoded back to RGB
//----------------------------------
localparam PANEL_W = TB_HDMI ? 720 : 480;
localparam PANEL_H = TB_HDMI ? 480 : 272;

hdmi_rx_model hdmi_rx (
     .fclk_i          ( dut.clk_ser     ),
     .d_i             ( HDMI_D_P        ),
     .clk_lane_i      ( HDMI_CLK_P      ),
     .pclk_o          ( rx_pclk         ),
     .de_o            ( rx_de           ),
     .hs_o            ( rx_hs           ),
     .vs_o            ( rx_vs           ),
     .r_o             ( rx_r            ),
     .g_o             ( rx_g            ),
     .b_o             ( rx_b            )
);

rgb_panel_model #(.W(PANEL_W), .H(PANEL_H)) panel (
     .clk_i           ( TB_HDMI ? rx_pclk : ~LCD_CLK  ),   // the Sipeed panel latches on LCD_CLK's falling edge
     .rst_n_i         ( dut.hresetn                   ),
     .de_i            ( TB_HDMI ? rx_de   : LCD_DE    ),
     .hs_n_i          ( TB_HDMI ? rx_hs   : LCD_HSYNC ),
     .vs_n_i          ( TB_HDMI ? rx_vs   : LCD_VSYNC ),
     .r_i             ( TB_HDMI ? rx_r    : LCD_R     ),
     .g_i             ( TB_HDMI ? rx_g    : LCD_G     ),
     .b_i             ( TB_HDMI ? rx_b    : LCD_B     )
);


//
// arvern FPGA Instance
//----------------------------------

assign LED = dut.periph0_led;

arvern_fpga #(.BOARD_HDMI(TB_HDMI), .PMEM_B_APH_REG(TB_PMEM_APH), .BOARD_LCD_PCLK_SEP(TB_LCD_PSEP)) dut (

     // USER CLOCK
     .SYS_CLK         ( SYS_CLK         ),

     // USER INTERFACE
     .KEY             ( KEY             ),
     .LED             ( LED_PIN         ),

     // PMOD0: GENERAL PURPOSE
     .PMOD0           ( PMOD0           ),

     // PMOD1: GENERAL PURPOSE
     .PMOD1           ( PMOD1           ),

     // AUDIO
     .I2S_BCLK        ( I2S_BCLK        ),
     .I2S_LRCK        ( I2S_LRCK        ),
     .I2S_DOUT        ( I2S_DOUT        ),
     .PA_EN           ( PA_EN           ),

     // MICROSD SLOT
     .SD_CLK          ( SD_CLK          ),
     .SD_DAT3         ( SD_DAT3         ),
     .SD_CMD          ( SD_CMD          ),
     .SD_DAT0         ( SD_DAT0         ),
     .SD_DET          ( SD_DET          ),

     // SDRAM1 HEADER: DEBUG TRANSPORT
     .SDRAM1          ( SDRAM1          ),
     // USB-A J15 / J16
     .USB1_DP         ( USB1_DP         ),
     .USB1_DM         ( USB1_DM         ),
     .USB2_DP         ( USB2_DP         ),
     .USB2_DM         ( USB2_DM         ),

     // RGB PANEL
     .LCD_CLK         ( LCD_CLK         ),
     .LCD_DE          ( LCD_DE          ),
     .LCD_HSYNC       ( LCD_HSYNC       ),
     .LCD_VSYNC       ( LCD_VSYNC       ),
     .LCD_R           ( LCD_R           ),
     .LCD_G           ( LCD_G           ),
     .LCD_B           ( LCD_B           ),
     .LCD_BL          ( LCD_BL          ),

     // HDMI J13
     .HDMI_D_P        ( HDMI_D_P        ),
     .HDMI_D_N        ( HDMI_D_N        ),
     .HDMI_CLK_P      ( HDMI_CLK_P      ),
     .HDMI_CLK_N      ( HDMI_CLK_N      ),
     .HDMI_HPD        ( 1'b1            ),
     .HDMI_PWR_SAV    (                 ),
     .HDMI_DDC_SCL    ( HDMI_DDC_SCL    ),
     .HDMI_DDC_SDA    ( HDMI_DDC_SDA    ),

     // UART TO THE BL616
     .UART_RXD        ( UART_RXD        ),
     .UART_TXD        ( UART_TXD        )
);


//
// HDMI DDC: the monitor's EDID (arvern-belenos bench model)
//----------------------------------
ddc_eeprom_model u_edid (
    .scl ( HDMI_DDC_SCL ),
    .sda ( HDMI_DDC_SDA )
);


//
// USB-A J15 and J16: one low-speed device model per port (arvern-ips
// apb_usb_host bench model, built with USB_LS_DEV_EXT_BUS: the bench resolves
// the lines). The host drives through its pads (dut.usb<n>_oe); the device
// drives while it transmits; its 1.5K pull-up (D- at low speed, D+ for a
// full-speed profile) holds the line high while nobody drives; else the
// board's pull-downs. The model checks the host on every transaction.
//----------------------------------
localparam USB_CLK_HZ  = 33854167;                                  // clk_sys (arvern_fpga CLK_SYS_HZ)
localparam USB_GRID_NS = (1000000000 + USB_CLK_HZ - 1) / USB_CLK_HZ;  // host edges on the clk_sys grid

assign USB1_DP = usb_dev0.drv_en ? usb_dev0.drv_dp :
                 (usb_dev0.pu_on &  usb_dev0.pu_fs & ~dut.usb0_oe) ? 1'b1 : 1'bz;
assign USB1_DM = usb_dev0.drv_en ? usb_dev0.drv_dm :
                 (usb_dev0.pu_on & ~usb_dev0.pu_fs & ~dut.usb0_oe) ? 1'b1 : 1'bz;
assign USB2_DP = usb_dev1.drv_en ? usb_dev1.drv_dp :
                 (usb_dev1.pu_on &  usb_dev1.pu_fs & ~dut.usb1_oe) ? 1'b1 : 1'bz;
assign USB2_DM = usb_dev1.drv_en ? usb_dev1.drv_dm :
                 (usb_dev1.pu_on & ~usb_dev1.pu_fs & ~dut.usb1_oe) ? 1'b1 : 1'bz;

usb_ls_dev #(.NAME("usb_dev0"), .SEED(1), .VERBOSE(1), .RATE_GRID_NS(USB_GRID_NS)) usb_dev0 (
    .dp          ( USB1_DP           ),
    .dm          ( USB1_DM           ),
    .host_oe     ( dut.usb0_oe       ),
    .attached    ( usb_dev0_attached ),
    .suspended   (                   ),
    .address     (                   ),
    .configured  (                   ),
    .dev_state   (                   ),
    .err_count   ( usb_dev0_err      )
);

usb_ls_dev #(.NAME("usb_dev1"), .SEED(2), .VERBOSE(1), .RATE_GRID_NS(USB_GRID_NS)) usb_dev1 (
    .dp          ( USB2_DP           ),
    .dm          ( USB2_DM           ),
    .host_oe     ( dut.usb1_oe       ),
    .attached    ( usb_dev1_attached ),
    .suspended   (                   ),
    .address     (                   ),
    .configured  (                   ),
    .dev_state   (                   ),
    .err_count   ( usb_dev1_err      )
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
        if (vs_prev_v && (`TB_VARB.addr_i[16:0] == vs_prev))         vs_wrd_same = vs_wrd_same + 1;
        if (vs_next_v && (`TB_VARB.addr_i[16:0] == vs_prev + 24'd1)) vs_wrd_next = vs_wrd_next + 1;
        vs_prev   = `TB_VARB.addr_i[16:0];
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

     if ((usb_dev0_err != 0) || (usb_dev1_err != 0))
       begin
          $display("ERROR: USB device model checks: %0d on J15, %0d on J16", usb_dev0_err, usb_dev1_err);
          if (usb_dev0_err != 0) usb_dev0.dump_summary;
          if (usb_dev1_err != 0) usb_dev1.dump_summary;
       end

     $display(" ===============================================");
     if ((error!=0) || (panel.errors!=0) || (usb_dev0_err != 0) || (usb_dev1_err != 0))
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

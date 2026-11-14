//----------------------------------------------------------------------------
// Timing constraints: Sipeed Tang Console 138K
//----------------------------------------------------------------------------

// clk_sys: the system PLL's 33.854 MHz output, hclk of the whole design (the
// panel clock LCD_CLK is clk_sys forwarded through an ODDR; the HDMI build's
// pixel and serialiser clocks are added by synthesis.tcl). This is the
// requirement: synthesis.tcl hands the placer a tighter period
// (GOWIN_SYS_PERIOD), and timing_summary.py judges the result against this one.
create_clock -name sys_clk -period 29.538 -waveform {0 14.769} [get_nets {clk_sys}]

// JTAG TCK (DTM_TYPE=0) enters on SDRAM1[4] (J10 pin 4): constrained at
// 25 MHz, asynchronous to sys_clk (the DTM crosses through 2-FF synchronisers
// and a req/ack handshake). For a cJTAG build (DTM_TYPE=3) the same pin
// carries TCKC: use a 266.667 ns period (the escape detector needs
// clk_i >= 8 x f_TCKC).
create_clock -name jtag_tck -period 40.000 -waveform {0 20.000} [get_ports {SDRAM1[4]}]
set_clock_groups -asynchronous -group [get_clocks {jtag_tck}] -group [get_clocks {sys_clk}]

// Buttons, LEDs and the panel: registers at the pads, no cycle requirement
set_false_path -from [get_ports {KEY[*]}]
set_false_path -to   [get_ports {LED[*]}]
set_false_path -to   [get_ports {LCD_*}]

// PMOD0 / PMOD1 (GPIO 0-15): inputs pass the GPIO synchronisers (the Pmod
// I2S2 SDOUT the I2S master's), outputs and output enables are register bits,
// or the I2S master's flops through the GPIO mux (BCLK a few MHz: its edges
// and the data change many cycles apart)
set_false_path -from [get_ports {PMOD0[*]}]
set_false_path -to   [get_ports {PMOD0[*]}]
set_false_path -from [get_ports {PMOD1[*]}]
set_false_path -to   [get_ports {PMOD1[*]}]

// Onboard amplifier: I2S master flops (through the Pmod-select gate) and the
// PA_EN register bit
set_false_path -to   [get_ports {I2S_*}]
set_false_path -to   [get_ports {PA_EN}]

// microSD slot: the SPI controller's SCK, chip select and CMD are flops a few
// pclk cycles apart (SCK at most pclk / 2); CMD and DAT0 in pass its
// synchronisers, sampled RX_DLY cycles after the SCK edge (a board budget, not
// an STA one); the card detect passes the board I/O debouncer
set_false_path -to   [get_ports {SD_*}]
set_false_path -from [get_ports {SD_*}]

// USB-A J15 / J16 D+/D-: inputs pass the hosts' synchronisers, outputs and
// output enables are register bits (1.5 Mb/s, edges on the clk_sys grid)
set_false_path -from [get_ports {USB1_D*}]
set_false_path -to   [get_ports {USB1_D*}]
set_false_path -from [get_ports {USB2_D*}]
set_false_path -to   [get_ports {USB2_D*}]

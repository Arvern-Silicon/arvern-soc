//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    free_run
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : free_run.v
// Module Description : arvern FPGA free-running stimulus for the virtual board
//                      (./run <app> -screen -free): any firmware runs with the
//                      LT24 panel model on GPIO_0 and no check. The simulation
//                      ends when the window is closed.
//----------------------------------------------------------------------------

// LT24 on GPIO_0 (8080-16)
wire [15:0] lt24_d_host = {GPIO_0[24], GPIO_0[23], GPIO_0[22], GPIO_0[21],
                           GPIO_0[20], GPIO_0[19], GPIO_0[18], GPIO_0[17],
                           GPIO_0[16], GPIO_0[15], GPIO_0[14], GPIO_0[13],
                           GPIO_0[5],  GPIO_0[6],  GPIO_0[7],  GPIO_0[8]};
wire [15:0] lt24_d_panel;
wire        lt24_rd_drive = (GPIO_0[25] === 1'b0) && (GPIO_0[10] === 1'b0);

assign {GPIO_0[24], GPIO_0[23], GPIO_0[22], GPIO_0[21],
        GPIO_0[20], GPIO_0[19], GPIO_0[18], GPIO_0[17],
        GPIO_0[16], GPIO_0[15], GPIO_0[14], GPIO_0[13],
        GPIO_0[5],  GPIO_0[6],  GPIO_0[7],  GPIO_0[8]} = lt24_rd_drive ? lt24_d_panel : 16'hzzzz;

ili9341_8080_model #(.W(240), .H(320), .LOG_NR(64)) lt24 (
    .reset_n_i ( GPIO_0[33]        ),
    .cs_n_i    ( GPIO_0[25]        ),
    .dc_i      ( GPIO_0[12]        ),
    .wr_n_i    ( GPIO_0[11]        ),
    .rd_n_i    ( GPIO_0[10]        ),
    .d_i       ( lt24_d_host       ),
    .d_oe_i    ( dut.lcd_d_oe      ),
    .d_o       ( lt24_d_panel      )
);

initial
   begin
      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA FREE RUN (virtual board; close it to stop)       |");
      $display(" ====================================================================");
      $display("");
   end

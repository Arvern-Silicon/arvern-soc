//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    vscreen
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : vscreen.v
// Module Description : Virtual DE0-Nano-SoC board (Verilator only, ARV_VSCREEN):
//                      the panel models draw into an SDL window (vscreen.cpp),
//                      which also shows the LEDs and, with ARV_VSCREEN_INPUT=1,
//                      lets the user drive the switches and keys. With input
//                      disabled the stimulus keeps the switches and keys and the
//                      window only shows them.
//----------------------------------------------------------------------------
`ifdef ARV_VSCREEN

`ifndef ARV_VSCREEN_INPUT
 `define ARV_VSCREEN_INPUT 1
`endif

module vscreen (
    input  wire        clk_i,      // board clock
    input  wire  [7:0] led_i,      // LEDs
    input  wire  [1:0] key_tb_i,   // keys driven by the stimulus
    input  wire  [3:0] sw_tb_i,    // switches driven by the stimulus
    output wire  [1:0] key_o,      // keys seen by the design
    output wire  [3:0] sw_o        // switches seen by the design
);

import "DPI-C" function void vscreen_init(input int input_en);
import "DPI-C" function void vscreen_tick(input int cycles, input int led, input int key_tb,
                                          input int sw_tb, output int key, output int sw,
                                          output int quit);
import "DPI-C" function void vscreen_done();

localparam INPUT_EN = `ARV_VSCREEN_INPUT;
localparam TICK     = 16384;            // cycles between window updates

integer    cnt;
reg  [1:0] key_win;
reg  [3:0] sw_win;
integer    key_v, sw_v, quit_v;

initial begin
    cnt     = 0;
    key_win = 2'b11;
    sw_win  = 4'b0000;
    vscreen_init(INPUT_EN);
end

always @(posedge clk_i) begin
    cnt = cnt + 1;
    if (cnt == TICK) begin
        cnt = 0;
        vscreen_tick(TICK, {24'h0, led_i}, {30'h0, key_tb_i}, {28'h0, sw_tb_i}, key_v, sw_v, quit_v);
        key_win = key_v[1:0];
        sw_win  = sw_v[3:0];
        if (quit_v != 0) begin
            $display("");
            $display(" virtual screen closed: simulation stopped");
            $finish;
        end
    end
end

// KEY[0] is the board reset: the window's button can only add resets to the
// stimulus' power-on pulse.
generate
    if (INPUT_EN != 0) begin : g_input
        assign key_o = {key_win[1], key_win[0] & key_tb_i[0]};
        assign sw_o  = sw_win;
    end else begin : g_view
        assign key_o = key_tb_i;
        assign sw_o  = sw_tb_i;
    end
endgenerate

// When the stimulus ends first, keep the last picture until the window closes.
final vscreen_done();

endmodule

`endif

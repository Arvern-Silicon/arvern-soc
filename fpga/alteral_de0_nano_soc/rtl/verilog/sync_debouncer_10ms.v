//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    sync_debouncer_10ms
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : sync_debouncer_10ms.v
// Module Description : Super basic 10ms debouncer. The asynchronous input is
//                      brought into the clock domain by an arv_synchronizer
//                      (2-FF), then debounced. Reset architecture (async/sync)
//                      is selectable via ASYNC_RST_EN and threaded to the
//                      arv_primitives primitives. NOTE: this module's `rst` is
//                      active-HIGH; it is inverted to the active-low convention
//                      the arv_ipdff / arv_synchronizer primitives expect.
//----------------------------------------------------------------------------

module sync_debouncer_10ms #(
    parameter ASYNC_RST_EN = 1'b1  // Reset architecture: 1=async active-low reset (default), 0=synchronous reset
) (

// OUTPUTs
    output wire signal_debounced,  // Synchronized and 10ms debounced signal

// INPUTs
    input  wire clk_50mhz,         // 50MHz clock
    input  wire rst,               // reset (active-high)
    input  wire signal_async       // Asynchonous signal
);


// Active-low reset for the arv_primitives primitives (local `rst` is active-high).
wire       rst_n = ~rst;


// Synchronize the asynchronous input into the clk_50mhz domain (2-FF).
wire signal_sync;
arv_synchronizer #(.W(1), .ARST_EN(ASYNC_RST_EN)) u_sync (
    .clk_i    (clk_50mhz),
    .rst_n_i  (rst_n),
    .async_i  (signal_async),
    .sync_o   (signal_sync));


// Debouncer (10.48ms = 0x7ffff x 50MHz clock cycles). Counter clears while the
// output already matches the synchronized input, otherwise counts up.
wire [18:0] debounce_counter;
wire [18:0] debounce_counter_nxt = (signal_debounced == signal_sync) ? 19'h00000
                                                                     : (debounce_counter + 19'd1);

arv_ipdff #(.WIDTH(19), .ARST_EN(ASYNC_RST_EN)) u_debounce_counter (
    .clk_i   (clk_50mhz),
    .rst_n_i (rst_n),
    .en_i    (1'b1),
    .d_i     (debounce_counter_nxt),
    .q_o     (debounce_counter));

// Debounce threshold (0x7ffff = 10.48ms @ 50MHz).
// Exposed as a wire so a simulation testbench can `force` for speedup
wire [18:0] debounce_threshold    = 19'h7ffff;
wire        debounce_counter_done = (debounce_counter == debounce_threshold);


// Output signal: toggles each time the debounce window completes (resets to 0).
arv_ipdff #(.WIDTH(1), .ARST_EN(ASYNC_RST_EN)) u_signal_debounced (
    .clk_i   (clk_50mhz),
    .rst_n_i (rst_n),
    .en_i    (debounce_counter_done),
    .d_i     (~signal_debounced),
    .q_o     (signal_debounced));


endmodule

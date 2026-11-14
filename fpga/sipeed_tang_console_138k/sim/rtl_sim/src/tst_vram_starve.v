//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          Module:    tst_vram_starve
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// File Name          : tst_vram_starve.v
// Module Description : arvern FPGA stimulus for software/apps/tst_vram_starve:
//                      a full-frame GPU fill alone, an unrolled CPU store loop
//                      into the VRAM window alone, then both together. The
//                      stimulus captures the tvs_* results (mtime ticks) the
//                      firmware stores in the data SRAM after its first pass
//                      and reports how much each slows the other, without and
//                      with a display refresh running.
//----------------------------------------------------------------------------

// The firmware's variable addresses (`FW_<name>, written by run from the ELF)
`include "firmware_syms.vh"

// Stores to the data SRAM: address phase, then the data of the next ready cycle
reg  [31:0] tvs_a;
reg         tvs_w;
reg  [31:0] tvs_fill_v, tvs_store_v, tvs_both_v, tvs_sib_v, tvs_filled_v;
reg  [31:0] tvs_rfill_v, tvs_rstore_v, tvs_rsib_v, tvs_rfilled_v;
reg         tvs_seen;
reg   [1:0] tvs_ph;                             // 1: stores during the fill, 2: the rest of the fill
initial begin tvs_w = 1'b0; tvs_seen = 1'b0; tvs_ph = 2'd0; end

// VRAM cycles of the second fill, per phase: who got the port, and the
// cycles the GPU asked for it in vain or did not ask while busy
`define TVS_ARB dut.belenos_inst.u_core.u_vram_arb
`define TVS_GPU dut.belenos_inst.u_core.G_GPU.u_gpu
integer tc_cyc[1:2], tc_win[1:2], tc_gpu[1:2], tc_bk[1:2], tc_idle_req[1:2], tc_lost[1:2], tc_noreq[1:2];
integer tc_i, tc_wr[1:2];
initial begin tc_wr[1] = 0; tc_wr[2] = 0; end
initial for (tc_i = 1; tc_i <= 2; tc_i = tc_i + 1) begin
   tc_cyc[tc_i] = 0; tc_win[tc_i] = 0; tc_gpu[tc_i] = 0; tc_bk[tc_i] = 0;
   tc_idle_req[tc_i] = 0; tc_lost[tc_i] = 0; tc_noreq[tc_i] = 0;
end
always @(posedge dut.ahb_bus_system_inst.hclk_i)
  if ((tvs_ph == 2'd1) || (tvs_ph == 2'd2)) begin
     tc_cyc[tvs_ph] = tc_cyc[tvs_ph] + 1;
     if (`TVS_ARB.gnt_o[0] === 1'b1) tc_win[tvs_ph] = tc_win[tvs_ph] + 1;
     if (`TVS_ARB.gnt_o[1] === 1'b1) tc_gpu[tvs_ph] = tc_gpu[tvs_ph] + 1;
     if (`TVS_ARB.gnt_o[2] === 1'b1)  tc_bk[tvs_ph]  = tc_bk[tvs_ph] + 1;
     if ((`TVS_ARB.req_i[1] === 1'b1) && (`TVS_ARB.gnt_o[1] !== 1'b1)) begin
        tc_lost[tvs_ph] = tc_lost[tvs_ph] + 1;
        if ((`TVS_ARB.gnt_o[0] !== 1'b1) && (`TVS_ARB.gnt_o[2] !== 1'b1))
           tc_idle_req[tvs_ph] = tc_idle_req[tvs_ph] + 1;
     end
     if ((dut.belenos_inst.bdi_px_valid_o === 1'b1) && (dut.belenos_inst.px_ready === 1'b1))
        tc_wr[tvs_ph] = tc_wr[tvs_ph] + 1;                  // a pixel taken by the panel driver
     if ((`TVS_GPU.prim_busy === 1'b1) && (`TVS_ARB.req_i[1] !== 1'b1))
        tc_noreq[tvs_ph] = tc_noreq[tvs_ph] + 1;
  end

always @(posedge dut.ahb_bus_system_inst.hclk_i)
  if (dut.ahb_bus_system_inst.s_sram_nx_hready) begin
     if (tvs_w)
        case (tvs_a)
          `FW_tvs_fill: tvs_fill_v   = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
          `FW_tvs_store: begin
                       tvs_store_v = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
                       if (dut.ahb_bus_system_inst.s_sram_nx_hwdata != 32'h0) tvs_ph = 2'd1;
                    end
          `FW_tvs_store_in_both: begin
                       tvs_sib_v   = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
                       if (tvs_ph == 2'd1) tvs_ph = 2'd2;
                    end
          `FW_tvs_both: begin
                       tvs_both_v  = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
                       if (tvs_ph == 2'd2) tvs_ph = 2'd3;
                    end
          `FW_tvs_filled: tvs_filled_v = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
          `FW_tvs_rfill: tvs_rfill_v = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
          `FW_tvs_rstore: tvs_rstore_v = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
          `FW_tvs_rsib: tvs_rsib_v = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
          `FW_tvs_rfilled: tvs_rfilled_v = dut.ahb_bus_system_inst.s_sram_nx_hwdata;
          `FW_tvs_done: tvs_seen     = (dut.ahb_bus_system_inst.s_sram_nx_hwdata != 32'h0);   // tvs_done++ (not the start-up clear)
          default: ;
        endcase
     tvs_w = dut.ahb_bus_system_inst.s_sram_nx_hsel & dut.ahb_bus_system_inst.s_sram_nx_htrans[1] &
             dut.ahb_bus_system_inst.s_sram_nx_hwrite;
     tvs_a = dut.ahb_bus_system_inst.s_sram_nx_haddr;
  end

initial
   begin
      $display("");
      $display(" ====================================================================");
      $display("|        arvern FPGA VRAM-window stores against a GPU fill            |");
      $display(" ====================================================================");
      $display("");
      wait (tvs_seen === 1'b1);
      $display("  GPU fill alone            : %0d ticks", tvs_fill_v);
      $display("  CPU store loop alone      : %0d ticks", tvs_store_v);
      $display("  stores while the GPU fills: %0d ticks", tvs_sib_v);
      $display("  fill + stores, until done : %0d ticks", tvs_both_v);
      $display("  fill words when the stores end: %0d", tvs_filled_v);
      for (tc_i = 1; tc_i <= 2; tc_i = tc_i + 1)
         $display("  %0s: %0d cycles; grants window %0d, GPU %0d, refresh %0d; GPU asking in vain %0d (port idle %0d); GPU busy, not asking %0d",
                  (tc_i == 1) ? "stores + fill" : "rest of fill ", tc_cyc[tc_i], tc_win[tc_i], tc_gpu[tc_i], tc_bk[tc_i],
                  tc_lost[tc_i], tc_idle_req[tc_i], tc_noreq[tc_i]);
      for (tc_i = 1; tc_i <= 2; tc_i = tc_i + 1)
         $display("  panel data writes in phase %0d: %0d", tc_i, tc_wr[tc_i]);
      $display("  with a refresh running: fill alone %0d, stores alone %0d, stores while the GPU fills %0d ticks, fill words then %0d",
               tvs_rfill_v, tvs_rstore_v, tvs_rsib_v, tvs_rfilled_v);
      // Stores slowed against the stores alone without a refresh; the fill's
      // progress during the stores against the fill alone without a refresh
      $display("  no refresh  : stores slowed by %0.1f %%, the fill advances at %0.1f %% of its speed",
               100.0 * tvs_sib_v / tvs_store_v - 100.0, 100.0 * tvs_filled_v * tvs_fill_v / (19200.0 * tvs_sib_v));
      $display("  with refresh: stores slowed by %0.1f %%, the fill advances at %0.1f %% of its speed",
               100.0 * tvs_rsib_v / tvs_store_v - 100.0, 100.0 * tvs_rfilled_v * tvs_fill_v / (19200.0 * tvs_rsib_v));
      if ((tvs_fill_v == 0) || (tvs_store_v == 0) || (tvs_both_v == 0) ||
          (tvs_rfill_v == 0) || (tvs_rstore_v == 0) || (tvs_rsib_v == 0))
         tb_error("tst_vram_starve: a measurement is zero");
      $display("");
      stimulus_done = 1;
   end

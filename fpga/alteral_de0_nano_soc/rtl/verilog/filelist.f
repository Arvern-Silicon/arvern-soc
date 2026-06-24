//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      filelist.f
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Design-only RTL source list for the DE0-Nano-SoC FPGA top (single source of
// truth). Mirrors the IP convention (rtl/verilog/filelist.f). The CPU and the
// IPs are pulled via their own -f filelists so shared primitives (arv_dff,
// arv_ipdff, arv_synchronizer, ...) are always included and never drift.
// Contains NO testbench and NO simulation-only models (the Altera altsyncram
// behavioural model lives in bench/verilog/submit.f); synthesis must not see it
// (Quartus provides altsyncram natively).
//----------------------------------------------------------------------------

//=============================================================================
// FPGA-specific RTL (top, board peripheral, debouncer, bus glue, on-chip mem)
//=============================================================================

arvern_fpga.v
ahb_led_key_sw.v
sram_8kb_wrapper.v
sync_debouncer_10ms.v
ahb_bus_system.v
ahb_arbiter.v
ahb_decoder.v
rom_32kb.v
sram_32kb.v

//=============================================================================
// arvern CPU core
//=============================================================================

+incdir+../../../../../arvern/rtl/verilog/
-f ../../../../../arvern/rtl/verilog/filelist.f

//=============================================================================
// AHB interconnect
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_interconnect/rtl/verilog/
-f ../../../../../arvern-ips/ahb_interconnect/rtl/verilog/filelist.f

//=============================================================================
// AHB peripherals (SRAM controllers, LED-KEY-SW register example)
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_sram_controller/rtl/verilog/
-f ../../../../../arvern-ips/ahb_sram_controller/rtl/verilog/filelist.f

+incdir+../../../../../arvern-ips/ahb_periph_example/rtl/verilog/
-f ../../../../../arvern-ips/ahb_periph_example/rtl/verilog/filelist.f

//=============================================================================
// ACLINT (pulls arv_common: arv_ipdff + arv_synchronizer)
//=============================================================================

+incdir+../../../../../arvern-ips/arv_common/rtl/verilog/
+incdir+../../../../../arvern-ips/ahb_aclint/rtl/verilog/
-f ../../../../../arvern-ips/ahb_aclint/rtl/verilog/filelist.f

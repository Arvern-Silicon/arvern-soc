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
// Design RTL source list for the chip_example ASIC top (single source of
// truth). The CPU and the IPs are pulled via their own -f filelists so shared
// primitives (arv_dff, arv_ipdff, arv_synchronizer, ...) are always included
// and never drift. Consumed by the synthesis flow through flatten_filelist.py
// (run_syn -> --format tcl -> submit_syn.tcl, sourced by read.tcl). The ASIC
// memory macro wrapper is supplied separately by the synthesis library setup
// ($SRAM_VERILOG_WRAPPER), not listed here.
//----------------------------------------------------------------------------

//=============================================================================
// Local SoC RTL (chip_example top, bus glue, on-chip memories)
//=============================================================================

ahb_bus_system.v
ahb_arbiter.v
ahb_decoder.v
rom_32kb.v
sram_32kb.v
chip_example.v

//=============================================================================
// arvern CPU core
//=============================================================================

+incdir+../../../../../arvern/rtl/verilog/
-f ../../../../../arvern/rtl/verilog/filelist.f

//=============================================================================
// Debug Transport Module (JTAG DTM) - instantiated when DEBUG_EN=1
//=============================================================================

+incdir+../../../../../arvern-ips/arv_dtm/rtl/verilog/
-f ../../../../../arvern-ips/arv_dtm/rtl/verilog/filelist.f

//=============================================================================
// AHB interconnect
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_interconnect/rtl/verilog/
-f ../../../../../arvern-ips/ahb_interconnect/rtl/verilog/filelist.f

//=============================================================================
// AHB peripherals (ROM / SRAM controllers, register example, custom CSR)
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_rom_controller/rtl/verilog/
-f ../../../../../arvern-ips/ahb_rom_controller/rtl/verilog/filelist.f

+incdir+../../../../../arvern-ips/ahb_sram_controller/rtl/verilog/
-f ../../../../../arvern-ips/ahb_sram_controller/rtl/verilog/filelist.f

+incdir+../../../../../arvern-ips/ahb_periph_example/rtl/verilog/
-f ../../../../../arvern-ips/ahb_periph_example/rtl/verilog/filelist.f

+incdir+../../../../../arvern-ips/arv_custom_csr/rtl/verilog/
-f ../../../../../arvern-ips/arv_custom_csr/rtl/verilog/filelist.f

//=============================================================================
// ACLINT
//=============================================================================

+incdir+../../../../../arvern-ips/arv_primitives/rtl/verilog/
+incdir+../../../../../arvern-ips/ahb_aclint/rtl/verilog/
-f ../../../../../arvern-ips/ahb_aclint/rtl/verilog/filelist.f

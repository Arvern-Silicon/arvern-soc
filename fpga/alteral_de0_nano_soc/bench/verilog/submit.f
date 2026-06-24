//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      submit.f
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Simulation submit file (testbench + RTL sources), mirroring the IP
// convention (bench/verilog/submit.f). Pulls the design-only RTL source of
// truth (rtl/verilog/filelist.f) and adds the testbench, the CPU trace probes,
// and the Altera altsyncram behavioural model (simulation only).
//----------------------------------------------------------------------------

//=============================================================================
// Testbench related (simulation only)
//=============================================================================

+incdir+.
+define+ARV_CPU_INST=dut.dut
+define+LONG_TIMEOUT
altsyncram.v
../../../../../arvern/bench/verilog/probes_cpu.v
../../../../../arvern/bench/verilog/probes_instructions.v
tb_arvern_fpga.v

//=============================================================================
// Design RTL (single source of truth)
//=============================================================================

-f ../../rtl/verilog/filelist.f

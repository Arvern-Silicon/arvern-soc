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
// the Gowin primitive behavioural models and the device models on the board's
// connectors (simulation only).
//----------------------------------------------------------------------------

//=============================================================================
// Testbench related (simulation only)
//=============================================================================

+incdir+.
+incdir+../../../../../arvern-belenos/bench/verilog
+incdir+../../../../../arvern-ips/apb_spi/bench/verilog
+define+ARV_CPU_INST=dut.dut
+define+VERY_LONG_TIMEOUT
+define+BLNS_INST=dut.belenos_inst
+define+BLNS_TRACE_CYCLE=probes_instructions.trace_cycle
+define+USB_LS_DEV_EXT_BUS
../../../../../arvern/bench/verilog/probes_cpu.v
../../../../../arvern/bench/verilog/probes_instructions.v
../../../../../arvern-belenos/bench/verilog/probes_gpu.v
../../../../../arvern-belenos/bench/verilog/ddc_eeprom_model.v
../../../../../arvern-ips/apb_usb_host/bench/verilog/usb_ls_dev.v
rgb_panel_model.v
PLL.v
ODDR.v
OSER10.v
CLKDIV.v
ELVDS_OBUF.v
hdmi_rx_model.v
tb_arvern_fpga.v

//=============================================================================
// Design RTL (single source of truth)
//=============================================================================

-f ../../rtl/verilog/filelist.f

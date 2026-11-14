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
apb_led_key_sw.v
sram_8kb_wrapper.v
sync_debouncer_10ms.v
ahb_bus_system.v
ahb_arbiter.v
ahb_decoder.v
pmem_48kb.v
sram_48kb.v

//=============================================================================
// Altera megafunction wrappers: ALTIOBUF bidirectional pad buffer and the
// Belenos VRAM / LUT memories (altsyncram). The real cyclonev_io_ibuf/obuf and
// altsyncram are provided by Quartus at synthesis; the simulation-only
// behavioural models live in bench/verilog/.
//=============================================================================

mega/io_buf.v
mega/ram_32x38400.v
mega/ram_24x512.v

//=============================================================================
// arvern CPU core
//=============================================================================

+incdir+../../../../../arvern/rtl/verilog/
-f ../../../../../arvern/rtl/verilog/filelist.f

//=============================================================================
// Debug Transport Module (selectable JTAG/UART/I2C wrapper) - DEBUG_EN=1 only
//=============================================================================

+incdir+../../../../../arvern-ips/arv_dtm/rtl/verilog/
-f ../../../../../arvern-ips/arv_dtm/rtl/verilog/filelist.f

//=============================================================================
// AHB interconnect
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_interconnect/rtl/verilog/
-f ../../../../../arvern-ips/ahb_interconnect/rtl/verilog/filelist.f

//=============================================================================
// AHB peripherals (SRAM controllers)
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_sram_controller/rtl/verilog/
-f ../../../../../arvern-ips/ahb_sram_controller/rtl/verilog/filelist.f

//=============================================================================
// ACLINT (pulls arv_primitives: arv_ipdff + arv_synchronizer)
//=============================================================================

+incdir+../../../../../arvern-ips/arv_primitives/rtl/verilog/
+incdir+../../../../../arvern-ips/ahb_aclint/rtl/verilog/
-f ../../../../../arvern-ips/ahb_aclint/rtl/verilog/filelist.f

//=============================================================================
// PLIC and GPIO
//=============================================================================

+incdir+../../../../../arvern-ips/ahb_plic/rtl/verilog/
-f ../../../../../arvern-ips/ahb_plic/rtl/verilog/filelist.f

+incdir+../../../../../arvern-ips/ahb_apb_bridge/rtl/verilog/
-f ../../../../../arvern-ips/ahb_apb_bridge/rtl/verilog/filelist.f
+incdir+../../../../../arvern-ips/apb_gpio/rtl/verilog/
-f ../../../../../arvern-ips/apb_gpio/rtl/verilog/filelist.f

//=============================================================================
// Belenos graphic controller (sibling repository arvern-belenos)
//=============================================================================

+incdir+../../../../../arvern-belenos/rtl/verilog/
-f ../../../../../arvern-belenos/rtl/verilog/filelist.f

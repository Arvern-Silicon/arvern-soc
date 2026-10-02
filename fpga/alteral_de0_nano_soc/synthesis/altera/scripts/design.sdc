#**************************************************************
# This .sdc file is created by Terasic Tool.
# Users are recommended to modify this file to match users logic.
#**************************************************************

# Read DTM_TYPE from the RTL so the JTAG timing constraints below only apply to
# the JTAG build (mirrors the proc in synthesis.tcl). cwd during fit/STA is
# .../WORK, so the top-level RTL is three levels up.
proc verilog_param {file name default} {
    if {[catch {open $file r} fh]} {
        post_message -type warning "design.sdc: cannot open $file - using $name=$default"
        return $default
    }
    set txt [read $fh]
    close $fh
    set pat "parameter\\y\[^;/=\]*\\y${name}\\y\\s*=\\s*(\[0-9\]+)"
    if {[regexp -line $pat $txt -> val]} { return $val }
    post_message -type warning "design.sdc: $name not found in $file - using $default"
    return $default
}
set DTM_TYPE [verilog_param ../../../rtl/verilog/arvern_fpga.v DTM_TYPE 0]
set CJTAG_ON [expr {$DTM_TYPE == 3}]

#**************************************************************
# Create Clock
#**************************************************************
create_clock -period "50.0 MHz" [get_ports FPGA_CLK1_50]
create_clock -period "50.0 MHz" [get_ports FPGA_CLK2_50]
create_clock -period "50.0 MHz" [get_ports FPGA_CLK3_50]

# JTAG TCK (DTM_TYPE=0 only) is a real clock, entering on GPIO_1[0]. The FT232H
# MPSSE drives it up to ~30 MHz (aRVern default 1 MHz); constrain at 25 MHz for
# headroom - the TAP logic closes trivially. Shorten the period if you clock faster.
# Placed before derive_clock_uncertainty so TCK gets clock uncertainty too.
if {$DTM_TYPE == 0} {
    create_clock -name jtag_tck -period 40.0 [get_ports {GPIO_1[0]}]
}

# cJTAG TCKC (DTM_TYPE=3 only) is a real clock, entering on GPIO_1[10]. The
# ceiling is NOT the probe's capability but the DTM's escape detector, which
# oversamples TMSC on clk_i and needs f_clk >= 8 x f_TCKC. With clk_i = 50 MHz
# that caps TCKC at 6.25 MHz (160 ns) -- do not shorten this period without
# raising clk_i. The scan engine itself is TCKC-clocked and has no ratio limit.
if {$CJTAG_ON} {
    create_clock -name cjtag_tckc -period 160.0 [get_ports {GPIO_1[10]}]
}

#**************************************************************
# Create Generated Clock
#**************************************************************
derive_pll_clocks


#**************************************************************
# Set Clock Latency
#**************************************************************


#**************************************************************
# Set Clock Uncertainty
#**************************************************************
derive_clock_uncertainty


#**************************************************************
# Set Input Delay
#**************************************************************


#**************************************************************
# Set Output Delay
#**************************************************************


#**************************************************************
# Set Clock Groups
#**************************************************************
# TCK is asynchronous to the 50 MHz system/PLL clocks. The DTM crosses between
# them only through 2-FF synchronizers + a quasi-static req/ack handshake
# (arv_dtm_dmi_master), so cut the domains - otherwise STA would falsely time
# the crossing paths. The second group is "every other clock" so any PLL-derived
# clocks are covered automatically.
if {$DTM_TYPE == 0} {
    set_clock_groups -asynchronous \
        -group [get_clocks jtag_tck] \
        -group [remove_from_collection [all_clocks] [get_clocks jtag_tck]]
}

# Same for cJTAG: TCKC is asynchronous to the 50 MHz system/PLL clocks, crossed
# only by the arv_dtm_dmi_master handshake and the escape-detector 2-FF path.
# v_tck (the recovered TAP clock, TCKC gated by arv_cgate) is propagated from
# TCKC by TimeQuest and is therefore covered by this group.
if {$CJTAG_ON} {
    set_clock_groups -asynchronous \
        -group [get_clocks cjtag_tckc] \
        -group [remove_from_collection [all_clocks] [get_clocks cjtag_tckc]]
}


#**************************************************************
# Set False Path
#**************************************************************


#**************************************************************
# Set Multicycle Path
#**************************************************************


#**************************************************************
# Set Maximum Delay
#**************************************************************


#**************************************************************
# Set Minimum Delay
#**************************************************************


#**************************************************************
# Set Input Transition
#**************************************************************


#**************************************************************
# Set Load
#**************************************************************

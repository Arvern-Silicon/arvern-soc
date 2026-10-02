##############################################################################
#                                                                            #
#                            CLOCK DEFINITION                                #
#                                                                            #
##############################################################################

# Clock period can be set by the library setup file (setup_*.tcl).
# If not already defined, use the default value below.
if {![info exists CLOCK_PERIOD]} {
    #set CLOCK_PERIOD 100.0; #  10 MHz
    #set CLOCK_PERIOD 66.6; #  15 MHz
    #set CLOCK_PERIOD 50.0; #  20 MHz
    #set CLOCK_PERIOD 40.0; #  25 MHz
    #set CLOCK_PERIOD 33.3; #  30 MHz
    #set CLOCK_PERIOD 30.0; #  33 MHz
    #set CLOCK_PERIOD 25.0; #  40 MHz
    #set CLOCK_PERIOD 22.2; #  45 MHz
    #set CLOCK_PERIOD 20.0; #  50 MHz
    #set CLOCK_PERIOD 16.7; #  60 MHz
    #set CLOCK_PERIOD 15.4; #  65 MHz
    #set CLOCK_PERIOD 15.0; #  66 MHz
    #set CLOCK_PERIOD 14.3; #  70 MHz
    #set CLOCK_PERIOD 14.0; #  71 MHz
    #set CLOCK_PERIOD 12.5; #  80 MHz
    #set CLOCK_PERIOD 11.0; #  91 MHz
    set CLOCK_PERIOD 10.0; # 100 MHz
    #set CLOCK_PERIOD  8.0; # 125 MHz
}


create_clock -name     "free_clk"                 \
             -period   "$CLOCK_PERIOD"            \
             -waveform "0 [expr $CLOCK_PERIOD/2]" \
             [get_ports free_clk_i]

# ACLINT always-on low-frequency clock (e.g. 32.768 kHz watch crystal).
# Asynchronous to free_clk. The ACLINT contains the crossing between the two
# domains, but it is NOT a synchronizer-based one: the 64-bit MTIME (clk_lf
# flops) is captured into the hclk mirror on a tick a bounded number of hclk
# after the clk_lf edge, and MTIMECMP / the load request go the other way the
# same fashion. Those paths are ordinary timed paths with a budget, so clk_lf
# is deliberately NOT put in an asynchronous clock group with free_clk -- that
# would false-path the crossing and hide a route longer than the budget.
# The budgets are the ones of the IP's own SDC (derived in
# arvern-ips/ahb_aclint/rtl/verilog/aclint_lf_tick.v), applied below on
# register collections. Only meaningful with LF_SYNC_EN=0: with LF_SYNC_EN=1
# nothing is clocked by clk_lf and both collections are empty.
if {![info exists CLOCK_LF_PERIOD]} {
    set CLOCK_LF_PERIOD 30517.6; # 32.768 kHz
}

create_clock -name     "clk_lf"                      \
             -period   "$CLOCK_LF_PERIOD"            \
             -waveform "0 [expr $CLOCK_LF_PERIOD/2]" \
             [get_ports clk_lf_i]

# JTAG test clock (external debug). Clocks the arv_dtm_jtag TAP/shift registers;
if {![info exists TCK_PERIOD]} {
    set TCK_PERIOD 100.0; # 10 MHz
}

create_clock -name     "jtag_tck"                 \
             -period   "$TCK_PERIOD"              \
             -waveform "0 [expr $TCK_PERIOD/2]"   \
             [get_ports jtag_tck_i]

set_clock_groups -asynchronous              \
    -group [get_clocks {free_clk clk_lf}]   \
    -group [get_clocks jtag_tck]

# ACLINT clk_lf <-> hclk crossing budgets (see the note at the clk_lf clock).
# Written -from/-to REGISTER COLLECTIONS, not clocks: at a ~3000:1 period ratio
# a clock-based exception exceeds DC's clock-expansion limit and is silently
# dropped, leaving the endpoints unconstrained while the run reports MET.
# The hclk side is restricted to the ACLINT instance: it is the only place a
# clk_lf path can start or end, and naming every free_clk register in the chip
# would only produce a TIM-179 warning for each one synthesis later removes.
set LF_REGS   [all_registers -clock clk_lf]
set HCLK_REGS [filter_collection [all_registers -clock free_clk] "full_name =~ *ahb_aclint_inst*"]
if {[sizeof_collection $LF_REGS] > 0} {
    # clk_lf -> hclk: launched on the clk_lf edge, captured on the tick >= 3 hclk later.
    set_max_delay [expr 3 * $CLOCK_PERIOD] \
                  -from $LF_REGS -to $HCLK_REGS
    # hclk -> clk_lf: launched on the tick <= 5 hclk after the clk_lf edge,
    # sampled by the next clk_lf edge. Five, not four: the first hclk edge after
    # a clk_lf edge may land in the synchroniser's setup window, so its first
    # stage can resolve to the old value and the capture slips one cycle,
    # carrying the tick with it. Both budgets are derived in the IP's
    # aclint_lf_tick.v ("tick timing and the two budgets") and this file mirrors
    # the IP's own constraints.tcl -- keep the two in step.
    set_max_delay [expr $CLOCK_LF_PERIOD - 5 * $CLOCK_PERIOD] \
                  -from $HCLK_REGS -to $LF_REGS
}


##############################################################################
#                                                                            #
#                          CREATE PATH GROUPS                                #
#                                                                            #
##############################################################################

# Exclude the clock ports (free_clk_i, clk_lf_i, jtag_tck_i) from the data-path
# from-groups so a clock is never treated as a data source.
group_path -name REGOUT      -to   [all_outputs]
group_path -name REGIN       -from [remove_from_collection [all_inputs] [get_ports "free_clk_i clk_lf_i jtag_tck_i"]]
group_path -name FEEDTHROUGH -from [remove_from_collection [all_inputs] [get_ports "free_clk_i clk_lf_i jtag_tck_i"]] -to [all_outputs]


##############################################################################
#                                                                            #
#                          BOUNDARY TIMINGS                                  #
#                                                                            #
##############################################################################

#=========================#
# PERIPHERAL REGISTERS    #
#=========================#

# Inputs (read-only registers driven from outside)
set PERIPH_IN_DLY    [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_08_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_08_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_09_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_09_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_10_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_10_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_11_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_11_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_12_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_12_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_13_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_13_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_14_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_14_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph0_reg_15_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_15_i]

set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_08_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_08_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_09_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_09_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_10_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_10_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_11_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_11_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_12_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_12_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_13_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_13_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_14_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_14_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph1_reg_15_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_15_i]

set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_08_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_08_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_09_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_09_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_10_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_10_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_11_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_11_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_12_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_12_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_13_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_13_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_14_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_14_i]
set_input_delay $PERIPH_IN_DLY  -max -clock "free_clk"  [get_ports periph2_reg_15_i]
set_input_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_15_i]

# Outputs (read-write registers visible outside)
set PERIPH_OUT_DLY   [expr ($CLOCK_PERIOD/100) * 70]

set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_00_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_00_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_01_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_01_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_02_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_02_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_03_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_03_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_04_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_04_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_05_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_05_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_06_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_06_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph0_reg_07_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph0_reg_07_o]

set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_00_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_00_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_01_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_01_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_02_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_02_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_03_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_03_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_04_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_04_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_05_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_05_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_06_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_06_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph1_reg_07_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph1_reg_07_o]

set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_00_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_00_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_01_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_01_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_02_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_02_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_03_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_03_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_04_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_04_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_05_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_05_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_06_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_06_o]
set_output_delay $PERIPH_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports periph2_reg_07_o]
set_output_delay 0               -min -clock "free_clk"  [get_ports periph2_reg_07_o]


#=========================#
# CCSR REGISTER VALUES    #
#=========================#

# Inputs (read-only values sampled by CCSR block)
set CCSR_RO_DLY      [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $CCSR_RO_DLY   -max -clock "free_clk"  [get_ports ccsr_usr_ro_0_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports ccsr_usr_ro_0_i]
set_input_delay $CCSR_RO_DLY   -max -clock "free_clk"  [get_ports ccsr_sup_ro_0_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports ccsr_sup_ro_0_i]
set_input_delay $CCSR_RO_DLY   -max -clock "free_clk"  [get_ports ccsr_mac_ro_0_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports ccsr_mac_ro_0_i]
set_input_delay $CCSR_RO_DLY   -max -clock "free_clk"  [get_ports ccsr_mac_ro_1_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports ccsr_mac_ro_1_i]

# Outputs (read-write values driven by CCSR block)
set CCSR_RW_DLY      [expr ($CLOCK_PERIOD/100) * 70]

set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_usr_rw_0_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_usr_rw_0_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_usr_rw_1_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_usr_rw_1_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_sup_rw_0_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_sup_rw_0_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_sup_rw_1_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_sup_rw_1_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_0_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_0_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_1_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_1_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_2_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_2_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_3_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_3_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_4_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_4_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_5_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_5_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_6_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_6_o]
set_output_delay $CCSR_RW_DLY  -add_delay -max -clock "free_clk"  [get_ports ccsr_mac_rw_7_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports ccsr_mac_rw_7_o]


#=========================#
# INTERRUPT INPUTS        #
#=========================#

set IRQ_DLY          [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $IRQ_DLY       -max -clock "free_clk"  [get_ports irq_m_external_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports irq_m_external_i]

set_input_delay $IRQ_DLY       -max -clock "free_clk"  [get_ports irq_s_external_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports irq_s_external_i]

set_input_delay $IRQ_DLY       -max -clock "free_clk"  [get_ports irq_platform_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports irq_platform_i]


#=========================#
# NMI                     #
#=========================#

set NMI_DLY          [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $NMI_DLY       -max -clock "free_clk"  [get_ports nmi_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports nmi_i]



#=========================#
# LOCKUP STATUS           #
#=========================#

set LOCKUP_DLY       [expr ($CLOCK_PERIOD/100) * 70]

set_output_delay $LOCKUP_DLY   -add_delay -max -clock "free_clk"  [get_ports lockup_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports lockup_o]


#=================================#
# HPM PLATFORM EVENTS (ZIHPM)     #
#=================================#

set HPM_EVT_DLY      [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $HPM_EVT_DLY   -max -clock "free_clk"  [get_ports hpm_platform_events_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports hpm_platform_events_i]


#=========================#
# EXTERNAL DEBUG (JTAG)    #
#=========================#

# JTAG PHY pins, timed against the TCK domain (loose: TCK runs far below core).
# TMS/TDI are sampled on the RISING edge and are driven by the debugger on the
# FALLING edge (IEEE 1149.1) -> -clock_fall on the input delay. TDO/TDO_OE are
# launched by the DTM on the FALLING edge (arv_ipdff CLK_NEGEDGE, already in the
# RTL), so a default rising-referenced output delay models negedge-launch to the
# debugger's next-rising capture.
set JTAG_IN_DLY   [expr ($TCK_PERIOD/100) * 20]
set JTAG_OUT_DLY  [expr ($TCK_PERIOD/100) * 20]

set_input_delay $JTAG_IN_DLY  -max -clock "jtag_tck" -clock_fall [get_ports jtag_tms_i]
set_input_delay 0             -min -clock "jtag_tck" -clock_fall [get_ports jtag_tms_i]
set_input_delay $JTAG_IN_DLY  -max -clock "jtag_tck" -clock_fall [get_ports jtag_tdi_i]
set_input_delay 0             -min -clock "jtag_tck" -clock_fall [get_ports jtag_tdi_i]

set_output_delay $JTAG_OUT_DLY -add_delay -max -clock "jtag_tck" [get_ports jtag_tdo_o]
set_output_delay 0                        -min -clock "jtag_tck" [get_ports jtag_tdo_o]
set_output_delay $JTAG_OUT_DLY -add_delay -max -clock "jtag_tck" [get_ports jtag_tdo_oe_o]
set_output_delay 0                        -min -clock "jtag_tck" [get_ports jtag_tdo_oe_o]


#=========================#
# DEBUG STATUS OUTPUTS    #
#=========================#

set DBG_OUT_DLY   [expr ($CLOCK_PERIOD/100) * 50]

set_output_delay $DBG_OUT_DLY -add_delay -max -clock "free_clk" [get_ports dbg_debug_mode_o]
set_output_delay 0                       -min -clock "free_clk" [get_ports dbg_debug_mode_o]
set_output_delay $DBG_OUT_DLY -add_delay -max -clock "free_clk" [get_ports dbg_halted_o]
set_output_delay 0                       -min -clock "free_clk" [get_ports dbg_halted_o]
set_output_delay $DBG_OUT_DLY -add_delay -max -clock "free_clk" [get_ports dbg_stoptime_o]
set_output_delay 0                       -min -clock "free_clk" [get_ports dbg_stoptime_o]
set_output_delay $DBG_OUT_DLY -add_delay -max -clock "free_clk" [get_ports dbg_ndmreset_o]
set_output_delay 0                       -min -clock "free_clk" [get_ports dbg_ndmreset_o]


#========================#
# FEEDTHROUGH EXCEPTIONS #
#========================#

#set_max_delay [expr 2.0 + $DMEM_DOUT_DLY + $DMEM_ADDR_DLY] \
#              -from       [get_ports dmem_dout]            \
#              -to         [get_ports dmem_addr]            \
#              -group_path FEEDTHROUGH


#===============#
# FALSE PATHS   #
#===============#

# Asynchronous resets: cut the release-recovery/removal timing to the fabric.
set_false_path -from [get_ports hresetn_i]      ; # free_clk-domain async reset
set_false_path -from [get_ports scan_mode_i] ; # static test-mode control
set_false_path -from [get_ports resetn_lf_i]    ; # clk_lf-domain async reset
set_false_path -from [get_ports jtag_trst_n_i]  ; # TCK-domain async TAP reset

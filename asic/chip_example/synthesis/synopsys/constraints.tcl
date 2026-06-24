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
# Asynchronous to free_clk; the ACLINT contains the CDC between the two domains.
if {![info exists CLOCK_LF_PERIOD]} {
    set CLOCK_LF_PERIOD 30517.6; # 32.768 kHz
}

create_clock -name     "clk_lf"                      \
             -period   "$CLOCK_LF_PERIOD"            \
             -waveform "0 [expr $CLOCK_LF_PERIOD/2]" \
             [get_ports clk_lf_i]

set_clock_groups -asynchronous -group [get_clocks free_clk] -group [get_clocks clk_lf]


##############################################################################
#                                                                            #
#                          CREATE PATH GROUPS                                #
#                                                                            #
##############################################################################

group_path -name REGOUT      -to   [all_outputs]
group_path -name REGIN       -from [remove_from_collection [all_inputs] [get_ports "free_clk_i clk_lf_i"]]
group_path -name FEEDTHROUGH -from [remove_from_collection [all_inputs] [get_ports "free_clk_i clk_lf_i"]] -to [all_outputs]


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

set_input_delay $IRQ_DLY       -max -clock "free_clk"  [get_ports irq_software_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports irq_software_i]

set_input_delay $IRQ_DLY       -max -clock "free_clk"  [get_ports irq_timer_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports irq_timer_i]

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
set NMI_VEC_DLY      [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $NMI_DLY       -max -clock "free_clk"  [get_ports nmi_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports nmi_i]

set_input_delay $NMI_VEC_DLY   -max -clock "free_clk"  [get_ports nmi_vector_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports nmi_vector_i]


#=========================#
# LOCKUP STATUS           #
#=========================#

set LOCKUP_DLY       [expr ($CLOCK_PERIOD/100) * 70]

set_output_delay $LOCKUP_DLY   -add_delay -max -clock "free_clk"  [get_ports lockup_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports lockup_o]


#=========================#
# TIME INTERFACE (ZICNTR) #
#=========================#

set TIME_IN_DLY      [expr ($CLOCK_PERIOD/100) * 20]
set TIME_OUT_DLY     [expr ($CLOCK_PERIOD/100) * 70]

set_input_delay $TIME_IN_DLY   -max -clock "free_clk"  [get_ports time_gnt_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports time_gnt_i]

set_input_delay $TIME_IN_DLY   -max -clock "free_clk"  [get_ports time_val_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports time_val_i]

set_output_delay $TIME_OUT_DLY -add_delay -max -clock "free_clk"  [get_ports time_req_o]
set_output_delay 0                         -min -clock "free_clk"  [get_ports time_req_o]


#=================================#
# HPM PLATFORM EVENTS (ZIHPM)     #
#=================================#

set HPM_EVT_DLY      [expr ($CLOCK_PERIOD/100) * 20]

set_input_delay $HPM_EVT_DLY   -max -clock "free_clk"  [get_ports hpm_platform_events_i]
set_input_delay 0              -min -clock "free_clk"  [get_ports hpm_platform_events_i]


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

set_false_path -from hresetn_i

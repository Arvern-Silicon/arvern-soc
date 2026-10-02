##############################################################################
#
# report_timing_summary.tcl
#
# Headline timing / loop / area numbers for a synthesized design, written in
# a machine-readable key=value form so two runs (e.g. before and after an RTL
# change, or one per AHB fabric flavor) can be diffed by summarize_timing.py.
#
# Written files (all under ./results/):
#
#   report.qor             report_qor          -- per path-group WNS/TNS/NVP
#   report.loops           report_timing -loops -- combinational loops DC found
#   report.disabled_arcs   report_disable_timing -- arcs DC cut (a broken loop
#                                                   shows up here, silently)
#   report.timing_summary  key=value summary   -- WNS/TNS/NVP (setup + hold),
#                                                 per-group WNS, loop counts,
#                                                 area
#
# Usage (from dc_shell after synthesis or after reading a .ddc):
#
#   source report_timing_summary.tcl
#   report_timing_summary
#
# Sourced automatically at the end of synthesis.tcl.
#
##############################################################################

proc _tsum_worst {delay} {
    # Returns {wns tns nvp}: worst slack over all endpoints, total negative
    # slack, number of violating endpoints (all path groups together). Not
    # capped like the -max_paths 200 reports, so tns is exact.
    # -max_paths is per path group, so this returns one path per group;
    # the overall WNS is the minimum over them.
    set worst [get_timing_paths -delay $delay -max_paths 1 -nworst 1]
    if {[sizeof_collection $worst] == 0} {
        return [list "n/a" 0.0 0]
    }
    set wns ""
    foreach_in_collection p $worst {
        set s [get_attribute $p slack]
        if {$wns == "" || $s < $wns} { set wns $s }
    }
    set viol [get_timing_paths -delay $delay -slack_lesser_than 0.0 -max_paths 100000 -nworst 1]
    set tns 0.0
    foreach_in_collection p $viol {
        set tns [expr {$tns + [get_attribute $p slack]}]
    }
    return [list $wns $tns [sizeof_collection $viol]]
}

proc report_timing_summary {} {
    set fp [open "./results/report.timing_summary" w]

    puts $fp "design=[get_object_name [current_design]]"
    if {[info exists ::env(FABRIC_TYPE)]} {
        puts $fp "fabric=$::env(FABRIC_TYPE)"
    }
    if {[info exists ::CLOCK_PERIOD]} {
        puts $fp "clock_period_ns=$::CLOCK_PERIOD"
    }

    # Overall setup / hold
    foreach {wns tns nvp} [_tsum_worst max] {}
    puts $fp "setup_wns=$wns"
    puts $fp "setup_tns=$tns"
    puts $fp "setup_nvp=$nvp"
    foreach {wns tns nvp} [_tsum_worst min] {}
    puts $fp "hold_wns=$wns"
    puts $fp "hold_tns=$tns"
    puts $fp "hold_nvp=$nvp"

    # Per path group (one per clock with set_clock_groups -asynchronous)
    foreach_in_collection g [get_path_groups] {
        set gname [get_object_name $g]
        set p [get_timing_paths -group $gname -delay max -max_paths 1 -nworst 1]
        if {[sizeof_collection $p] > 0} {
            puts $fp "setup_wns\[$gname\]=[get_attribute $p slack]"
        }
        set p [get_timing_paths -group $gname -delay min -max_paths 1 -nworst 1]
        if {[sizeof_collection $p] > 0} {
            puts $fp "hold_wns\[$gname\]=[get_attribute $p slack]"
        }
    }

    # Combinational loops. DC breaks a loop by disabling one arc and only
    # warns (OPT-314 / TIM-179); after that check_timing reports clean. So
    # count both the loop paths and the disabled arcs.
    redirect -file ./results/report.loops         {report_timing -loops -max_paths 100}
    redirect -file ./results/report.disabled_arcs {report_disable_timing}
    set n_loops 0
    set lfp [open "./results/report.loops" r]
    while {[gets $lfp line] >= 0} {
        if {[regexp {^\s*Startpoint:} $line]} { incr n_loops }
    }
    close $lfp
    puts $fp "timing_loops=$n_loops"

    set n_disabled 0
    set dfp [open "./results/report.disabled_arcs" r]
    while {[gets $dfp line] >= 0} {
        # report_disable_timing rows: <cell> <from> <to> <flag> [<reason>]
        # flag 'l' = arc disabled to break a loop
        if {[regexp {^\S+\s+\S+\s+\S+\s+\S*l\S*(\s|$)} $line]} {
            incr n_disabled
        }
    }
    close $dfp
    puts $fp "loop_disabled_arcs=$n_disabled"

    # Area (whole chip, core, fabric) -- the fabric one is what an RTL fix in
    # ahb_interconnect moves.
    redirect -variable a {report_area}
    if {[regexp {Total cell area:\s+([0-9.]+)} $a -> v]} { puts $fp "area_chip=$v" }
    # Instance name depends on the flavor: ahb_interconnect_inst (HIPERF,
    # FUSED) or ahb_interconnect_generic_inst (GENERIC).
    set fab [get_cells -quiet ahb_bus_system_inst/ahb_interconnect_*inst]
    if {[sizeof_collection $fab] == 1} {
        current_design [get_attribute $fab ref_name]
        redirect -variable a {report_area}
        if {[regexp {Total cell area:\s+([0-9.]+)} $a -> v]} { puts $fp "area_fabric=$v" }
        current_design $::DESIGN_NAME
    }
    set core [get_cells -quiet dut]
    if {[sizeof_collection $core] > 0} {
        current_design [get_attribute $core ref_name]
        redirect -variable a {report_area}
        if {[regexp {Total cell area:\s+([0-9.]+)} $a -> v]} { puts $fp "area_core=$v" }
        current_design $::DESIGN_NAME
    }

    close $fp
    redirect -file ./results/report.qor {report_qor}

    # ACLINT clk_lf <-> hclk crossing (timed paths under constraints.tcl's
    # register-collection max_delay exceptions; empty with LF_SYNC_EN=1).
    redirect -file ./results/report.lf_crossing {
        if {[sizeof_collection [all_registers -clock clk_lf]] > 0} {
            echo "==== clk_lf -> hclk ===="
            report_timing -path full -delay max -from [all_registers -clock clk_lf   -clock_pins] -to [all_registers -clock free_clk -data_pins] -max_paths 2
            echo "==== hclk -> clk_lf ===="
            report_timing -path full -delay max -from [all_registers -clock free_clk -clock_pins] -to [all_registers -clock clk_lf   -data_pins] -max_paths 2
        } else {
            echo "no clk_lf registers: no crossing"
        }
    }

    # Echo to the log as well
    set fp [open "./results/report.timing_summary" r]
    puts "\n==== timing summary ===="
    puts [read $fp]
    puts "========================\n"
    close $fp
}

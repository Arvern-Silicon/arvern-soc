##############################################################################
#
# report_sram_path.tcl
#
# Analyzes SRAM-to-SRAM timing paths and breaks down the data path delay
# into segments: SRAM (clk->Q) -> Fabric -> arvern -> Fabric -> SRAM (setup)
#
# Usage (from dc_shell after synthesis or after reading a .ddc):
#
#   source report_sram_path.tcl
#   report_sram_path                ;# worst path across all SRAMs
#   report_sram_path 5              ;# 5 worst paths
#   report_sram_path 1 sram_x       ;# worst path, executable SRAM only
#   report_sram_path 3 rom          ;# 3 worst paths involving ROM
#
##############################################################################

proc classify_point {pin_name} {
    # SRAM macro: the actual memory instance inside sram_8kb_wrapper
    # Matches both before and after change_name -rules verilog:
    #   .u_sram/u_sram_1/...  (original)
    #   _u_sram/u_sram_1/...  (after change_name replaces . with _)
    if {[regexp {u_sram/u_sram(_[01])?/} $pin_name]} {
        return "SRAM"
    }

    # arvern processor core (instance name: dut)
    if {[string match "dut/*" $pin_name]} {
        return "arvern"
    }

    # Everything else: wrappers, controllers, interconnect, decoders, arbiters
    return "Fabric"
}

proc report_sram_path { {num_paths 1} {sram_filter ""} } {

    puts ""
    puts "============================================================================"
    puts "  SRAM-to-SRAM Timing Path Breakdown"
    puts "============================================================================"

    # -----------------------------------------------------------------
    # Find SRAM macro cells by instance name pattern
    # -----------------------------------------------------------------
    set sram_cells [get_cells -quiet -hier -filter \
        {full_name =~ "*u_sram_0" && is_hierarchical == false}]
    set sram_cells [add_to_collection $sram_cells [get_cells -quiet -hier -filter \
        {full_name =~ "*u_sram_1" && is_hierarchical == false}]]
    # Single-instance macros
    set single [get_cells -quiet -hier u_sram -filter \
        {full_name =~ "*sram_gen*" && is_hierarchical == false}]
    if {[sizeof_collection $single] > 0} {
        set sram_cells [add_to_collection $sram_cells $single]
    }

    # Optional hierarchy filter (e.g., "sram_x", "sram_nx", "rom")
    if {$sram_filter ne ""} {
        set sram_cells [filter_collection $sram_cells \
            "full_name =~ *${sram_filter}*"]
    }

    if {[sizeof_collection $sram_cells] == 0} {
        puts "\n  ERROR: No SRAM macro cells found."
        puts "         Check that the design is elaborated and SRAM .db is linked."
        return
    }

    puts "\n  SRAM macros ([sizeof_collection $sram_cells] instances):"
    foreach_in_collection c $sram_cells {
        puts "    [get_attribute $c full_name]  \[[get_attribute $c ref_name]\]"
    }

    # -----------------------------------------------------------------
    # Get SRAM pins for timing path queries
    # -----------------------------------------------------------------
    # Do NOT use -from with output pins (rejected as non-startpoints) or
    # cells (rejected as wrong type). Use -through on SRAM output pins
    # instead: DC finds valid startpoints automatically by tracing backward,
    # and accepts any pin as a -through waypoint regardless of liberty attrs.
    set sram_out [get_pins -quiet -of_objects $sram_cells \
        -filter {direction == out}]
    set sram_to  [get_pins -quiet -of_objects $sram_cells \
        -filter {direction == in}]

    # -----------------------------------------------------------------
    # Get worst timing paths from SRAM to SRAM
    # -----------------------------------------------------------------
    set paths [get_timing_paths -delay max -nworst $num_paths \
                   -through $sram_out -to $sram_to]

    if {[sizeof_collection $paths] == 0} {
        puts "\n  No timing paths found between SRAM macros."
        return
    }

    # -----------------------------------------------------------------
    # Analyze each path
    # -----------------------------------------------------------------
    set path_idx 0
    foreach_in_collection path $paths {
        incr path_idx

        set slack      [get_attribute $path slack]
        set start_name [get_attribute [get_attribute $path startpoint] full_name]
        set end_name   [get_attribute [get_attribute $path endpoint]   full_name]

        puts ""
        puts "  --------------------------------------------------------------------------"
        puts "  Path $path_idx"
        puts "  --------------------------------------------------------------------------"
        puts "  Startpoint : $start_name"
        puts "  Endpoint   : $end_name"
        puts [format "  Slack      : %.3f" $slack]

        # ---- Walk through path points and build segment list ----
        set points  [get_attribute $path points]
        set segments {}
        set cur_seg  ""
        set seg_t0   0.0
        set prev_t   0.0

        foreach_in_collection pt $points {
            set pin_name [get_attribute [get_attribute $pt object] full_name]
            set arrival  [get_attribute $pt arrival]
            set seg      [classify_point $pin_name]

            if {$seg ne $cur_seg} {
                if {$cur_seg ne ""} {
                    lappend segments [list $cur_seg $seg_t0 $arrival \
                        [expr {$arrival - $seg_t0}]]
                }
                set cur_seg $seg
                set seg_t0  $arrival
            }
            set prev_t $arrival
        }
        # Close last segment
        if {$cur_seg ne ""} {
            lappend segments [list $cur_seg $seg_t0 $prev_t \
                [expr {$prev_t - $seg_t0}]]
        }

        set data_arrival $prev_t

        # ---- Compute SRAM setup time ----
        # required = data_arrival + slack
        # setup    = clock_period - required
        set required   [expr {$data_arrival + $slack}]
        set clk_period [lindex [get_attribute [get_clocks *] period] 0]
        set setup_time [expr {$clk_period - $required}]

        # ---- Post-process: rename first/last SRAM segments ----
        set display {}
        set seg_count [llength $segments]

        for {set i 0} {$i < $seg_count} {incr i} {
            set s     [lindex $segments $i]
            set name  [lindex $s 0]
            set t0    [lindex $s 1]
            set t1    [lindex $s 2]
            set delay [lindex $s 3]

            if {$i == 0 && $name eq "SRAM"} {
                # Startpoint: SRAM clock-to-Q
                lappend display [list "SRAM (clk->Q)" $t0 $t1 $delay]
            } elseif {$i == $seg_count - 1 && $name eq "SRAM"} {
                # Endpoint pin with ~0 data-path delay — skip,
                # replaced by SRAM (setup) below
            } else {
                lappend display [list $name $t0 $t1 $delay]
            }
        }
        # Append the SRAM setup time
        lappend display [list "SRAM (setup)" "" "" $setup_time]

        # ---- Print segment table ----
        puts ""
        puts [format "  %-16s %8s %8s %8s %6s" \
            "Segment" "Start" "End" "Delay" "%"]
        puts "  [string repeat - 52]"

        # Total budget = data_arrival + setup
        set total_budget [expr {$data_arrival + $setup_time}]
        set seg_names {}

        foreach s $display {
            set name  [lindex $s 0]
            set t0    [lindex $s 1]
            set t1    [lindex $s 2]
            set delay [lindex $s 3]

            if {$total_budget > 0} {
                set pct [expr {100.0 * $delay / $total_budget}]
            } else {
                set pct 0.0
            }

            if {$t0 ne ""} {
                puts [format "  %-16s %8.3f %8.3f %8.3f %5.1f%%" \
                    $name $t0 $t1 $delay $pct]
            } else {
                puts [format "  %-16s %8s %8s %8.3f %5.1f%%" \
                    $name "" "" $delay $pct]
            }
            lappend seg_names [list $name $delay $pct]
        }

        puts "  [string repeat - 52]"
        puts [format "  %-16s %8s %8s %8.3f" "Data Arrival" "" "" $data_arrival]
        puts [format "  %-16s %8s %8s %8.3f" "SRAM Setup" "" "" $setup_time]
        puts [format "  %-16s %8s %8s %8.3f" "Clock Period" "" "" $clk_period]
        puts [format "  %-16s %8s %8s %8.3f" "Slack" "" "" $slack]

        # ---- Flow summary with aligned arrows ----
        # Compute column width per segment (max of time and pct representations)
        set col_widths {}
        foreach s $seg_names {
            set name  [lindex $s 0]
            set delay [lindex $s 1]
            set pct   [lindex $s 2]
            set time_str [format "%s(%.2f)" $name $delay]
            set pct_str  [format "%s(%.0f%%)" $name $pct]
            set w [expr {max([string length $time_str], [string length $pct_str])}]
            lappend col_widths $w
        }

        set flow_line "  Flow: "
        set pct_line  "        "
        set i 0
        foreach s $seg_names {
            set name  [lindex $s 0]
            set delay [lindex $s 1]
            set pct   [lindex $s 2]
            set w     [lindex $col_widths $i]
            set time_str [format "%s(%.2f)" $name $delay]
            set pct_str  [format "%s(%.0f%%)" $name $pct]
            if {$i > 0} {
                append flow_line " -> "
                append pct_line  " -> "
            }
            append flow_line [format "%-${w}s" $time_str]
            append pct_line  [format "%-${w}s" $pct_str]
            incr i
        }

        puts ""
        puts $flow_line
        puts $pct_line
    }

    puts ""
    puts "============================================================================"
    puts ""
}

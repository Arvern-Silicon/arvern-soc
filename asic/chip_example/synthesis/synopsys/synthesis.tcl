
#=============================================================================#
#                                Configuration                                #
#=============================================================================#

# Enable/Disable DC_ULTRA option
set WITH_DC_ULTRA 1

# Enable/Disable DFT insertion
set WITH_DFT      1


#=============================================================================#
#                           Read technology library                           #
#=============================================================================#
source -echo -verbose ./library.tcl


#=============================================================================#
#                               Read design RTL                               #
#=============================================================================#
source -echo -verbose ./read.tcl


#=============================================================================#
#                           Set design constraints                            #
#=============================================================================#
source -echo -verbose ./constraints.tcl


#=============================================================================#
#              Set operating conditions & wire-load models                    #
#=============================================================================#

# Set operating conditions
set_operating_conditions -max $LIB_WC_OPCON -max_library $LIB_WC_NAME \
	                     -min $LIB_BC_OPCON -min_library $LIB_BC_NAME

# Set wire-load models
set_wire_load_mode top
set_wire_load_model -name $LIB_WIRE_LOAD -max -library $LIB_WC_NAME
set_wire_load_model -name $LIB_WIRE_LOAD -min -library $LIB_BC_NAME


#=============================================================================#
#                                Synthesize                                   #
#=============================================================================#

# Prevent assignment statements in the Verilog netlist.
set_fix_multiple_port_nets -all -buffer_constants

# Configuration
current_design $DESIGN_NAME
uniquify
set_max_area  0.0
set_flatten false
set_structure true -timing true -boolean true

# Verify constraints before synthesis
redirect -tee -file ./results/report.check_timing_pre {check_timing}

# Synthesis
if {$WITH_DC_ULTRA} {
    if {$WITH_DFT} {
	    compile_ultra -scan -no_autoungroup
    } else {
	    compile_ultra       -no_autoungroup
    }

    # Area optimization (run after compile)
    optimize_netlist -area

    # Check if timing met after area optimization
    if {[sizeof_collection [get_timing_paths -slack_lesser_than 0.0 -max_paths 1]] > 0} {
        puts "WARNING: Timing violated after area optimization. Re-optimizing for timing..."
        compile_ultra -incremental
    }

} else {
    if {$WITH_DFT} {
	compile       -scan -map_effort high -area_effort high
    } else {
	compile             -map_effort high -area_effort high
    }
}


#=============================================================================#
#                                DFT Insertion                                #
#=============================================================================#
if {$WITH_DFT} {

    # DFT Signal Type Definitions
    # scan_mode_i: test-mode constant. In the ACLINT it isolates the clk_lf_i
    # clock-as-data path and bypasses the trust-reset synchronizer; in the JTAG
    # DTM it holds the TAP resets inactive; without the declaration DRC reports
    # D10 on the clock-as-data path and D3 on the internally generated resets.
    set_dft_signal -view spec         -type Constant    -port scan_mode_i   -active_state 1
    set_dft_signal -view existing_dft -type Constant    -port scan_mode_i   -active_state 1
    # No scan-enable pin: DC creates the chains' scan-enable port at insertion.
    # Every clock and reset pin of the chip: the core clock, the ACLINT's
    # low-frequency clock and its reset, and the JTAG DTM's TCK and TRST.
    set_dft_signal -view existing_dft -type ScanClock   -port free_clk_i    -timing [list 45 55]
    set_dft_signal -view existing_dft -type ScanClock   -port clk_lf_i      -timing [list 45 55]
    set_dft_signal -view existing_dft -type ScanClock   -port jtag_tck_i    -timing [list 45 55]
    set_dft_signal -view existing_dft -type Reset       -port hresetn_i     -active 0
    set_dft_signal -view existing_dft -type Reset       -port resetn_lf_i   -active 0
    set_dft_signal -view existing_dft -type Reset       -port jtag_trst_n_i -active 0

    # DFT Configuration
    set_dft_insertion_configuration -preserve_design_name true
    set_scan_configuration -style multiplexed_flip_flop
    set_scan_configuration -clock_mixing mix_clocks
    set_scan_configuration -chain_count 3

    # DFT Test Protocol Creation
    create_test_protocol

    # DFT Design Rule Check
    # Expected DRC baseline (anything else is a regression):
    #   TEST-451 x12 -- the ROM / SRAM macros are black boxes to DFT (no test model
    #                   in the library views). A production flow tests them by
    #                   memory BIST with the vendor's test models; this example
    #                   SoC does not, so the logic around them is covered only as
    #                   far as ATPG can reach through a black box.
    #   D14      x2  -- the JTAG DTM launches TDO on the falling TCK edge
    #                   (IEEE 1149.1).
    redirect -tee -file ./results/report.dft_drc           {dft_drc}
    redirect      -file ./results/report.dft_drc_verbose   {dft_drc -verbose}
    redirect      -file ./results/report.dft_drc_coverage  {dft_drc -coverage_estimate}
    redirect      -file ./results/report.dft_scan_config   {report_scan_configuration}
    redirect      -file ./results/report.dft_insert_config {report_dft_insertion_configuration}

    # Preview DFT insertion
    redirect -tee -file ./results/report.dft_preview       {preview_dft}
    redirect      -file ./results/report.dft_preview_all   {preview_dft -show all -test_points all}

    # DFT insertion
    insert_dft

    # DFT Incremental Compile
    if {$WITH_DC_ULTRA} {
	    compile_ultra -scan -incremental
    } else {
	    compile       -scan -incremental
    }

    # DFT Coverage estimate
    redirect      -file ./results/report.dft_drc_coverage  {dft_drc -coverage_estimate}
}

#=============================================================================#
#                            Reports generation                               #
#=============================================================================#

redirect -file ./results/report.timing         {check_timing}
redirect -file ./results/report.constraints    {report_constraints -all_violators -verbose}
redirect -file ./results/report.paths.max      {report_timing -path end  -delay max -max_paths 200 -nworst 2}
redirect -file ./results/report.full_paths.max {report_timing -path full -delay max -max_paths 5   -nworst 2}
redirect -file ./results/report.paths.min      {report_timing -path end  -delay min -max_paths 200 -nworst 2}
redirect -file ./results/report.full_paths.min {report_timing -path full -delay min -max_paths 5   -nworst 2}

# Violations-only, worst-slack first (report_timing sorts by ascending slack by
# default). -slack_lesser_than 0.0 keeps only failing paths, so the file is EMPTY
# (bar the header) when timing is clean. Caps at 1000 paths / 10 per endpoint --
# if a design violates beyond that, the headline numbers already tell the story.
redirect -file ./results/report.violations.max {report_timing -path full -delay max -slack_lesser_than 0.0 -max_paths 1000 -nworst 10}
redirect -file ./results/report.violations.min {report_timing -path full -delay min -slack_lesser_than 0.0 -max_paths 1000 -nworst 10}
redirect -file ./results/report.refs           {report_reference}
redirect -file ./results/report.area           {report_area}
redirect -file ./results/report.full_area      {report_area -hierarchy}

# Get clock period and frequency
set      CLK_FREQ     [expr {1000.0 / $CLOCK_PERIOD}]
set    ::CLK_FREQ_RPT         "\n"
append ::CLK_FREQ_RPT         "                ===========================================\n"
append ::CLK_FREQ_RPT [format "               |    Clock period: %.1f ns  (%.0f MHz)\n" $CLOCK_PERIOD $CLK_FREQ]
append ::CLK_FREQ_RPT         "                ===========================================\n\n"

# Add NAND2 size equivalent report to the area report file
set ::AREA_SUMMARY ""
if {[info exists NAND2_NAME]} {
    set nand2_area [get_attribute [get_lib_cell $LIB_WC_NAME/$NAND2_NAME] area]

    current_design $DESIGN_NAME
    redirect -variable chip_area {report_area}

    current_design $DESIGN_NAME
    current_design [get_attribute [get_cells dut] ref_name]
    redirect -variable arv_area {report_area}

    # Instance name depends on the fabric flavor: ahb_interconnect_inst for
    # HIPERF / FUSED, ahb_interconnect_generic_inst for GENERIC.
    current_design $DESIGN_NAME
    current_design [get_attribute [get_cells ahb_bus_system_inst/ahb_interconnect_*inst] ref_name]
    redirect -variable ahb_area {report_area}

    regexp {Total cell area:\s+([^\n]+)\n} $chip_area whole_match chip_area
    regexp {Total cell area:\s+([^\n]+)\n} $arv_area  whole_match arv_area
    regexp {Total cell area:\s+([^\n]+)\n} $ahb_area  whole_match ahb_area

    set chip_nand2_eq [expr round($chip_area/$nand2_area)]
    set arv_nand2_eq  [expr round($arv_area/$nand2_area)]
    set ahb_nand2_eq  [expr round($ahb_area/$nand2_area)]

    set chip_area     [expr round($chip_area)]
    set arv_area      [expr round($arv_area)]
    set ahb_area      [expr round($ahb_area)]

    set fp [open "./results/report.area" a]
    puts $fp ""
    puts $fp "NAND2 equivalent cell area: Chip-example     --> $chip_nand2_eq"
    puts $fp "NAND2 equivalent cell area: arvern           --> $arv_nand2_eq"
    puts $fp "NAND2 equivalent cell area: AHB Interconnect --> $ahb_nand2_eq"
    close $fp
    append ::AREA_SUMMARY "\n"
    append ::AREA_SUMMARY "      =================================================================\n"
    append ::AREA_SUMMARY "     |                           AREA SUMMARY                          |\n"
    append ::AREA_SUMMARY "     |-----------------------------------------------------------------|\n"
    append ::AREA_SUMMARY "     |                                                                 |\n"
    append ::AREA_SUMMARY [format "%-5s| %-9s cell gate area: %-37s |\n" "" $NAND2_NAME $nand2_area]
    append ::AREA_SUMMARY "     |                                                                 |\n"
    append ::AREA_SUMMARY [format "%-5s| %-15s %-15s %-15s %-15s |\n" "" ""           "Chip Example" "arvern"      "AHB"]
    append ::AREA_SUMMARY [format "%-5s| %-15s %-15s %-15s %-15s |\n" "" "Area:"      $chip_area     $arv_area     $ahb_area]
    append ::AREA_SUMMARY [format "%-5s| %-15s %-15s %-15s %-15s |\n" "" "NAND2 eq.:" $chip_nand2_eq $arv_nand2_eq $ahb_nand2_eq]
    append ::AREA_SUMMARY "     |                                                                 |\n"
    append ::AREA_SUMMARY "      =================================================================\n"
    append ::AREA_SUMMARY "\n"
}

# Machine-readable WNS/TNS/NVP, loop and area summary (report.timing_summary,
# report.qor, report.loops, report.disabled_arcs) for run-to-run comparison.
current_design $DESIGN_NAME
source ./report_timing_summary.tcl
report_timing_summary

#=============================================================================#
#          Dump gate level netlist, final DDC file and Test protocol          #
#=============================================================================#
current_design $DESIGN_NAME

change_name -rules verilog -hierarchy

write -hierarchy -format verilog -output "./results/$DESIGN_NAME.gate.v"
write -hierarchy -format ddc     -output "./results/$DESIGN_NAME.ddc"

if {$WITH_DFT} {
    write_test_protocol          -output "./results/$DESIGN_NAME.spf"
}

#=============================================================================#
#                    PRINT SOME FINAL INTERESTING RESULTS                     #
#=============================================================================#

# SRAM-to-SRAM timing path breakdown
current_design $DESIGN_NAME
source ./report_sram_path.tcl
redirect -tee -file ./results/report.sram_path {report_sram_path}

# Print area summary with NAND2 equivalent cell count
puts $::AREA_SUMMARY

# Print clock period and frequency
puts $::CLK_FREQ_RPT

if {$::env(NO_QUIT) == 0} {
    quit
}

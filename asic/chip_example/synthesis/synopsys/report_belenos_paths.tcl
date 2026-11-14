##############################################################################
#
# report_belenos_paths.tcl
#
# Timing of the Belenos graphic controller inside the chip: the worst path
# into each of its register groups, and the detailed paths into the pixel
# engine's token register (u_t2), which ends the blend alpha path.
#
# Usage (from dc_shell after synthesis or after reading a .ddc):
#
#   source report_belenos_paths.tcl
#   report_belenos_paths
#
##############################################################################

proc report_belenos_paths {} {
    set regs [filter_collection [all_registers -data_pins] "full_name =~ belenos_inst/*"]
    if {[sizeof_collection $regs] == 0} {
        puts "report_belenos_paths: no Belenos register found"
        return
    }
    puts "\n#### Belenos: worst paths into its registers (one per endpoint, 20 worst)\n"
    report_timing -to $regs -max_paths 20 -nworst 1 -path_type end
    set t2 [filter_collection [all_registers -data_pins] "full_name =~ belenos_inst/*u_gpu/u_px/u_px_fw/u_t2/*"]
    if {[sizeof_collection $t2] > 0} {
        puts "\n#### Belenos pixel engine: the 3 worst paths into the token register u_t2\n"
        report_timing -to $t2 -max_paths 3 -nworst 1 -input_pins -nets
    }
}

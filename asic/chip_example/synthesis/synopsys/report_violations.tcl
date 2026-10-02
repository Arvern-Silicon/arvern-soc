#=============================================================================#
#  report_violations.tcl                                                        #
#-----------------------------------------------------------------------------#
#  Emit timing reports that contain ONLY violating paths (negative slack),      #
#  worst-slack first, in the same full-path format as report.full_paths.max.    #
#                                                                               #
#  report_timing sorts by ascending slack by default, so the worst violation    #
#  is first. -slack_lesser_than 0.0 drops every path that meets timing, so a     #
#  clean design yields an (essentially) empty file -- header only.              #
#                                                                               #
#  synthesis.tcl already writes these two files at the end of every run_syn.     #
#  This standalone copy is for regenerating them on demand from an already-      #
#  loaded design, e.g. after `./run_syn -noquit` (dc_shell stays open):         #
#                                                                               #
#      dc_shell> source report_violations.tcl                                   #
#                                                                               #
#  No recompile needed -- it just re-queries the design currently in memory.    #
#=============================================================================#

redirect -file ./results/report.violations.max {
    report_timing -path full -delay max -slack_lesser_than 0.0 -max_paths 1000 -nworst 10
}
redirect -file ./results/report.violations.min {
    report_timing -path full -delay min -slack_lesser_than 0.0 -max_paths 1000 -nworst 10
}

# Console summary: how many violating paths each report captured.
set n_setup [sizeof_collection [get_timing_paths -delay max -slack_lesser_than 0.0 -max_paths 1000 -nworst 10]]
set n_hold  [sizeof_collection [get_timing_paths -delay min -slack_lesser_than 0.0 -max_paths 1000 -nworst 10]]
echo "report_violations: setup (max) violating paths = $n_setup  -> results/report.violations.max"
echo "report_violations: hold  (min) violating paths = $n_hold  -> results/report.violations.min"

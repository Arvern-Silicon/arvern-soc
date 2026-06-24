project_open arvern_fpga
create_timing_netlist
read_sdc
update_timing_netlist

set rpt_file "output_files/arvern_fpga.critical_paths.rpt"

# Report top 20 critical setup paths (slow 1100mV 85C)
report_timing -setup -npaths 100 -detail full_path -file $rpt_file

delete_timing_netlist
project_close

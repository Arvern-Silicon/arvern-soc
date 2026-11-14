#------------------------------------------------------------------------------
# Gowin synthesis, place & route and bitstream for the Sipeed Tang Console 138K.
# Run by ../0_create_bitstream.sh from ./WORK (gw_sh synthesis.tcl), which
# holds pmem.mem (the firmware image read by $readmemh) and design_files.f
# (the flattened RTL filelist). The run is a Gowin project,
# WORK/arvern_fpga/arvern_fpga.gprj (results in WORK/arvern_fpga/impl): the
# Gowin IDE opens it as it is. Gowin keeps the project's file paths relative
# to the current directory but resolves them from the project directory, so
# the script works from the project directory with absolute paths.
#
# The 138K SoM has been built with two silicon versions (B and C): set
# GOWIN_DEVICE_VERSION to the one on the board (the programmer's detect output
# or the package marking).
#------------------------------------------------------------------------------

set dev_version "C"
if {[info exists ::env(GOWIN_DEVICE_VERSION)]} { set dev_version $::env(GOWIN_DEVICE_VERSION) }

set work [file normalize .]
create_project -name arvern_fpga -dir $work -pn GW5AST-LV138PG484AC1/I0 -device_version $dev_version -force
cd [file join $work arvern_fpga]

# RTL: +incdir+ lines become include paths, the others are source files
set incdirs {}
set fh [open [file join $work design_files.f] r]
while {[gets $fh line] >= 0} {
    set line [string trim $line]
    if {$line eq "" || [string match "//*" $line] || [string match "#*" $line]} { continue }
    if {[string match "+incdir+*" $line]} {
        lappend incdirs [file normalize [file join $work [string range $line 8 end]]]
    } elseif {[string match "+*" $line] || [string match "-*" $line]} {
        continue
    } elseif {[file tail $line] eq "arvern_fpga.v" && [info exists ::env(GOWIN_PARAM_OVERRIDES)]} {
        # Experiment builds: GOWIN_PARAM_OVERRIDES="NAME=VALUE ..." changes those
        # top-level parameters in a copy of the top (the RTL is left as it is)
        set fh2 [open [file join $work $line] r]; set src [read $fh2]; close $fh2
        foreach ov $::env(GOWIN_PARAM_OVERRIDES) {
            lassign [split $ov "="] pname pval
            if {![regsub "(parameter\\s+${pname}\\s*=\\s*)\[^;\]+;" $src "\\1${pval};" src]} {
                error "GOWIN_PARAM_OVERRIDES: no parameter $pname in [file tail $line]"
            }
            puts "Override: $pname = $pval"
        }
        set fh2 [open arvern_fpga.v w]; puts -nonewline $fh2 $src; close $fh2
        add_file -type verilog [file normalize arvern_fpga.v]
    } elseif {[file tail $line] eq "pmem_bram.v"} {
        # The firmware image: GowinSynthesis does not resolve the relative
        # $readmemh path from here, so a copy names it in full
        set fh2 [open [file join $work $line] r]; set src [read $fh2]; close $fh2
        regsub -all {"pmem_bank} $src "\"$work/pmem_bank" src
        set fh2 [open pmem_bram.v w]; puts -nonewline $fh2 $src; close $fh2
        add_file -type verilog [file normalize pmem_bram.v]
    } else {
        add_file -type verilog [file normalize [file join $work $line]]
    }
}
close $fh

# The debug transport (DTM_TYPE in the top-level RTL) sets the SDRAM1 header pads and
# the TCK constraint: a cJTAG build (3) holds TCKC/TMSC with the pad keeper (the
# OScan1 protocol leaves TMSC undriven) and constrains TCKC at 266.667 ns
set dtm_type 0
set fh [open [file join $work ../../../rtl/verilog/arvern_fpga.v] r]
regexp -line {parameter\s+DTM_TYPE\s*=\s*([0-9]+)} [read $fh] -> dtm_type
close $fh
puts "DTM_TYPE = $dtm_type"

# The display (BOARD_HDMI in the top-level RTL, or a GOWIN_PARAM_OVERRIDES
# entry): the HDMI build adds the 27.03 MHz pixel and 135.16 MHz serialiser clocks
set board_hdmi 0
set board_lcd_pclk_sep 0
set fh [open [file join $work ../../../rtl/verilog/arvern_fpga.v] r]; set top [read $fh]; close $fh
regexp -line {parameter\s+BOARD_HDMI\s*=\s*([0-9]+)} $top -> board_hdmi
regexp -line {parameter\s+BOARD_LCD_PCLK_SEP\s*=\s*([0-9]+)} $top -> board_lcd_pclk_sep
if {[info exists ::env(GOWIN_PARAM_OVERRIDES)]} {
    foreach ov $::env(GOWIN_PARAM_OVERRIDES) {
        lassign [split $ov "="] pname pval
        if {$pname eq "BOARD_HDMI"} { set board_hdmi $pval }
        if {$pname eq "BOARD_LCD_PCLK_SEP"} { set board_lcd_pclk_sep $pval }
    }
}
puts "BOARD_HDMI = $board_hdmi, BOARD_LCD_PCLK_SEP = $board_lcd_pclk_sep"

set fh [open [file join $work ../scripts/design.cst] r]; set cst [read $fh]; close $fh
set fh [open [file join $work ../scripts/design.sdc] r]; set sdc [read $fh]; close $fh
if {$dtm_type == 3} {
    regsub -all {(IO_PORT "SDRAM1\[(3|4)\]"[^;]*)PULL_MODE=UP} $cst {\1PULL_MODE=KEEPER} cst
    regsub {(create_clock -name jtag_tck -period )40\.000 -waveform \{0 20\.000\}} $sdc {\1266.667 -waveform {0 133.333}} sdc
}
# The placer stops improving once it meets its target, so it aims at 27 ns
# (37 MHz), tighter than the design.sdc requirement: the project's SDC and the
# Gowin timing report show that period and its negative slack, and
# timing_summary.py judges the result against design.sdc. Gowin has no seed:
# GOWIN_SYS_PERIOD (ns) and GOWIN_PLACE_OPTION (the placement algorithm) are
# the ways to explore placements.
set sys_period 27
if {[info exists ::env(GOWIN_SYS_PERIOD)]} { set sys_period $::env(GOWIN_SYS_PERIOD) }
regsub {(create_clock -name sys_clk -period )[0-9.]+ -waveform \{0 [0-9.]+\}} $sdc "\\1$sys_period -waveform {0 [expr {$sys_period / 2.0}]}" sdc
puts "sys_clk placer period: $sys_period ns"
# Placement algorithm: 1 when run on its own; ../0_create_bitstream.sh runs
# several in parallel and keeps the best
set place_option 1
if {[info exists ::env(GOWIN_PLACE_OPTION)]} { set place_option $::env(GOWIN_PLACE_OPTION) }
puts "place_option: $place_option"
if {$board_hdmi != 0} {
    append sdc "// HDMI build: the TMDS serialisers' bit clock (DDR) from the PLL and the\n"
    append sdc "// Belenos pixel clock, clk_ser / 5 through a CLKDIV; Belenos crosses from clk_sys to\n"
    append sdc "// clk_pix through synchronisers and a request/acknowledge handshake\n"
    append sdc "create_clock -name pix_clk -period 36.995 -waveform {0 18.497} \[get_nets {clk_pix}\]\n"
    append sdc "create_clock -name ser_clk -period 7.399 -waveform {0 3.700} \[get_nets {clk_ser}\]\n"
    append sdc "set_clock_groups -asynchronous -group \[get_clocks {pix_clk ser_clk}\] -group \[get_clocks {sys_clk}\]\n"
} elseif {$board_lcd_pclk_sep != 0} {
    append sdc "// Panel build on its own pixel clock: the system PLL's second output\n"
    append sdc "create_clock -name pix_clk -period 88.615 -waveform {0 44.308} \[get_nets {clk_lcd_pix}\]\n"
    append sdc "set_clock_groups -asynchronous -group \[get_clocks {pix_clk}\] -group \[get_clocks {sys_clk}\]\n"
}
set fh [open design.cst w]; puts -nonewline $fh $cst; close $fh
set fh [open design.sdc w]; puts -nonewline $fh $sdc; close $fh

add_file -type cst [file normalize design.cst]
add_file -type sdc [file normalize design.sdc]

set_option -top_module        arvern_fpga
set_option -verilog_std       v2001
set_option -include_path      [join $incdirs ";"]
set_option -output_base_name  arvern_fpga
set_option -use_sspi_as_gpio  1      ;# LCD G5/G6 sit on the SSPI pins (M22, N22)
set_option -use_mspi_as_gpio  1
set_option -use_cpu_as_gpio   1      ;# J10 pins 1-3 sit on SSPI / CPU-mode pins
set_option -use_ready_as_gpio 1
set_option -use_done_as_gpio  1
set_option -rw_check_on_ram   0
set_option -timing_driven     1
set_option -global_freq       75     ;# synthesis target (the SDC holds the clock periods)
set_option -place_option      $place_option
set route_option 0                   ;# default router (1 left nets unrouted); GOWIN_ROUTE_OPTION overrides
if {[info exists ::env(GOWIN_ROUTE_OPTION)]} { set route_option $::env(GOWIN_ROUTE_OPTION) }
set_option -route_option      $route_option
puts "route_option: $route_option"
set_option -replicate_resources 1    ;# duplicate high-fanout drivers

# Boot from the configuration flash: the bitstream is compressed and read at
# 35 MHz (the C device version defaults to 2.5 MHz, a boot of over 10 s)
set_option -bit_compress      1
set_option -gen_posp          1      ;# post-place file: the sites of the BSRAM, DSP and PLL blocks

# Synthesis goal: timing (about a quarter more LUTs than auto, and the fastest
# placements); GOWIN_OPT_GOAL = auto | area | timing and GOWIN_RETIMING =
# none | all | bsram | dsp override it
set opt_goal timing
if {[info exists ::env(GOWIN_OPT_GOAL)]} { set opt_goal $::env(GOWIN_OPT_GOAL) }
set_option -opt_goal $opt_goal
puts "opt_goal: $opt_goal"
if {[info exists ::env(GOWIN_RETIMING)]} { set_option -retiming_resource $::env(GOWIN_RETIMING); puts "retiming: $::env(GOWIN_RETIMING)" }
# LUT mapping (GOWIN_MAP_OPTION): 1 default, 2 LUT5 at equal area, 3 LUT5 and
# 4 LUT5/LUT6 trading LUTs for fewer levels
if {[info exists ::env(GOWIN_MAP_OPTION)]} { set_option -map_option $::env(GOWIN_MAP_OPTION); puts "map_option: $::env(GOWIN_MAP_OPTION)" }
# Further options (experiments): GOWIN_EXTRA_OPTS = "name value" pairs
# separated by ';', e.g. "netlist_hierarchy 0;max_fanout 100"
if {[info exists ::env(GOWIN_EXTRA_OPTS)]} {
    foreach opt [split $::env(GOWIN_EXTRA_OPTS) ";"] {
        lassign [string trim $opt] oname oval
        set_option -$oname $oval
        puts "option: -$oname $oval"
    }
}

# Incremental placement (experiment): GOWIN_INC_PLACE = a previous build's
# placement file (impl/pnr/arvern_fpga.p), reused as the starting placement
if {[info exists ::env(GOWIN_INC_PLACE)]} {
    set_option -inc_place $::env(GOWIN_INC_PLACE)
    puts "incremental placement from $::env(GOWIN_INC_PLACE)"
}
set_option -loading_rate      35.000

# Floorplan (experiment): GOWIN_FP_ARGS = the arguments of gen_regions.py
# after the netlist (--pins <posp>, --fence <region>, --gfx <region>, see the
# script); the constraints are generated from the synthesized netlist and
# appended to the CST before place & route
if {[info exists ::env(GOWIN_FP_ARGS)]} {
    run syn
    set_option -cst_warn_to_error 0    ;# a few netlist cells are merged away by the packer
    exec python3 [file join $work ../scripts/gen_regions.py] [file normalize impl/gwsynthesis/arvern_fpga.vg] \
        {*}$::env(GOWIN_FP_ARGS) >> [file normalize design.cst]
    puts "floorplan: $::env(GOWIN_FP_ARGS)"
    run pnr
} else {
    run all
}

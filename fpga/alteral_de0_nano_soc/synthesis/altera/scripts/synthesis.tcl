# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      synthesis.tcl
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------

# Load Quartus II Tcl Project package
package require ::quartus::project
package require ::quartus::flow

# Create project
project_new -revision arvern_fpga arvern_fpga

# Make assignments
set_global_assignment   -name FAMILY "Cyclone V"
set_global_assignment   -name DEVICE 5CSEMA4U23C6

# Source list auto-generated from the single-source design filelist
# (sim/rtl_sim/src/filelist_core.f) by 0_create_bitstream.sh via
# flatten_filelist.py --format qsf. Replaces the formerly hand-maintained
# design_rtl.v (which had drifted -- e.g. missing arv_ipdff.v / arv_dff.v).
source                  ../scripts/design_files.qsf
set_global_assignment   -name SEARCH_PATH              ../../../rtl/verilog/
set_global_assignment   -name TOP_LEVEL_ENTITY         arvern_fpga
set_global_assignment   -name PROJECT_OUTPUT_DIRECTORY output_files

# Limit parallel processors (avoids memory issues)
set_global_assignment   -name NUM_PARALLEL_PROCESSORS             2

# PERFORMANCE / AREA / BALANCED
#set_global_assignment  -name OPTIMIZATION_MODE            BALANCED
set_global_assignment   -name OPTIMIZATION_MODE           "HIGH PERFORMANCE EFFORT"
#set_global_assignment  -name OPTIMIZATION_MODE           "AGGRESSIVE PERFORMANCE"
set_global_assignment   -name OPTIMIZATION_TECHNIQUE       SPEED                        ;# AREA - BALANCED - SPEED

set_global_assignment   -name ALLOW_REGISTER_MERGING       ON
set_global_assignment   -name ALLOW_REGISTER_DUPLICATION   ON
set_global_assignment   -name ALLOW_REGISTER_RETIMING      ON

set_global_assignment   -name REMOVE_REDUNDANT_LOGIC_CELLS OFF
set_global_assignment   -name REMOVE_DUPLICATE_REGISTERS   ON

set_global_assignment   -name MIN_CORE_JUNCTION_TEMP 0
set_global_assignment   -name MAX_CORE_JUNCTION_TEMP 85
set_global_assignment   -name ERROR_CHECK_FREQUENCY_DIVISOR 256
set_global_assignment   -name PARTITION_NETLIST_TYPE SOURCE -section_id Top
set_global_assignment   -name PARTITION_FITTER_PRESERVATION_LEVEL PLACEMENT_AND_ROUTING -section_id Top
set_global_assignment   -name PARTITION_COLOR 16764057 -section_id Top
set_instance_assignment -name PARTITION_HIERARCHY root_partition -to | -section_id Top

# Clock
set_global_assignment   -name SDC_FILE ../scripts/design.sdc

# Pin assignments
proc my_pin_assignment {PORT_NAME PIN_NAME IO_STD} {
    set_location_assignment $PIN_NAME                 -to $PORT_NAME
    set_instance_assignment -name IO_STANDARD $IO_STD -to $PORT_NAME
}

my_pin_assignment   FPGA_CLK1_50     PIN_V11    "3.3-V LVTTL"
my_pin_assignment   FPGA_CLK2_50     PIN_Y13    "3.3-V LVTTL"
my_pin_assignment   FPGA_CLK3_50     PIN_E11    "3.3-V LVTTL"

my_pin_assignment   KEY[0]           PIN_AH17   "3.3-V LVTTL"
my_pin_assignment   KEY[1]           PIN_AH16   "3.3-V LVTTL"

my_pin_assignment   SW[0]            PIN_L10    "3.3-V LVTTL"
my_pin_assignment   SW[1]            PIN_L9     "3.3-V LVTTL"
my_pin_assignment   SW[2]            PIN_H6     "3.3-V LVTTL"
my_pin_assignment   SW[3]            PIN_H5     "3.3-V LVTTL"

my_pin_assignment   LED[0]           PIN_W15    "3.3-V LVTTL"
my_pin_assignment   LED[1]           PIN_AA24   "3.3-V LVTTL"
my_pin_assignment   LED[2]           PIN_V16    "3.3-V LVTTL"
my_pin_assignment   LED[3]           PIN_V15    "3.3-V LVTTL"
my_pin_assignment   LED[4]           PIN_AF26   "3.3-V LVTTL"
my_pin_assignment   LED[5]           PIN_AE26   "3.3-V LVTTL"
my_pin_assignment   LED[6]           PIN_Y16    "3.3-V LVTTL"
my_pin_assignment   LED[7]           PIN_AA23   "3.3-V LVTTL"

my_pin_assignment   GPIO_0[0]        PIN_V12    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[1]        PIN_AF7    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[2]        PIN_W12    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[3]        PIN_AF8    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[4]        PIN_Y8     "3.3-V LVTTL"
my_pin_assignment   GPIO_0[5]        PIN_AB4    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[6]        PIN_W8     "3.3-V LVTTL"
my_pin_assignment   GPIO_0[7]        PIN_Y4     "3.3-V LVTTL"
my_pin_assignment   GPIO_0[8]        PIN_Y5     "3.3-V LVTTL"
my_pin_assignment   GPIO_0[9]        PIN_U11    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[10]       PIN_T8     "3.3-V LVTTL"
my_pin_assignment   GPIO_0[11]       PIN_T12    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[12]       PIN_AH5    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[13]       PIN_AH6    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[14]       PIN_AH4    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[15]       PIN_AG5    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[16]       PIN_AH3    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[17]       PIN_AH2    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[18]       PIN_AF4    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[19]       PIN_AG6    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[20]       PIN_AF5    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[21]       PIN_AE4    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[22]       PIN_T13    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[23]       PIN_T11    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[24]       PIN_AE7    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[25]       PIN_AF6    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[26]       PIN_AF9    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[27]       PIN_AE8    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[28]       PIN_AD10   "3.3-V LVTTL"
my_pin_assignment   GPIO_0[29]       PIN_AE9    "3.3-V LVTTL"
my_pin_assignment   GPIO_0[30]       PIN_AD11   "3.3-V LVTTL"
my_pin_assignment   GPIO_0[31]       PIN_AF10   "3.3-V LVTTL"
my_pin_assignment   GPIO_0[32]       PIN_AD12   "3.3-V LVTTL"
my_pin_assignment   GPIO_0[33]       PIN_AE11   "3.3-V LVTTL"
my_pin_assignment   GPIO_0[34]       PIN_AF11   "3.3-V LVTTL"
my_pin_assignment   GPIO_0[35]       PIN_AE12   "3.3-V LVTTL"

my_pin_assignment   GPIO_1[0]        PIN_Y15    "3.3-V LVTTL"
my_pin_assignment   GPIO_1[1]        PIN_AG28   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[2]        PIN_AA15   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[3]        PIN_AH27   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[4]        PIN_AG26   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[5]        PIN_AH24   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[6]        PIN_AF23   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[7]        PIN_AE22   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[8]        PIN_AF21   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[9]        PIN_AG20   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[10]       PIN_AG19   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[11]       PIN_AF20   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[12]       PIN_AC23   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[13]       PIN_AG18   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[14]       PIN_AH26   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[15]       PIN_AA19   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[16]       PIN_AG24   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[17]       PIN_AF25   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[18]       PIN_AH23   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[19]       PIN_AG23   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[20]       PIN_AE19   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[21]       PIN_AF18   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[22]       PIN_AD19   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[23]       PIN_AE20   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[24]       PIN_AE24   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[25]       PIN_AD20   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[26]       PIN_AF22   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[27]       PIN_AH22   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[28]       PIN_AH19   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[29]       PIN_AH21   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[30]       PIN_AG21   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[31]       PIN_AH18   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[32]       PIN_AD23   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[33]       PIN_AE23   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[34]       PIN_AA18   "3.3-V LVTTL"
my_pin_assignment   GPIO_1[35]       PIN_AC22   "3.3-V LVTTL"

my_pin_assignment   ARDUINO_IO[0]    PIN_AG13   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[1]    PIN_AF13   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[2]    PIN_AG10   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[3]    PIN_AG9    "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[4]    PIN_U14    "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[5]    PIN_U13    "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[6]    PIN_AG8    "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[7]    PIN_AH8    "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[8]    PIN_AF17   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[9]    PIN_AE15   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[10]   PIN_AF15   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[11]   PIN_AG16   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[12]   PIN_AH11   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[13]   PIN_AH12   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[14]   PIN_AH9    "3.3-V LVTTL"
my_pin_assignment   ARDUINO_IO[15]   PIN_AG11   "3.3-V LVTTL"
my_pin_assignment   ARDUINO_RESET_N  PIN_AH7    "3.3-V LVTTL"

my_pin_assignment   ADC_CONVST       PIN_U9     "3.3-V LVTTL"
my_pin_assignment   ADC_SCK          PIN_V10    "3.3-V LVTTL"
my_pin_assignment   ADC_SDI          PIN_AC4    "3.3-V LVTTL"
my_pin_assignment   ADC_SDO          PIN_AD4    "3.3-V LVTTL"

# --- Debug-transport pad settings on the GPIO_1 (JP7) DTM pins ----------------
# Cyclone V I/O offers a programmable weak pull-UP only (no internal pull-down).
# DTM_TYPE is read straight from the RTL parameter so the two can't drift.
#   0 = JTAG : IEEE 1149.1 pull-ups on TMS/TDI/TRST_N (+ TDO, floats when released)
#   1 = UART : pull-up on RX so a disconnected line reads idle-high (TX is push-pull)
#   2 = I2C  : SCL/SDA are open-drain and NON-FUNCTIONAL without a pull-up
#   3 = cJTAG: bus-hold (keeper) on TCKC/TMSC; TMSC is undriven while TCKC is high

# Extract the decimal value of a Verilog `parameter <name> = <n>;` from a source
# file. Ignores comments (`//...`), instantiation lines (`.name(...)`) and any
# `;` before the name. Returns $default if the file or parameter is not found.
proc verilog_param {file name default} {
    if {[catch {open $file r} fh]} {
        post_message -type warning "verilog_param: cannot open $file - using $name=$default"
        return $default
    }
    set txt [read $fh]
    close $fh
    set pat "parameter\\y\[^;/=\]*\\y${name}\\y\\s*=\\s*(\[0-9\]+)"
    if {[regexp -line $pat $txt -> val]} { return $val }
    post_message -type warning "verilog_param: $name not found in $file - using $default"
    return $default
}

# cwd at run time is synthesis/altera/WORK (see 0_create_bitstream.sh), so the
# top-level RTL is three levels up - the same path used for SEARCH_PATH above.
set DTM_TYPE [verilog_param ../../../rtl/verilog/arvern_fpga.v DTM_TYPE 0]
post_message "DTM debug transport: DTM_TYPE=$DTM_TYPE (from arvern_fpga.v)"

if {$DTM_TYPE == 3} {
    set_instance_assignment -name ENABLE_BUS_HOLD_CIRCUITRY ON -to GPIO_1[11]  ;# CJTAG_TMSC
    set_instance_assignment -name ENABLE_BUS_HOLD_CIRCUITRY ON -to GPIO_1[10]  ;# CJTAG_TCKC

} elseif {$DTM_TYPE == 2} {
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[7]   ;# I2C_SCL
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[8]   ;# I2C_SDA

} elseif {$DTM_TYPE == 1} {
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[9]   ;# UART_RX

} elseif {$DTM_TYPE == 0} {
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[1]   ;# JTAG_TMS
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[2]   ;# JTAG_TDI
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[3]   ;# JTAG_TRST_N
    set_instance_assignment -name WEAK_PULL_UP_RESISTOR     ON -to GPIO_1[4]   ;# JTAG_TDO
    # GPIO_1[0]/TCK ideally idles quiet (pull-down): not available internally on
    # Cyclone V -> add an external resistor if a defined level is required.
}

# Commit assignments
export_assignments


# Run synthesis
execute_flow -compile


# Close project
#project_close

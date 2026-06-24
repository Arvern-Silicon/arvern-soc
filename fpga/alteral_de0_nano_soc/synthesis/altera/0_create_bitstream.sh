#!/bin/bash
#----------------------------------------------------------------------------
# 0_create_bitstream.sh <test> : build the FPGA bitstream for a software app.
#
# Quartus is invoked through the $QUARTUS_PFX prefix, glued directly in front of
# the tool name:   ${QUARTUS_PFX}quartus_sh -t ...
#   - Native Quartus on PATH : leave QUARTUS_PFX empty            -> quartus_sh
#   - Quartus NOT on PATH    : set QUARTUS_PFX to its bin dir, with a trailing '/'
#                              (e.g. /opt/intelFPGA/22.1/quartus/bin/)
#----------------------------------------------------------------------------

# Cleanup
rm -rf ./WORK
mkdir WORK

###############################################################################
#                            Parameter Check                                  #
###############################################################################
EXPECTED_ARGS=1
if [ $# -ne $EXPECTED_ARGS ]; then
    echo ""
    echo "ERROR          : wrong number of arguments"
    echo "USAGE          : ./0_create_bitstream.sh <test name>"
    echo "EXAMPLE        : ./0_create_bitstream.sh    leds"
    echo ""
    echo "AVAILABLE TESTS:"
    for fullfile in ../../software/apps/* ; do
	filename=$(basename "$fullfile")
	filename="${filename%.*}"
	echo "                  - $filename"
    done
    echo ""
  exit 1
fi

###############################################################################
#                     Check if the required files exist                       #
###############################################################################
softdir=../../software/apps/$1;
ihexfile=../../software/apps/$1/$1.ihex;

if [ ! -e $softdir ]; then
    echo "Software directory doesn't exist: $softdir"
    exit 1
fi

###############################################################################
#                    Generate toolchain config from RTL                       #
###############################################################################
echo ""
echo " -----------------------------------------------"
echo "|  GENERATE TOOLCHAIN CONFIG"
echo " -----------------------------------------------"

../../sim/rtl_sim/bin/gen_march_config.sh ../../rtl/verilog/arvern_fpga.v ../../sim/rtl_sim/run/march_config.sh
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to generate march_config.sh"
    exit 1
fi

###############################################################################
#                           Compile program                                   #
###############################################################################
echo ""
echo " -----------------------------------------------"
echo "|  COMPILE PROGRAM: $1"
echo " -----------------------------------------------"
echo ""

cd $softdir
make clean
make
cd ../../../synthesis/altera

###############################################################################
#                           Generate MEM file                                 #
###############################################################################
echo ""
echo " -----------------------------------------------"
echo "|  GENERATE MEM FILE: $1"
echo " -----------------------------------------------"
echo ""

cd ./WORK

# Generate memory file
IHEX2MEM=../../../../../../arvern/sim/rtl_sim/bin/ihex2mem.py
python3 $IHEX2MEM -i ../$ihexfile -o pmem.mem -b 0x20000000 -s 32768

echo "New MEM file generated:"
echo "                          ./WORK/pmem.mem"
echo ""

###############################################################################
#                       Generate per-bank MIF files                           #
###############################################################################
echo ""
echo " -----------------------------------------------"
echo "|  GENERATE MIF FILES"
echo " -----------------------------------------------"
echo ""

MEM2MIF=../scripts/mem2mif.py
python3 $MEM2MIF -i pmem.mem -o pmem -s 2048 -n 4

echo ""

###############################################################################
#              Generate Quartus source assignments from filelist              #
###############################################################################
# Regenerate ../scripts/design_files.qsf from the single-source design filelist
# (filelist_core.f), replacing the formerly hand-maintained design_rtl.v.
FLATTEN=../../../../../../arvern/sim/rtl_sim/bin/flatten_filelist.py
CORE_FILELIST=../../../rtl/verilog/filelist.f
python3 "$FLATTEN" --format qsf --relative-to "$(pwd)" "$CORE_FILELIST" ../scripts/design_files.qsf

echo ""

###############################################################################
#                           Generate bitstream                                #
###############################################################################
echo ""
echo " -----------------------------------------------"
echo "|  GENERATE NEW BITSTREAM (SOF FILE)"
echo " -----------------------------------------------"
echo ""

# Native mode (empty QUARTUS_PFX) needs quartus_sh on PATH;
# otherwise the user must set the QUARTUS_PFX environment variable to the right path
if [ -z "$QUARTUS_PFX" ] && ! command -v quartus_sh >/dev/null 2>&1; then
    echo "ERROR: quartus_sh not found on PATH and QUARTUS_PFX is empty."
    exit 1
fi

# FPGA flow (i.e. generate SOF file). quartus_sh has no --log option, so capture
# stdout+stderr with tee (shown live, and saved into the WORK dir).
${QUARTUS_PFX}quartus_sh -t ../scripts/synthesis.tcl 2>&1 | tee ./quartus_synthesis.log

# Generate detailed timing report (top 20 critical paths)
${QUARTUS_PFX}quartus_sta -t ../scripts/report_timing.tcl

cd ..
mkdir -p bitstreams
cp -f ./WORK/output_files/arvern_fpga.sof ./bitstreams/$1.sof

echo ""
echo "New SOF file generated:"
echo "                        ./bitstreams/$1.sof"
echo ""
echo "Timing report:"
echo "                        ./WORK/output_files/arvern_fpga.critical_paths.rpt"
echo ""

###############################################################################
#                  Relevant synthesis warnings (filtered)                     #
###############################################################################
echo " -----------------------------------------------"
echo "|  QUARTUS WARNINGS  (excluding _unused signals)"
echo " -----------------------------------------------"
echo ""

# Surface Quartus warnings worth a look, hiding the intentional *_unused
# lint-cleanup signals. (cwd is the altera dir here; the log lives in WORK.)
# Captured into a variable so an empty result doesn't make the script exit
# non-zero (grep returns 1 when nothing matches).
SYNTH_LOG=WORK/quartus_synthesis.log
if [ -f "$SYNTH_LOG" ]; then
    warnings=$(grep "Warning" "$SYNTH_LOG" | grep -v "_unused")
    if [ -n "$warnings" ]; then
        echo "$warnings"
    else
        echo "  (none beyond the expected _unused lint-cleanup signals)"
    fi
else
    echo "  (synthesis log not found: $SYNTH_LOG)"
fi
echo ""

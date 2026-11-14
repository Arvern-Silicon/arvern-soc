#!/bin/bash
#------------------------------------------------------------------------------
# Build the FPGA bitstream for one firmware app:  ./0_create_bitstream.sh <app> [hdmi|lcd]
# The firmware (software/apps/<app>, hardware build) is the program memory's
# initial image. Gowin EDA: gw_sh on PATH, or GOWIN_HOME set to the install
# (on macOS: /Applications/GowinIDE.app/Contents/Resources/Gowin_EDA).
# The display: "hdmi" (the default: BOARD_HDMI=1, TV or monitor on J13, 720 x 480)
# or "lcd" (the RGB panel); the same firmware runs on both.
# Result: ./bitstreams/<app>.fs (<app>_hdmi.fs), reports in ./WORK/arvern_fpga/impl/pnr/; the
# Gowin IDE opens the run as ./WORK/arvern_fpga/arvern_fpga.gprj.
# GOWIN_PLACE_OPTION picks the placement algorithms (0-4, default "1 0 3 4":
# the set that meets timing on both displays); several run in parallel, each
# in ./WORK_po<n>, and the best kept becomes ./WORK: the one meeting every
# clock with the highest sys_clk Fmax (else the highest sys_clk Fmax, reported
# as failing). A run that routes for longer than GOWIN_RUN_MINUTES (default
# 120) is stopped. Gowin's results vary from run to run: a build that fails
# timing is worth running again before changing anything.
#------------------------------------------------------------------------------

if [ $# -lt 1 ] || [ $# -gt 2 ] || { [ $# -eq 2 ] && [ "$2" != "hdmi" ] && [ "$2" != "lcd" ]; }; then
    echo "Usage: $0 <app> [hdmi|lcd]  (default hdmi)  with <app> one of:"
    for d in ../../software/apps/*/ ; do [ -f "$d/Makefile" ] && echo "    - $(basename $d)"; done
    exit 1
fi
APP=$1
SUFFIX=
if [ "${2:-hdmi}" = "hdmi" ]; then
    export GOWIN_PARAM_OVERRIDES="${GOWIN_PARAM_OVERRIDES:+$GOWIN_PARAM_OVERRIDES }BOARD_HDMI=1"
    SUFFIX=_hdmi
fi
softdir=../../software/apps/$APP
if [ ! -d $softdir ]; then
    echo "ERROR: no app $APP"; exit 1
fi

# Gowin EDA: gw_sh and its libraries. The DYLD_* variables are always set: macOS
# strips them whenever a system binary (such as /bin/bash) starts
GOWIN_HOME=${GOWIN_HOME:-/Applications/GowinIDE.app/Contents/Resources/Gowin_EDA}
if ! command -v gw_sh >/dev/null 2>&1; then
    if [ ! -x "$GOWIN_HOME/IDE/bin/gw_sh" ]; then
        echo "ERROR: gw_sh not found (set GOWIN_HOME or put gw_sh on PATH)"; exit 1
    fi
    export PATH="$GOWIN_HOME/IDE/bin:$PATH"
fi
export DYLD_FRAMEWORK_PATH="$GOWIN_HOME/IDE/lib" DYLD_LIBRARY_PATH="$GOWIN_HOME/IDE/lib"
export LD_LIBRARY_PATH="$GOWIN_HOME/IDE/lib:$LD_LIBRARY_PATH"

echo " -----------------------------------------------"
echo "|  Firmware: $APP"
echo " -----------------------------------------------"
../../sim/rtl_sim/bin/gen_march_config.sh ../../rtl/verilog/arvern_fpga.v ../../sim/rtl_sim/run/march_config.sh || exit 1
( cd $softdir && make clean > /dev/null && make ) || { echo "ERROR: firmware build failed"; exit 1; }

# A run directory (./WORK or ./WORK_po<n>): the firmware image and the filelist
prepare_work() {
    rm -rf ./$1 && mkdir ./$1 && (
        cd ./$1 || exit 1
        IHEX2MEM=../../../../../../arvern/sim/rtl_sim/bin/ihex2mem.py
        ROM_KB=$(sed -n 's/^parameter  *ROM_SIZE  *= *\([0-9]*\)\*1024.*/\1/p' ../../../rtl/verilog/arvern_fpga.v)   # program memory size (arvern_fpga.v)
        [ -n "$ROM_KB" ] || { echo "ERROR: no ROM_SIZE in arvern_fpga.v"; exit 1; }
        python3 $IHEX2MEM -i ../$softdir/$APP.ihex -o pmem.mem -b 0x20000000 -s $((ROM_KB * 1024)) || exit 1
        python3 ../../../sim/rtl_sim/bin/mem_banks.py pmem.mem --banks $((ROM_KB / 8)) > /dev/null || exit 1
        FLATTEN=../../../../../../arvern/sim/rtl_sim/bin/flatten_filelist.py
        python3 "$FLATTEN" --format raw --relative-to "$(pwd)" ../../../rtl/verilog/filelist.f design_files.f || exit 1
    )
}

OPTS=(${GOWIN_PLACE_OPTION:-1 0 3 4 2})
echo " -----------------------------------------------"
echo "|  Synthesis, place & route, bitstream"
echo " -----------------------------------------------"
if [ ${#OPTS[@]} -le 1 ]; then
    prepare_work WORK || exit 1
    ( cd ./WORK && gw_sh ../scripts/synthesis.tcl 2>&1 | tee ./gowin_synthesis.log )
else
    LIMIT=$(( ${GOWIN_RUN_MINUTES:-120} * 60 ))
    PIDS=()
    for o in "${OPTS[@]}"; do
        prepare_work WORK_po$o || exit 1
        ( cd ./WORK_po$o && GOWIN_PLACE_OPTION=$o exec gw_sh ../scripts/synthesis.tcl > ./gowin_synthesis.log 2>&1 ) &
        PIDS+=($!)
        echo "  place_option $o: ./WORK_po$o/gowin_synthesis.log"
    done
    kill_tree() { local c; for c in $(pgrep -P $1); do kill_tree $c; done; kill $1 2>/dev/null; }
    END=$(( $(date +%s) + LIMIT ))
    ( while [ $(date +%s) -lt $END ]; do sleep 20; done; for p in "${PIDS[@]}"; do kill_tree $p; done ) &
    WATCHDOG=$!
    wait "${PIDS[@]}"
    kill $WATCHDOG 2>/dev/null
    BEST=; BEST_KEY=
    for o in "${OPTS[@]}"; do
        pnr=./WORK_po$o/arvern_fpga/impl/pnr
        if [ ! -f $pnr/arvern_fpga.fs ]; then echo "  place_option $o: no bitstream"; continue; fi
        sum=$(python3 ./scripts/timing_summary.py $pnr)
        fmax=$(echo "$sum" | sed -n 's/^ *sys_clk: Fmax \([0-9.]*\).*/\1/p')
        if echo "$sum" | grep -q FAILED; then met=0; else met=1; fi
        echo "  place_option $o: sys_clk Fmax $fmax MHz, $( [ $met = 1 ] && echo met || echo FAILED )"
        key="$met $fmax"
        if [ -z "$BEST" ] || [ "$(printf '%s\n%s\n' "$key" "$BEST_KEY" | sort -k1,1n -k2,2n | tail -1)" = "$key" ]; then
            BEST=$o; BEST_KEY=$key
        fi
    done
    [ -n "$BEST" ] || { echo "ERROR: no run produced a bitstream"; exit 1; }
    echo "  kept: place_option $BEST"
    rm -rf ./WORK && mv ./WORK_po$BEST ./WORK
fi
if [ ! -f ./WORK/arvern_fpga/impl/pnr/arvern_fpga.fs ]; then
    echo "ERROR: no bitstream (see WORK/gowin_synthesis.log)"; exit 1
fi

mkdir -p bitstreams
cp -f ./WORK/arvern_fpga/impl/pnr/arvern_fpga.fs ./bitstreams/$APP$SUFFIX.fs
python3 ./scripts/timing_summary.py ./WORK/arvern_fpga/impl/pnr
echo ""
echo "Bitstream: ./bitstreams/$APP$SUFFIX.fs"

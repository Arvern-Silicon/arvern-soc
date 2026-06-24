#!/bin/bash
# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      rtlsim.sh
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------

###############################################################################
#                            Parameter Check                                  #
###############################################################################
if [ $# -ne 3 ]; then
  echo "ERROR    : wrong number of arguments"
  echo "USAGE    : rtlsim.sh <top module> <stimulus file> <submit file>"
  echo "Example  : rtlsim.sh tb_arvern_fpga ./stimulus.v ../src/submit.f"
  exit 1
fi

###############################################################################
#                     Check if the required files exist                       #
###############################################################################

if [ ! -e $2 ]; then
    echo "Verilog stimulus file $2 doesn't exist"
    exit 1
fi
if [ ! -e $3 ]; then
    echo "Verilog submit file $3 doesn't exist"
    exit 1
fi

###############################################################################
#              Flatten the submit filelist (resolve nested -f)                #
###############################################################################
# Resolve the IPs' nested -f includes (e.g. the shared arv_common library) to
# absolute paths and drop duplicate sources, so the simulator sees a clean,
# self-contained list regardless of cwd. Falls back to the raw file if the
# flattener can't be found.
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
FLATTEN="$SCRIPT_DIR/../../../../../../arvern/sim/rtl_sim/bin/flatten_filelist.py"
SUBMIT_FLAT="./submit_sim.f"
if [ -f "$FLATTEN" ]; then
    python3 "$FLATTEN" "$3" "$SUBMIT_FLAT"
else
    echo "Warning: flatten_filelist.py not found at $FLATTEN; using raw submit file."
    SUBMIT_FLAT="$3"
fi

###############################################################################
#                         Start verilog simulation                            #
###############################################################################

if [ "${VERILOG_SIMULATOR:-iverilog}" = iverilog ]; then

    rm -rf simv

    NODUMP=${SIMULATION_NODUMP-0}

    DEFINES=""
    if [ $NODUMP -eq 1 ]; then
        DEFINES="$DEFINES -D NODUMP"
    fi
    if [ -n "$SIMULATION_DEFINES" ]; then
        DEFINES="$DEFINES $SIMULATION_DEFINES"
    fi

    iverilog -o simv -s $1 -c $SUBMIT_FLAT $DEFINES

    echo "Running simulation with: Icarus Verilog (iverilog)"

    if [[ $(uname -s) == CYGWIN* ]]; then
        vvp.exe ./simv
    else
        ./simv
    fi

else

    NODUMP=${SIMULATION_NODUMP-0}
    vargs=""
    if [ $NODUMP -eq 1 ]; then
        vargs="$vargs +define+NODUMP"
    fi

   case $VERILOG_SIMULATOR in
    ncverilog* )
       rm -rf INCA_libs
       vargs="$vargs +access+r +nclicq +define+TRN_FILE" ;;
    vcs* )
       rm -rf csrc simv*
       vargs="$vargs -lca -debug_access+all -sverilog +define+VPD_FILE" ;;
    vsim* )
       if [ -d work ]; then vdel -all; fi
       vlib work
       echo "Running simulation with: Modelsim (vsim)"
       exec vlog +acc=prn -f $SUBMIT_FLAT $vargs -R -c -do "run -all" ;;
   esac

   echo "Running simulation with: $VERILOG_SIMULATOR"
   exec $VERILOG_SIMULATOR -f $SUBMIT_FLAT $vargs
fi

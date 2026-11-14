#!/bin/bash
#------------------------------------------------------------------------------
# Write a bitstream to the configuration flash (the FPGA boots it at
# power-on):   ./2_program_flash.sh <app>        writes ./bitstreams/<app>.fs
# Through the board's USB port marked MCU (onboard BL616 USB-JTAG), with the
# Gowin Programmer's command line (programmer_cli, operation 54: exFlash
# Erase,Program,Verify Arora V). Read-only checks:
#     ./2_program_flash.sh --detect    the flash ID (operation 51, exFlash Detect ID)
#     ./2_program_flash.sh --id        the FPGA ID code (operation 0, Read Device Codes)
#     ./2_program_flash.sh --status    the configuration status (operation 74)
# USE_OFL=1 uses openFPGALoader instead (Homebrew: brew install openfpgaloader).
#------------------------------------------------------------------------------

GOWIN_HOME=${GOWIN_HOME:-/Applications/GowinIDE.app/Contents/Resources/Gowin_EDA}
PROGRAMMER=$GOWIN_HOME/Programmer/bin/programmer_cli
DEVICE=GW5AST-138${GOWIN_DEVICE_VERSION:-C}   # the device version, as in synthesis.tcl

run_gowin() {   # programmer_cli <operation> [<.fs>]
    if [ ! -x "$PROGRAMMER" ]; then
        echo "ERROR: programmer_cli not found (set GOWIN_HOME)"; exit 1
    fi
    export DYLD_FRAMEWORK_PATH="$GOWIN_HOME/Programmer/lib:$GOWIN_HOME/IDE/lib"
    export DYLD_LIBRARY_PATH="$GOWIN_HOME/Programmer/lib:$GOWIN_HOME/IDE/lib"
    "$PROGRAMMER" --device "$DEVICE" --run "$1" ${2:+--fsFile "$2"}
}

case "$1" in
    --detect) run_gowin 51; exit $? ;;
    --id)     run_gowin 0;  exit $? ;;
    --status) run_gowin 74; exit $? ;;
esac

if [ $# -lt 1 ] || [ $# -gt 2 ] || { [ $# -eq 2 ] && [ "$2" != "hdmi" ] && [ "$2" != "lcd" ]; }; then
    echo "Usage: $0 <app> [hdmi|lcd]  (default hdmi; or a bitstream name ending in _hdmi)  with:"
    for f in ./bitstreams/*.fs; do [ -f "$f" ] && echo "    - $(basename $f .fs)"; done
    echo "       $0 --detect | --id | --status   (read-only checks)"
    exit 1
fi
# The display flavour: hdmi (default) or lcd; an app name already ending in
# _hdmi is taken as the bitstream's name
case "$1" in
    *_hdmi) FS=./bitstreams/$1.fs ;;
    *)      [ "${2:-hdmi}" = "lcd" ] && FS=./bitstreams/$1.fs || FS=./bitstreams/$1_hdmi.fs ;;
esac
if [ ! -f $FS ]; then
    echo "ERROR: $FS not found (./0_create_bitstream.sh ${1%_hdmi} ${2:-hdmi} builds it)"; exit 1
fi

if [ -n "$USE_OFL" ]; then
    command -v openFPGALoader >/dev/null 2>&1 || { echo "ERROR: openFPGALoader not found"; exit 1; }
    openFPGALoader -b tangconsole -f $FS
else
    run_gowin 54 "$(cd "$(dirname "$FS")" && pwd)/$(basename "$FS")"
fi

#!/bin/bash

###############################################################################
# 3_program_flash.sh
#
# Programs the DE0-Nano-SoC EPCS flash with a .jic previously produced by
# 2_generate_flash_file.sh, using quartus_pgm over the USB-Blaster II.
#
# IMPORTANT: quartus_pgm needs live JTAG/USB-Blaster access. It therefore does
# NOT work inside the dockerized Quartus flow on macOS (Docker on macOS cannot
# reach the host USB). Run this on a USB-capable host with Quartus (native
# Linux, or a VM with the USB-Blaster passed through).
#
# On a Windows machine, prefer the graphical route instead of this script:
#   install the free standalone "Quartus Prime Programmer", open <name>.cdf
#   (produced next to the .jic by 2_generate_flash_file.sh), and press Start.
###############################################################################

###############################################################################
#                            Parameter Check                                  #
###############################################################################
EXPECTED_ARGS=1
if [ $# -ne $EXPECTED_ARGS ]; then
    echo ""
    echo "ERROR          : wrong number of arguments"
    echo "USAGE          : ./3_program_flash.sh <bitstream name>"
    echo "EXAMPLE        : ./3_program_flash.sh    leds"
    echo ""
    echo "AVAILABLE JIC FILES (run ./2_generate_flash_file.sh first):"
    for fullfile in ./bitstreams/*.jic ; do
        [ -e "$fullfile" ] || continue
        filename=$(basename "$fullfile")
        filename="${filename%.*}"
        echo "                       - $filename"
    done
    echo ""
  exit 1
fi

###############################################################################
#                     Check if the required files exist                       #
###############################################################################
jicfile=bitstreams/$1.jic;

if [ ! -e $jicfile ]; then
    echo ""
    echo "ERROR: JIC file doesn't exist: $jicfile"
    echo "       Generate it first with: ./2_generate_flash_file.sh $1"
    echo ""
    exit 1
fi

###############################################################################
#                             Program FLASH                                   #
###############################################################################
echo ""
echo " ---------------------------------------------------------"
echo "|  PROGRAM FLASH: $jicfile"
echo " ---------------------------------------------------------"
echo ""
echo "Note: if failing:"
echo "                  - try killing 'jtagd', running 'jtagconfig'"
echo "                  - try as 'root'"
echo "                  - check dev usb permissions"
echo "                  - remember: this does NOT work through Docker on macOS"
echo ""

# Native mode (empty QUARTUS_PFX) needs quartus_pgm on PATH;
# otherwise the user must set the QUARTUS_PFX environment variable to the right path
if [ -z "$QUARTUS_PFX" ] && ! command -v quartus_pgm >/dev/null 2>&1; then
    echo "ERROR: quartus_pgm not found on PATH and QUARTUS_PFX is empty."
    exit 1
fi

# Copy and process CDF file (working copy referencing ../bitstreams/ from WORK)
cp scripts/chain_with_flash.cdf  ./bitstreams/.
sed -ie "s/BITSTREAM_NAME/$1/g"  ./bitstreams/chain_with_flash.cdf

# Program flash. NOTE: quartus_pgm needs JTAG/USB-Blaster access.
mkdir -p WORK
( cd WORK && ${QUARTUS_PFX}quartus_pgm ../bitstreams/chain_with_flash.cdf )

# Cleanup
rm -rf ./bitstreams/chain_with_flash.*

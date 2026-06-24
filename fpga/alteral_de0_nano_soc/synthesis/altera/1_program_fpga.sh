#!/bin/bash

###############################################################################
#                            Parameter Check                                  #
###############################################################################
EXPECTED_ARGS=1
if [ $# -ne $EXPECTED_ARGS ]; then
    echo ""
    echo "ERROR          : wrong number of arguments"
    echo "USAGE          : ./1_program_fpga.sh <bitstream name>"
    echo "EXAMPLE        : ./1_program_fpga.sh    leds"
    echo ""
    echo "AVAILABLE BITSTREAMS:"
    for fullfile in ./bitstreams/*.sof ; do
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
soffile=bitstreams/$1.sof;
svffile=bitstreams/$1.svf;

if [ ! -e $soffile ]; then
    echo ""
    echo "ERROR: Specified SOF file doesn't exist: $soffile"
    echo ""
    exit 1
fi

###############################################################################
#                          Convert SOF to SVF                                 #
###############################################################################
echo " -----------------------------------------------"
echo "|  CONVERT SOF TO SVF"
echo "|"
echo "|  $soffile --> $svffile"
echo "|"
echo " -----------------------------------------------"
echo ""

# Native mode (empty QUARTUS_PFX) needs quartus_cpf on PATH;
# otherwise the user must set the QUARTUS_PFX environment variable to the right path
if [ -z "$QUARTUS_PFX" ] && ! command -v quartus_cpf >/dev/null 2>&1; then
    echo "ERROR: quartus_cpf not found on PATH and QUARTUS_PFX is empty."
    exit 1
fi
mkdir -p WORK
( cd WORK && ${QUARTUS_PFX}quartus_cpf -c -q 24MHz -g 3.3 -n p ../$soffile ../$svffile )

###############################################################################
#                             Program FPGA                                    #
###############################################################################
echo ""
echo " -----------------------------------------------"
echo "|  PROGRAM FPGA: $svffile"
echo " -----------------------------------------------"
echo ""

#openFPGALoader -b de0nanoSoC $svffile
openFPGALoader -v -c usb-blasterII --probe-firmware ./scripts/blaster_6810.hex  $svffile

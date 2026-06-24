#!/bin/bash

###############################################################################
#                            Parameter Check                                  #
###############################################################################
EXPECTED_ARGS=1
if [ $# -ne $EXPECTED_ARGS ]; then
    echo ""
    echo "ERROR          : wrong number of arguments"
    echo "USAGE          : ./2_program_flash.sh <bitstream name>"
    echo "EXAMPLE        : ./2_program_flash.sh    leds"
    echo ""
    echo "AVAILABLE BITSTREAMS:"
    for fullfile in ./bitstreams/*.sof ; do
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
soffile=bitstreams/$1.sof;
jicfile=bitstreams/$1.jic;

if [ ! -e $soffile ]; then
    echo ""
    echo "ERROR: Specified SOF file doesn't exist: $soffile"
    echo ""
    exit 1
fi

###############################################################################
#                             Generate JIC file
###############################################################################
echo " ---------------------------------------------------------"
echo "|  GENERATE JIC FILE"
echo "|"
echo "|  $soffile --> $jicfile"
echo "|"
echo " ---------------------------------------------------------"
echo ""

# Native mode (empty QUARTUS_PFX) needs quartus_cpf on PATH;
# otherwise the user must set the QUARTUS_PFX environment variable to the right path
if [ -z "$QUARTUS_PFX" ] && ! command -v quartus_cpf >/dev/null 2>&1; then
    echo "ERROR: quartus_cpf not found on PATH and QUARTUS_PFX is empty."
    exit 1
fi

# Copy and process COF file (host-side, cwd = altera)
cp scripts/sof2jic.cof ./bitstreams/.
sed -ie "s/BITSTREAM_NAME/$1/g"  ./bitstreams/sof2jic.cof

# Convert SOF -> JIC
mkdir -p WORK
( cd WORK && ${QUARTUS_PFX}quartus_cpf -c ../bitstreams/sof2jic.cof )

# Cleanup
rm -rf ./bitstreams/sof2jic.cof*


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
echo ""

# Native mode (empty QUARTUS_PFX) needs quartus_pgm on PATH;
# otherwise the user must set the QUARTUS_PFX environment variable to the right path
if [ -z "$QUARTUS_PFX" ] && ! command -v quartus_pgm >/dev/null 2>&1; then
    echo "ERROR: quartus_pgm not found on PATH and QUARTUS_PFX is empty."
    exit 1
fi

# Copy and process CDF file
cp scripts/chain_with_flash.cdf  ./bitstreams/.
sed -ie "s/BITSTREAM_NAME/$1/g"  ./bitstreams/chain_with_flash.cdf

# Program flash. NOTE: quartus_pgm needs JTAG/USB-Blaster access.
mkdir -p WORK
( cd WORK && ${QUARTUS_PFX}quartus_pgm ../bitstreams/chain_with_flash.cdf )

# Cleanup
rm -rf ./bitstreams/chain_with_flash.*

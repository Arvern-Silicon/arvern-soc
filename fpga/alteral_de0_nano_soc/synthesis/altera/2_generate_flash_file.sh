#!/bin/bash

###############################################################################
# 2_generate_flash_file.sh
#
# Generates the files needed to program the DE0-Nano-SoC EPCS flash:
#   - bitstreams/<name>.jic  : JTAG Indirect Configuration file (self-contained;
#                              bundles the Serial Flash Loader for 5CSEMA4 + the
#                              bitstream). This is what quartus_pgm consumes.
#   - bitstreams/<name>.cdf  : a drop-in Chain Description File that points at
#                              <name>.jic in the SAME folder, so both files can
#                              be copied to a USB-capable machine and opened
#                              directly in the (free, standalone) Quartus Prime
#                              Programmer: File -> Open -> <name>.cdf -> Start.
#
# This step only CONVERTS files (no JTAG/USB access), so it runs fine inside the
# dockerized Quartus flow on macOS. The actual programming is done separately by
# 3_program_flash.sh (which needs live USB-Blaster access and therefore a
# USB-capable host).
###############################################################################

###############################################################################
#                            Parameter Check                                  #
###############################################################################
EXPECTED_ARGS=1
if [ $# -ne $EXPECTED_ARGS ]; then
    echo ""
    echo "ERROR          : wrong number of arguments"
    echo "USAGE          : ./2_generate_flash_file.sh <bitstream name>"
    echo "EXAMPLE        : ./2_generate_flash_file.sh    leds"
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
cdffile=bitstreams/$1.cdf;

if [ ! -e $soffile ]; then
    echo ""
    echo "ERROR: Specified SOF file doesn't exist: $soffile"
    echo ""
    exit 1
fi

###############################################################################
#                             Generate JIC file                               #
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

if [ ! -e $jicfile ]; then
    echo ""
    echo "ERROR: JIC was not generated: $jicfile"
    echo ""
    exit 1
fi

###############################################################################
#              Generate a drop-in Chain Description File (.cdf)               #
###############################################################################
# Same-folder .cdf (Path "./") so <name>.jic + <name>.cdf can be dropped into a
# single directory on the target machine and opened straight in the Programmer.
sed -e "s/BITSTREAM_NAME/$1/g" \
    -e 's#Path("../bitstreams/")#Path("./")#g' \
    scripts/chain_with_flash.cdf > $cdffile

echo ""
echo " ---------------------------------------------------------"
echo "|  DELIVERABLES (copy BOTH into one folder on the"
echo "|  programming machine):"
echo "|"
echo "|     $jicfile"
echo "|     $cdffile"
echo "|"
echo "|  To program (USB-capable host, e.g. a Windows PC):"
echo "|    1. Install the free standalone 'Quartus Prime Programmer'"
echo "|       (no full Quartus suite needed; includes USB-Blaster II drivers)."
echo "|    2. Plug in the board, open $1.cdf, press Start."
echo "|  Or, on a USB-capable host with Quartus: ./3_program_flash.sh $1"
echo " ---------------------------------------------------------"
echo ""

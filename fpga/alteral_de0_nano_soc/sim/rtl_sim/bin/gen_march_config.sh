#!/bin/bash
# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      gen_march_config.sh
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------
# Description: Parse RTL parameters from arvern_fpga.v and generate
#              march_config.sh with the matching MARCH strings and
#              toolchain configuration.
#
# Usage: gen_march_config.sh <rtl_file> <output_file>
# ----------------------------------------------------------------------------

RTL_FILE=$1
OUTPUT_FILE=$2

if [ -z "$RTL_FILE" ] || [ -z "$OUTPUT_FILE" ]; then
    echo "USAGE: gen_march_config.sh <rtl_file> <output_file>"
    exit 1
fi

if [ ! -e "$RTL_FILE" ]; then
    echo "ERROR: RTL file not found: $RTL_FILE"
    exit 1
fi

###############################################################################
#                    Parse RTL parameters                                     #
###############################################################################

# Extract parameter values from the Verilog source
get_param() {
    grep "parameter.*$1" "$RTL_FILE" | sed -n "s/.*$1[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p" | head -1
}

RV32E_EN=$(get_param    "RV32E_EN")
B_EXTENSION=$(get_param "B_EXTENSION")
C_EXTENSION=$(get_param "C_EXTENSION")
M_EXTENSION=$(get_param "M_EXTENSION")

# Default to 0 if not found
RV32E_EN=${RV32E_EN:-0}
B_EXTENSION=${B_EXTENSION:-0}
C_EXTENSION=${C_EXTENSION:-0}
M_EXTENSION=${M_EXTENSION:-0}

echo ""
echo " Parsing RTL configuration from: $(basename $RTL_FILE)"
echo ""
echo "   RV32E_EN    = $RV32E_EN"
echo "   B_EXTENSION = $B_EXTENSION"
echo "   C_EXTENSION = $C_EXTENSION"
echo "   M_EXTENSION = $M_EXTENSION"

###############################################################################
#                    Build MARCH strings                                       #
###############################################################################

# Base ISA and matching integer ABI. RV32E uses the reduced 16-register
# ilp32e ABI; RV32I uses ilp32 (no hard-float variants are built here).
if [ "$RV32E_EN" -eq 1 ]; then
    BASE="rv32e"
    MABI="ilp32e"
else
    BASE="rv32i"
    MABI="ilp32"
fi

# M extension (standard part of base string)
M_STR=""
if [ "$M_EXTENSION" -eq 2 ]; then
    M_STR="m"
fi

# Build extension list for standard mode
EXT=""

# Zmmul (multiply-only, when M_EXTENSION=1)
if [ "$M_EXTENSION" -eq 1 ]; then
    EXT="${EXT}_zmmul"
fi

# B extensions
if [ "$B_EXTENSION" -ge 1 ]; then
    EXT="${EXT}_zbb"
fi
if [ "$B_EXTENSION" -ge 2 ]; then
    EXT="${EXT}_zba"
fi
if [ "$B_EXTENSION" -ge 3 ]; then
    EXT="${EXT}_zbs"
fi
if [ "$B_EXTENSION" -ge 4 ]; then
    EXT="${EXT}_zbc"
fi

# Always add Zicsr
EXT="${EXT}_zicsr"

# MARCH_STD: no compressed instructions
MARCH_STD="${BASE}${M_STR}${EXT}"

# MARCH_COMP: with compressed instructions
if [ "$C_EXTENSION" -ge 1 ]; then
    C_EXT=""
    if [ "$C_EXTENSION" -ge 2 ]; then
        C_EXT="${C_EXT}_zcb"
    fi
    if [ "$C_EXTENSION" -ge 3 ]; then
        C_EXT="${C_EXT}_zcmp"
    fi
    if [ "$C_EXTENSION" -ge 4 ]; then
        C_EXT="${C_EXT}_zcmt"
    fi
    MARCH_COMP="${BASE}${M_STR}c${EXT}${C_EXT}"
else
    MARCH_COMP="${MARCH_STD}"
fi

echo ""
echo "   MARCH_STD   = $MARCH_STD"
echo "   MARCH_COMP  = $MARCH_COMP"
echo "   MABI        = $MABI"

###############################################################################
#                    Detect toolchain                                          #
###############################################################################

# Try xPacks toolchain first, then generic riscv64
if command -v riscv-none-elf-gcc &> /dev/null; then
    CROSS="riscv-none-elf"
elif command -v riscv64-unknown-elf-gcc &> /dev/null; then
    CROSS="riscv64-unknown-elf"
elif command -v riscv32-unknown-elf-gcc &> /dev/null; then
    CROSS="riscv32-unknown-elf"
else
    echo ""
    echo "WARNING: No RISC-V toolchain found in PATH, defaulting to riscv-none-elf"
    CROSS="riscv-none-elf"
fi

echo "   CROSS       = $CROSS"
echo ""

###############################################################################
#                    Generate march_config.sh                                  #
###############################################################################

# Human-readable extension descriptions
case $B_EXTENSION in
    0) B_DESC="none" ;;
    1) B_DESC="Zbb" ;;
    2) B_DESC="Zbb+Zba" ;;
    3) B_DESC="Zbb+Zba+Zbs" ;;
    4) B_DESC="Zbb+Zba+Zbs+Zbc" ;;
esac

case $C_EXTENSION in
    0) C_DESC="none" ;;
    1) C_DESC="Zca" ;;
    2) C_DESC="Zca+Zcb" ;;
    3) C_DESC="Zca+Zcb+Zcmp" ;;
    4) C_DESC="Zca+Zcb+Zcmp+Zcmt" ;;
esac

case $M_EXTENSION in
    0) M_DESC="none" ;;
    1) M_DESC="Zmmul" ;;
    2) M_DESC="M" ;;
esac

if [ "$RV32E_EN" -eq 1 ]; then
    ISA_DESC="RV32E"
else
    ISA_DESC="RV32I"
fi

cat > "$OUTPUT_FILE" << HEREDOC
#!/bin/bash
#==============================================================================
#
# Auto-generated MARCH and toolchain configuration
#
# Generated from RTL parameters in: $(basename $RTL_FILE)
#
# RTL Configuration:
#   RV32E_EN    = $RV32E_EN ($ISA_DESC)
#   C_EXTENSION = $C_EXTENSION ($C_DESC)
#   M_EXTENSION = $M_EXTENSION ($M_DESC)
#   B_EXTENSION = $B_EXTENSION ($B_DESC)
#
#==============================================================================

# MARCH for standard (non-compressed) instruction mode
MARCH_STD="$MARCH_STD"

# MARCH for compressed instruction mode
MARCH_COMP="$MARCH_COMP"

# Integer ABI (ilp32e for RV32E, ilp32 for RV32I)
MABI="$MABI"

# Toolchain configuration
CROSS="$CROSS"
TC_CC="${CROSS}-gcc"
TC_AS="${CROSS}-as"
TC_LD="${CROSS}-ld"
TC_OBJCOPY="${CROSS}-objcopy"
TC_OBJDUMP="${CROSS}-objdump"
TC_SIZE="${CROSS}-size"
HEREDOC

chmod +x "$OUTPUT_FILE"
echo " Generated: $OUTPUT_FILE"

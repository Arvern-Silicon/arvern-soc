#!/usr/bin/env bash
# Start a GDB server for this board on localhost:3333 over JTAG (FT232H).
# Needs the JTAG DTM build (DTM_TYPE=0, the board default); see README.md for
# the other transports. One front door, two backends:
#
#   ./run-gdbserver.sh                          # arvern-gdbserver (pure-Python, default)
#   ./run-gdbserver.sh --jtag-freq 15000000     #   ...crank TCK to 15 MHz
#   ./run-gdbserver.sh --listen localhost:3334  #   ...different port
#   ./run-gdbserver.sh --trace                  #   ...any other server flag passes through
#   ./run-gdbserver.sh --openocd                # OpenOCD via arvern-ft232h-gdb.cfg
#   ./run-gdbserver.sh --openocd -d3            #   ...extra args pass through to openocd
#
# Default backend: JTAG/FT232H defaults are set here and any arvern-gdbserver
# option AFTER them overrides (argparse keeps the last value).
# OpenOCD backend: TCK speed and gdb port live in the .cfg, so --jtag-freq /
# --listen apply to the DEFAULT backend only; edit the cfg to change them.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- OpenOCD backend: thin dispatch to the sibling config ---
if [ "${1:-}" = "--openocd" ]; then
  shift
  command -v openocd >/dev/null 2>&1 \
    || { echo "error: openocd not found on PATH (brew install open-ocd)" >&2; exit 1; }
  exec openocd -f "$HERE/arvern-ft232h-gdb.cfg" "$@"
fi

# --- default backend: arvern-gdbserver ---
# arvern-tools sits four levels up (sibling repo); override ARVERN_TOOLS if elsewhere.
ARVERN_TOOLS="${ARVERN_TOOLS:-$(cd "$HERE/../../../../arvern-tools" 2>/dev/null && pwd || true)}"

# Resolve the server: a PATH install first, else the in-repo virtualenv.
SERVER="$(command -v arvern-gdbserver 2>/dev/null || true)"
[ -n "$SERVER" ] || SERVER="$ARVERN_TOOLS/.venv/bin/arvern-gdbserver"
if [ ! -x "$SERVER" ]; then
  echo "error: arvern-gdbserver not found (tried PATH and $SERVER)" >&2
  echo "install it once:  pipx install --editable '$ARVERN_TOOLS'" >&2
  echo "or use the OpenOCD backend instead:  $0 --openocd" >&2
  exit 1
fi

exec "$SERVER" \
  --transport jtag --adapter ft232h --jtag-freq 1000000 --listen localhost:3333 \
  "$@"

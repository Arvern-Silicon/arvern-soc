#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    demo_prof.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Statistical profile of a demo on the board through a J-Link: load and start
# the ELF, wait, then halt / read the PC / resume N times, and print the share
# of samples per function (inlined functions count where they were inlined
# with --inline).
#
#   demo_prof.py [--after SECONDS] [-n SAMPLES] [--gap MS] [--lines] app
#----------------------------------------------------------------------------
import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

APPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps")


def elf_of(app):
    if app.endswith(".elf"):
        return os.path.abspath(app)
    name = os.path.basename(os.path.normpath(app))
    return os.path.abspath(os.path.join(APPS, name, name + ".elf"))


def main():
    ap = argparse.ArgumentParser(description="PC-sampling profile over J-Link")
    ap.add_argument("app")
    ap.add_argument("--after", type=float, default=5.0, help="seconds to run before sampling")
    ap.add_argument("-n", "--samples", type=int, default=300)
    ap.add_argument("--gap", type=int, default=13, help="ms between samples")
    ap.add_argument("--lines", action="store_true", help="also per source line")
    ap.add_argument("--speed", type=int, default=25000, help="JTAG clock in kHz")
    args = ap.parse_args()

    elf = elf_of(args.app)
    cmds = ["h", f"loadfile {elf}", "r", "g", f"sleep {int(args.after * 1000)}"]
    cmds += ["h", "g", f"sleep {args.gap}"] * args.samples
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as fh:
        fh.write("\n".join(cmds) + "\nqc\n")
        script = fh.name
    try:
        res = subprocess.run(["JLinkExe", "-device", "RISC-V", "-if", "JTAG", "-speed", str(args.speed),
                              "-jtagconf", "-1,-1", "-autoconnect", "1", "-nogui", "1", "-ExitOnError", "1",
                              "-CommandFile", script], capture_output=True, text=True)
    finally:
        os.unlink(script)
    pcs = re.findall(r"pc = ([0-9A-F]{8})", res.stdout)[1:]
    if not pcs:
        sys.stderr.write(res.stdout[-2000:])
        sys.exit("no samples")
    out = subprocess.run(["riscv-none-elf-addr2line", "-f", "-e", elf] + ["0x" + p for p in pcs],
                         capture_output=True, text=True).stdout.split("\n")
    fn = [out[2 * i] for i in range(len(pcs))]
    ln = [out[2 * i + 1].split("/")[-1].split(" ")[0] for i in range(len(pcs))]
    print(f"{os.path.basename(elf)}: {len(pcs)} samples after {args.after:g} s")
    for name, k in collections.Counter(fn).most_common(12):
        print(f"  {100.0 * k / len(pcs):5.1f}%  {name}")
    if args.lines:
        for name, k in collections.Counter(f"{f} {l}" for f, l in zip(fn, ln)).most_common(15):
            print(f"  {100.0 * k / len(pcs):5.1f}%  {name}")


if __name__ == "__main__":
    main()

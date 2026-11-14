#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    demo_perf.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Measure the Belenos demos on the board through a J-Link: load an ELF, reset,
# run it until a hardware breakpoint on demo_pass_done() halts it after one pass
# through its scenes (or for a fixed time with -s), then read the demo_kit load
# meter (demo_perf[]: frames, busy and total mtime ticks per scene). Prints the
# frame rate and the CPU load of every scene. SW[0] (hold) must be off.
#
#   demo_perf.py [-s SECONDS] [-t TIMEOUT] [--serial N] [--speed KHZ] app [app ...]
#
# 'app' is a directory under ../apps (demo_water) or an ELF path. Needs
# JLinkExe and riscv-none-elf-nm on PATH; the FPGA must already be programmed.
#----------------------------------------------------------------------------
import argparse
import os
import re
import subprocess
import sys
import tempfile

APPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps")
SCENES_MAX = 8
TIME_HZ = 5_000_000            # mtime rate, de0_board.h BOARD_TIME_HZ


def elf_of(app):
    if app.endswith(".elf"):
        return os.path.abspath(app)
    name = os.path.basename(os.path.normpath(app))
    return os.path.abspath(os.path.join(APPS, name, name + ".elf"))


def symbol(elf, name):
    out = subprocess.run(["riscv-none-elf-nm", elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    sys.exit(f"{elf}: no symbol {name}")


def jlink(cmds, args):
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as fh:
        fh.write("\n".join(cmds) + "\nqc\n")
        script = fh.name
    try:
        cmd = ["JLinkExe", "-device", "RISC-V", "-if", "JTAG", "-speed", str(args.speed),
               "-jtagconf", "-1,-1", "-autoconnect", "1", "-nogui", "1", "-ExitOnError", "1", "-CommandFile", script]
        if args.serial:
            cmd[1:1] = ["-USB", args.serial]
        res = subprocess.run(cmd, capture_output=True, text=True)
    finally:
        os.unlink(script)
    if res.returncode != 0:
        sys.stderr.write(res.stdout[-3000:] + res.stderr)
        sys.exit(f"JLinkExe failed (rc {res.returncode})")
    return res.stdout


def measure(app, args):
    elf = elf_of(app)
    addr = symbol(elf, "demo_perf")
    words = 3 * SCENES_MAX
    if args.seconds:
        run = [f"sleep {int(args.seconds * 1000)}", "h"]
    else:
        run = [f"WaitHalt {int(args.timeout * 1000)}"]
    out = jlink(["h", f"loadfile {elf}", "r"] +
                ([] if args.seconds else [f"SetBP 0x{symbol(elf, 'demo_pass_done'):08X} H"]) +
                ["g"] + run + ["h", f"mem32 0x{addr:08X}, {words}"], args)
    if not args.seconds and "halted" not in out.lower():
        sys.stderr.write(out[-2000:])
        sys.exit(f"{app}: no halt at demo_pass_done within {args.timeout} s")
    vals = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9A-Fa-f]{8})\s*=\s*((?:[0-9A-Fa-f]{8}\s*)+)$", line)
        if m and int(m.group(1), 16) >= addr:
            vals += [int(v, 16) for v in m.group(2).split()]
    if len(vals) < words:
        sys.stderr.write(out[-3000:])
        sys.exit(f"{app}: could not read demo_perf")
    rows = []
    for s in range(SCENES_MAX):
        frames, busy, total = vals[3 * s:3 * s + 3]
        if frames:
            rows.append((s + 1, frames, frames * TIME_HZ / total if total else 0.0,
                         100.0 * busy / total if total else 0.0))
    return os.path.basename(elf)[:-4], rows


def main():
    ap = argparse.ArgumentParser(description="Belenos demo load meter over J-Link")
    ap.add_argument("apps", nargs="+")
    ap.add_argument("-s", "--seconds", type=float, default=0.0,
                    help="run for a fixed time instead of one pass through the scenes")
    ap.add_argument("-t", "--timeout", type=float, default=300.0, help="longest pass, seconds (default 300)")
    ap.add_argument("--serial", default="", help="J-Link USB serial number")
    ap.add_argument("--speed", type=int, default=25000, help="JTAG clock in kHz (default 25000, the SDC limit)")
    args = ap.parse_args()

    print(f"{'demo':<18} {'scene':>5} {'frames':>7} {'fps':>6} {'load %':>7}")
    for app in args.apps:
        name, rows = measure(app, args)
        for scene, frames, fps, load in rows:
            print(f"{name:<18} {scene:>5} {frames:>7} {fps:>6.1f} {load:>7.1f}")
        sys.stdout.flush()


if __name__ == "__main__":
    main()

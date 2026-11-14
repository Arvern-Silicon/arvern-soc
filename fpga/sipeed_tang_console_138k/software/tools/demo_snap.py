#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    demo_snap.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Snapshot of the Belenos frame on the board through a J-Link: halt the CPU
# (now, or at a breakpoint), read the display registers, the frame the refresh
# shows and the palette, and write a PNG of the picture (frame-buffer
# orientation, pixel doubling applied). The CPU resumes afterwards.
#
#   demo_snap.py [-o out.png] [--elf app] [--load] [--break SYMBOL]
#                [--after SECONDS] [--frame N] [--scale K]
#
# --elf names the running program (an app directory under ../apps or an ELF):
# its pal[] (demo_kit) gives the colours of 8 bpp and lower frames; without it,
# or with --lut, they come from the LUT bank the refresh shows. --load loads and
# starts it first. --break halts at the first execution of SYMBOL after
# --after seconds (a hardware breakpoint). Only the CPU halts: the refresh and
# the GPU keep running.
#----------------------------------------------------------------------------
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

APPS      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps")
BLNS_BASE = 0x10050000         # tang_board.h BOARD_BLNS_BASE
VRAM_BASE = 0x82000000         # tang_board.h BOARD_VRAM_BASE
LUT_WIN   = 0x800              # LUT bank 0 in the register port (bank 1 at +0x400)
REG_NR    = 0x58 // 4          # BLNS_ID .. LUT_ROT


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
    return None


def jlink(cmds, args):
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as fh:
        fh.write("\n".join(cmds) + "\nqc\n")
        script = fh.name
    try:
        cmd = ["JLinkExe", "-device", "RISC-V", "-if", "JTAG", "-speed", str(args.speed),
               "-jtagconf", "-1,-1", "-autoconnect", "1", "-nogui", "1", "-ExitOnError", "1",
               "-CommandFile", script]
        res = subprocess.run(cmd, capture_output=True, text=True)
    finally:
        os.unlink(script)
    if res.returncode != 0:
        sys.stderr.write(res.stdout[-3000:] + res.stderr)
        sys.exit(f"JLinkExe failed (rc {res.returncode})")
    return res.stdout


def words(path):
    data = open(path, "rb").read()
    return list(struct.unpack("<%dI" % (len(data) // 4), data[:len(data) // 4 * 4]))


def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                 chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description="Belenos frame snapshot over J-Link")
    ap.add_argument("-o", "--out", default="snap.png")
    ap.add_argument("--elf", default="", help="running program (app name or ELF) for pal[] and symbols")
    ap.add_argument("--load", action="store_true", help="load and start --elf first")
    ap.add_argument("--break", dest="brk", default="", help="halt at this symbol instead of now")
    ap.add_argument("--after", type=float, default=0.0, help="seconds to run before arming --break")
    ap.add_argument("--timeout", type=float, default=60.0, help="longest wait for --break, seconds")
    ap.add_argument("--frame", type=int, default=-1, help="frame pointer to read (default: the shown one)")
    ap.add_argument("--lut", action="store_true", help="colours from the LUT, not pal[]")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--speed", type=int, default=25000, help="JTAG clock in kHz (default 25000)")
    args = ap.parse_args()

    elf = elf_of(args.elf) if args.elf else ""
    if (args.load or args.brk) and not elf:
        sys.exit("--load and --break need --elf")
    tmp = tempfile.mkdtemp()
    f_regs, f_vram, f_pal = (os.path.join(tmp, n) for n in ("regs.bin", "vram.bin", "pal.bin"))

    # 1) halt, then the registers
    cmds = []
    if args.load:
        cmds += ["h", f"loadfile {elf}", "r", "g"]
    if args.after:
        cmds += [f"sleep {int(args.after * 1000)}"]
    if args.brk:
        addr = symbol(elf, args.brk)
        if addr is None:
            sys.exit(f"{elf}: no symbol {args.brk}")
        cmds += ["h", f"SetBP 0x{addr:08X} H", "g", f"WaitHalt {int(args.timeout * 1000)}"]
    cmds += ["h", f"savebin {f_regs} 0x{BLNS_BASE:08X} 0x{REG_NR * 4:X}"]
    jlink(cmds, args)
    for attempt in range(8):
        r = words(f_regs)
        mode   = r[0x10 // 4] & 7
        size   = r[0x20 // 4]
        w, h   = size & 0xFFFF, size >> 16
        dbl    = (r[0x24 // 4] >> 4) & 1
        fsel   = r[0x30 // 4] & 3 if args.frame < 0 else args.frame
        fbase  = r[0x40 // 4 + fsel]
        lutcfg = r[0x50 // 4]
        bpp    = (1, 2, 4, 8, 16)[mode] if mode <= 4 else 8
        fw, fh = ((w + 1) // 2, (h + 1) // 2) if dbl else (w, h)
        nbytes = (fbase * bpp // 8) % 4 + (fw * fh * bpp + 7) // 8
        start  = VRAM_BASE + (fbase * bpp // 8) // 4 * 4

        # 2) the frame and the palette, then resume
        # a new session resumes the CPU: re-read the registers in the same halt as
        # the frame, and retry if the CPU flipped the frame in between
        f_regs2 = f_regs + ".2"
        cmds = ["h", f"savebin {f_regs2} 0x{BLNS_BASE:08X} 0x{REG_NR * 4:X}",
                f"savebin {f_vram} 0x{start:08X} 0x{(nbytes + 3) // 4 * 4:X}"]
        pal_addr = symbol(elf, "pal") if (elf and not args.lut) else None
        if bpp <= 8:
            if pal_addr is not None:
                cmds += [f"savebin {f_pal} 0x{pal_addr:08X} 0x400"]
            else:
                bank = (lutcfg >> 1) & 1
                cmds += [f"savebin {f_pal} 0x{BLNS_BASE + LUT_WIN + bank * 0x400:08X} 0x400"]
        cmds += ["g"]
        jlink(cmds, args)
        r2 = words(f_regs2)
        if all(r2[k // 4] == r[k // 4] for k in (0x10, 0x20, 0x24, 0x30, 0x40 + 4 * fsel)):
            break
        f_regs = f_regs2
    else:
        sys.exit("the frame kept changing between the two J-Link sessions")

    vram = open(f_vram, "rb").read()
    pal  = words(f_pal) if bpp <= 8 else []
    bit0 = (fbase * bpp) % 32
    rgb  = bytearray(w * h * 3)
    for y in range(h):
        sy = y // 2 if dbl else y
        for x in range(w):
            sx = x // 2 if dbl else x
            pos = bit0 + (sy * fw + sx) * bpp
            if bpp == 16:
                v = vram[pos // 8] | (vram[pos // 8 + 1] << 8)
                c = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
            else:
                v = (vram[pos // 8] >> (pos % 8)) & ((1 << bpp) - 1)
                p = pal[v]
                c = ((p >> 16) & 255, (p >> 8) & 255, p & 255)
            rgb[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(c)
    k = max(1, args.scale)
    if k > 1:
        big = bytearray(w * k * h * k * 3)
        for y in range(h * k):
            row = rgb[(y // k) * w * 3:(y // k + 1) * w * 3]
            line = b"".join(bytes(row[x * 3:x * 3 + 3]) * k for x in range(w))
            big[y * w * k * 3:(y + 1) * w * k * 3] = line
        rgb, w, h = big, w * k, h * k
    png(args.out, w, h, rgb)
    print(f"{args.out}: frame {fsel} @ pixel {fbase}, {bpp} bpp, "
          f"{w // k} x {h // k}{' doubled' if dbl else ''}, colours from "
          f"{'16 bpp' if bpp == 16 else ('pal[]' if pal_addr is not None else 'LUT')}")


if __name__ == "__main__":
    main()

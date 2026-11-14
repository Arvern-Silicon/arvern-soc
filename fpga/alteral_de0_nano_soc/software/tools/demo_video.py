#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    demo_video.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Video of a demo_kit demo on the board through a J-Link: load and start the
# ELF, halt at every frame wait (hardware breakpoints on blns_wait_frame and,
# when the demo has it, wait_fence_frame), wait for the GPU queue to drain,
# read the frame the refresh shows next with the LUT bank it uses (palette
# rotation applied), write it as a PNG and resume; stop after one pass through
# the scenes (demo_pass_done(), or the LED scene number wrapping) or after
# --max-frames, then assemble the PNGs with ffmpeg. The demo only slows down:
# a demo paced by its frame waits runs exactly as on the board, one PNG per
# refresh frame.
#
#   demo_video.py [-o out.mp4] [--fps 60] [--scale 2] [--max-frames N]
#                 [--frames DIR] [--keep] [--speed KHZ] app
#
# 'app' is a directory under ../apps or an ELF. Needs the pylink-square Python
# package (J-Link DLL), riscv-none-elf-nm and ffmpeg; the FPGA must already be
# programmed; the switches off (SW[2] puts the load meter on the LEDs). Every
# frame wait of the demo must be blns_wait_frame (demo_kit pal_show /
# show_frame / wait_frames(1)) or wait_fence_frame.
#----------------------------------------------------------------------------

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

import pylink

APPS      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps")
BLNS_BASE = 0x10050000         # de0_board.h BOARD_BLNS_BASE
VRAM_BASE = 0x82000000         # de0_board.h BOARD_VRAM_BASE
GPU_STAT  = BLNS_BASE + 0x104
REG_NR    = 0x58 // 4          # BLNS_ID .. LUT_ROT
LUT_WIN   = BLNS_BASE + 0x800  # LUT bank 0 in the register port (bank 1 at +0x400)
LED_CTRL  = 0x10040000         # de0_board.h BOARD_LKS_BASE: the scene number
CSR_DPC   = 0x7B1              # J-Link register index of a CSR = its number


def elf_of(app):
    if app.endswith(".elf"):
        return os.path.abspath(app)
    name = os.path.basename(os.path.normpath(app))
    return os.path.abspath(os.path.join(APPS, name, name + ".elf"))


def symbols(elf, names):
    out = subprocess.run(["riscv-none-elf-nm", elf], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 3 and f[2] in names:
            found[f[2]] = int(f[0], 16)
    return found


def load_segments(elf):
    """(address, bytes) of every PT_LOAD segment with file contents (ELF32 LE)"""
    data = open(elf, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit(f"{elf}: not a little-endian ELF32")
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    segs = []
    for i in range(phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", data, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            segs.append((p_paddr, data[p_offset:p_offset + p_filesz]))
    return segs


def png_rgb(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                 chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b""))


def palette(j, r, pal_addr):
    """The 256 colours of the next frame: the LUT bank REFR_BANK selects with
    LUT_ROT applied, or pal[] when the programmable LUT is off"""
    lutcfg, rot = r[0x50 // 4], r[0x54 // 4]
    if lutcfg & 1:
        lut = j.memory_read32(LUT_WIN + ((lutcfg >> 1) & 1) * 0x400, 256)
        first, last, off = rot & 255, (rot >> 8) & 255, (rot >> 16) & 255
        if first <= last:
            def look(v):
                if not first <= v <= last:
                    return v
                e = v + off
                return (e - (last - first + 1)) & 255 if e > last else e
            lut = [lut[look(v)] for v in range(256)]
    elif pal_addr is not None:
        lut = j.memory_read32(pal_addr, 256)
    else:
        lut = [v * 0x010101 for v in range(256)]
    return [bytes(((p >> 16) & 255, (p >> 8) & 255, p & 255)) for p in lut]


def frame(j, r, pal_addr):
    """(w, h, RGB bytes) of the frame the refresh shows next"""
    mode  = r[0x10 // 4] & 7
    bpp   = (1, 2, 4, 8, 16)[mode] if mode <= 4 else 8
    w, h  = r[0x20 // 4] & 0xFFFF, r[0x20 // 4] >> 16
    dbl   = (r[0x24 // 4] >> 4) & 1
    fbase = r[0x40 // 4 + (r[0x30 // 4] & 3)]
    fw, fh = ((w + 1) // 2, (h + 1) // 2) if dbl else (w, h)
    bit0  = fbase * bpp
    a0    = bit0 // 32 * 4
    nw    = (bit0 % 32 + fw * fh * bpp + 31) // 32
    vram  = struct.pack("<%dI" % nw, *j.memory_read32(VRAM_BASE + a0, nw))
    bit0 %= 32
    if bpp == 16:
        px = []
        for k in range(fw * fh):
            v = vram[bit0 // 8 + 2 * k] | (vram[bit0 // 8 + 2 * k + 1] << 8)
            px.append(bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)))
    else:
        pal = palette(j, r, pal_addr)
        if bpp == 8:
            px = [pal[v] for v in vram[bit0 // 8:bit0 // 8 + fw * fh]]
        else:
            m = (1 << bpp) - 1
            px = [pal[(vram[(bit0 + k * bpp) // 8] >> ((bit0 + k * bpp) % 8)) & m] for k in range(fw * fh)]
    if dbl:
        rows = [b"".join(p * 2 for p in px[y * fw:(y + 1) * fw])[:w * 3] for y in range(fh)]
        return w, h, b"".join(rows[y // 2] for y in range(h))
    return w, h, b"".join(px)


def wait_halt(j, timeout):
    t0 = time.time()
    while not j.halted():
        if time.time() - t0 > timeout:
            return False
        time.sleep(0.0005)
    return True


def main():
    ap = argparse.ArgumentParser(description="Frame-by-frame video of a demo over J-Link")
    ap.add_argument("app")
    ap.add_argument("-o", "--out", default="", help="video file (default: <app>.mp4)")
    ap.add_argument("--fps", type=int, default=60, help="refresh rate of the demo (default 60)")
    ap.add_argument("--scale", type=int, default=2, help="pixel scale of the video (default 2)")
    ap.add_argument("--max-frames", type=int, default=0, help="stop after N frames (default: one pass)")
    ap.add_argument("--frames", default="", help="directory for the PNGs (default: a temporary one)")
    ap.add_argument("--keep", action="store_true", help="keep the PNGs")
    ap.add_argument("--timeout", type=float, default=10.0, help="longest frame, seconds")
    ap.add_argument("--speed", type=int, default=25000, help="JTAG clock in kHz (default 25000)")
    ap.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable (default: on PATH)")
    args = ap.parse_args()

    elf  = elf_of(args.app)
    name = os.path.splitext(os.path.basename(elf))[0]
    out  = args.out or f"{name}.mp4"
    sym  = symbols(elf, ["blns_wait_frame", "wait_fence_frame", "demo_pass_done", "pal"])
    if "blns_wait_frame" not in sym:
        sys.exit(f"{elf}: no blns_wait_frame")
    fdir = args.frames or tempfile.mkdtemp(prefix=f"{name}_frames_")
    os.makedirs(fdir, exist_ok=True)

    j = pylink.JLink()
    j.open()
    j.set_tif(pylink.enums.JLinkInterfaces.JTAG)
    j.set_speed(args.speed)
    j.connect("RISC-V")

    # Load from a reset and halt: the capture starts at the first frame
    j.reset(ms=0, halt=True)
    for addr, blob in load_segments(elf):
        n = len(blob) // 4 * 4
        if n:
            j.memory_write32(addr, list(struct.unpack("<%dI" % (n // 4), blob[:n])))
        if len(blob) > n:
            j.memory_write8(addr + n, list(blob[n:]))
    j.reset(ms=0, halt=True)
    # Two hardware triggers: the frame waits, else the end of a pass
    j.hardware_breakpoint_set(sym["blns_wait_frame"])
    second = "wait_fence_frame" if "wait_fence_frame" in sym else "demo_pass_done"
    if second in sym:
        j.hardware_breakpoint_set(sym[second])
    j.restart()

    frames, t0, led_max = 0, time.time(), 0
    try:
        while not args.max_frames or frames < args.max_frames:
            if not wait_halt(j, args.timeout):
                print(f"no frame wait within {args.timeout} s: stopping")
                break
            if j.register_read(CSR_DPC) == sym.get("demo_pass_done", -1):
                print("one pass done")
                break
            led = j.memory_read32(LED_CTRL, 1)[0] & 255
            if led_max > 1 and 0 < led < led_max:
                print(f"one pass done (scene {led_max} -> {led})")
                break
            led_max = max(led_max, led)
            for _ in range(10000):                      # the GPU finishes the frame
                st = j.memory_read32(GPU_STAT, 1)[0]
                if (st >> 16) & 1 and not (st >> 20) & 3:
                    break
            r = j.memory_read32(BLNS_BASE, REG_NR)
            w, h, rgb = frame(j, r, sym.get("pal"))
            png_rgb(os.path.join(fdir, f"f{frames:06d}.png"), w, h, rgb)
            frames += 1
            if frames % 100 == 0:
                el = time.time() - t0
                print(f"{frames} frames, {el:.0f} s ({el / frames:.3f} s/frame)", flush=True)
            j.step()                                    # past the breakpoint, then run
            j.restart()
    finally:
        j.breakpoint_clear_all()
        if j.halted():
            j.restart()
        j.close()

    print(f"{frames} frames captured in {time.time() - t0:.0f} s")
    if frames:
        k = max(1, args.scale)
        subprocess.run([args.ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps),
                        "-i", os.path.join(fdir, "f%06d.png"),
                        "-vf", f"scale=iw*{k}:ih*{k}:flags=neighbor", "-c:v", "libx264",
                        "-pix_fmt", "yuv420p", "-crf", "16", out], check=True)
        print(f"{out}: {frames} frames at {args.fps} fps ({frames / args.fps:.1f} s)")
    if not args.keep and not args.frames:
        shutil.rmtree(fdir)


if __name__ == "__main__":
    main()

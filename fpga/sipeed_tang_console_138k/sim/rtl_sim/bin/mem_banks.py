#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    mem_banks.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Split a $readmemh word image (pmem.mem, "@addr" records) into the images of
# the program memory's banks (<prefix><n>.mem, one word per line, zero-filled).
#
#   mem_banks.py pmem.mem [--banks 12] [--words 2048] [--prefix pmem_bank]
# (<n> is 0-9 then A-Z, as pmem_bram.v names its INIT_FILEs)
#----------------------------------------------------------------------------
import argparse

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--banks", type=int, default=12)
ap.add_argument("--words", type=int, default=2048)
ap.add_argument("--prefix", default="pmem_bank")
a = ap.parse_args()

mem = [0] * (a.banks * a.words)
addr = 0
with open(a.image) as f:
    for line in f:
        for tok in line.split("//")[0].split():
            if tok.startswith("@"):
                addr = int(tok[1:], 16)
            else:
                if addr < len(mem):
                    mem[addr] = int(tok, 16)
                addr += 1
for b in range(a.banks):
    with open("%s%s.mem" % (a.prefix, chr(48 + b) if b < 10 else chr(55 + b)), "w") as f:
        for w in mem[b * a.words:(b + 1) * a.words]:
            f.write("%08x\n" % w)

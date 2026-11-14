#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    check_loader.py (boot_menu)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Checks that the loader block of a boot_menu ELF needs nothing below it:
# no relocation of the block (a call, a jump, a branch, an address of code
# or data, a pointer in its read-only data) resolves into the program memory
# from its base up to _loader_start, which an app image overwrites while the
# loader runs. The ELF is linked with --emit-relocs (-Wl,-q).
#
#   tools/check_loader.py riscv-none-elf-readelf boot_menu.elf
#----------------------------------------------------------------------------
import re
import subprocess
import sys

ROM_BASE = 0x20000000
IGNORED = ('R_RISCV_RELAX', 'R_RISCV_ALIGN', 'R_RISCV_ADD', 'R_RISCV_SUB', 'R_RISCV_SET')


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def main():
    readelf, elf = sys.argv[1], sys.argv[2]
    syms = {}
    for line in run(readelf, '-sW', elf).splitlines():
        f = line.split()
        if len(f) >= 8 and f[0].endswith(':') and f[0][:-1].isdigit():
            syms[f[7]] = int(f[1], 16)
    start, end = syms['_loader_start'], syms['_loader_end']

    bad, section, n = [], None, 0
    for line in run(readelf, '-rW', elf).splitlines():
        m = re.match(r"Relocation section '([^']+)'", line)
        if m:
            section = m.group(1)
            continue
        if section not in ('.rela.loader',):
            continue
        f = line.split()
        if len(f) < 4 or not re.fullmatch(r'[0-9a-f]+', f[0]):
            continue
        rtype = f[2]
        if rtype.startswith(IGNORED):
            continue
        m = re.search(r'([0-9a-f]+)\s+(\S+)\s*([+-])\s*([0-9a-f]+)\s*$', line)
        if not m:
            continue
        target = int(m.group(1), 16) + (1 if m.group(3) == '+' else -1) * int(m.group(4), 16)
        n += 1
        if ROM_BASE <= target < start:
            bad.append('  0x%s %s -> 0x%08x (%s)' % (f[0], rtype, target, m.group(2)))

    if n == 0:
        sys.exit('check_loader.py: no relocation in .loader: link with -Wl,-q')
    if bad:
        print('check_loader.py: the loader block 0x%08x-0x%08x reaches below itself:' % (start, end))
        print('\n'.join(bad))
        sys.exit(1)
    print('loader block 0x%08x-0x%08x (%d bytes): %d relocations, none below it' % (start, end, end - start, n))


if __name__ == '__main__':
    main()

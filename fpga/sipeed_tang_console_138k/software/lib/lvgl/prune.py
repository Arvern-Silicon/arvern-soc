#!/usr/bin/env python3
# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      prune.py (software/lib/lvgl)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------
# Copy the part of an LVGL checkout that a configuration builds into this
# directory (see README.md):
#
#   prune.py <lvgl checkout> <directory of lv_conf.h> [--dest DIR]
#
# Every src/**/*.c of the checkout is compiled with that lv_conf.h; the files
# that produce code or data are kept, except the DROP list below, with every
# header they include and every header lvgl.h includes (gcc -MMD). The licence,
# the copyright notes and the top-level headers are kept too. DIR (default: this
# directory) loses its old src/ and include/ first (its other files, e.g. the
# stand-ins of the DROP list, stay). The checkout is only read.
# ----------------------------------------------------------------------------
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

CC = "riscv-none-elf-gcc"
NM = "riscv-none-elf-nm"
FLAGS = ["-march=rv32emc_zbb_zba_zbs_zbc_zicsr_zcb", "-mabi=ilp32e", "-Os", "-std=gnu11",
         "-DLV_CONF_INCLUDE_SIMPLE"]
TOP = ["LICENCE.txt", "COPYRIGHTS.md", "lvgl.h", "lvgl_private.h", "lv_version.h"]
# Sources left out although they produce code: LVGL has no option to turn them
# off, and the application does not use them. The file next to this script that
# stands in for them (its global functions) is named after each group.
DROP = {
    "lv_draw_blur_none.c": ["src/draw/lv_draw_blur.c", "src/draw/sw/lv_draw_sw_blur.c"],
}


def deps(dfile, root):
    """Headers of a gcc -MMD dependency file, relative to the checkout."""
    text = dfile.read_text().replace("\\\n", " ")
    out = set()
    for tok in text.split(":", 1)[1].split():
        p = Path(tok).resolve()
        if p.suffix == ".h" and root in p.parents:
            out.add(p.relative_to(root))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("lvgl")
    ap.add_argument("conf")
    ap.add_argument("--dest", default=str(Path(__file__).resolve().parent))
    a = ap.parse_args()
    root, conf, dest = Path(a.lvgl).resolve(), Path(a.conf).resolve(), Path(a.dest).resolve()
    if not (conf / "lv_conf.h").exists():
        sys.exit(f"no lv_conf.h in {conf}")
    tmp = Path(tempfile.mkdtemp(prefix="lvgl_prune_"))
    inc = [f"-I{conf}", f"-I{root}"]

    def cc(src):
        o = tmp / (str(src.relative_to(root)).replace("/", "__") + ".o")
        r = subprocess.run([CC, *FLAGS, *inc, "-MMD", "-c", str(src), "-o", str(o)],
                           capture_output=True, text=True)
        if r.returncode:
            return src, None, r.stderr
        syms = subprocess.run([NM, "--defined-only", str(o)], capture_output=True, text=True).stdout
        used = any(l.split()[1] in "TtDdRrBbSsGgCc" for l in syms.splitlines() if len(l.split()) == 3)
        return src, (deps(o.with_suffix(".d"), root) if used else None), ""

    srcs = sorted(root.glob("src/**/*.c"))
    with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        res = list(ex.map(cc, srcs))
    bad = [(s, e) for s, d, e in res if e]
    if bad:
        for s, e in bad[:5]:
            print(s, e[:500], file=sys.stderr)
        sys.exit(f"{len(bad)} files did not compile")
    drop = {Path(f) for files in DROP.values() for f in files}
    missing = drop - {s.relative_to(root) for s in srcs}
    if missing:
        sys.exit(f"DROP names files the checkout does not have: {sorted(map(str, missing))}")
    kept = [(s, d) for s, d, _ in res if d is not None and s.relative_to(root) not in drop]
    keep = {s.relative_to(root) for s, _ in kept}
    hdrs = set().union(*(d for _, d in kept))
    probe = tmp / "probe.c"
    probe.write_text('#include "lvgl.h"\n')
    subprocess.run([CC, *FLAGS, *inc, "-MMD", "-c", str(probe), "-o", str(tmp / "probe.o")], check=True)
    hdrs |= deps(tmp / "probe.d", root)

    for d in ("src", "include"):
        shutil.rmtree(dest / d, ignore_errors=True)
    for rel in sorted(keep | hdrs | {Path(t) for t in TOP}):
        (dest / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / rel, dest / rel)
    shutil.rmtree(tmp)
    print(f"{len(keep)} of {len(srcs)} sources ({len(drop)} dropped), {len(hdrs)} headers -> {dest}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Print the resource use and the Fmax per clock of a Gowin place & route
(impl/pnr), each Fmax judged against its requirement:
python3 timing_summary.py <impl/pnr dir>

The placer aims at a tighter sys_clk period than the requirement
(synthesis.tcl), so a clock's requirement is the slower of the report's
constraint and the period design.sdc gives it."""
import glob, html, os, re, sys

d = sys.argv[1] if len(sys.argv) > 1 else "WORK/arvern_fpga/impl/pnr"
sdc = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "design.sdc")).read()
req = {n: 1000.0 / float(p) for n, p in re.findall(r"create_clock -name (\S+) -period ([0-9.]+)", sdc)}

rpt = glob.glob(os.path.join(d, "*.rpt.txt"))
if rpt:
    txt = open(rpt[0], errors="replace").read()
    for key in ("Logic", "Register", "BSRAM", "DSP", "I/O Port", "PLL"):
        m = re.search(r"^\s*%s[^\n]*" % re.escape(key), txt, re.M)
        if m: print("  " + " ".join(m.group(0).split()))
tr = glob.glob(os.path.join(d, "*_tr_content.html"))
if tr:
    txt = re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", " ", open(tr[0], errors="replace").read())))
    for clk, cons, fmax in re.findall(r"\d+ (\S+) (\d+\.\d+)\(MHz\) (\d+\.\d+)\(MHz\) \d+ TOP", txt):
        r = min(float(cons), req.get(clk, float(cons)))
        print("  %s: Fmax %s MHz, requirement %.3f MHz: %s"
              % (clk, fmax, r, "met" if float(fmax) >= r else "FAILED"))

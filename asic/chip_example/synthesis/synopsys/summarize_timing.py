#!/usr/bin/env python3
"""
summarize_timing.py -- tabulate (and diff) the per-fabric synthesis snapshots
written by run_syn_sweep under ./results_sweep/<tag>/<fabric>/.

Usage:
    python3 summarize_timing.py <tag>              # one snapshot, all fabrics
    python3 summarize_timing.py <tag_a> <tag_b>    # side by side, with deltas

Per fabric it reports, from report.qor (canonical DC numbers):
    main-clock (free_clk) period / WNS / TNS / NVP / hold WNS,
    overall setup WNS / TNS / NVP and hold WNS / TNS / NVP (all path groups),
from report.timing_summary (report_timing_summary.tcl):
    timing loops (report_timing -loops), loop-broken arcs (report_disable_timing),
    fabric / core / chip cell area,
and from synthesis.log:
    loop warnings (OPT-314 "to break a timing loop", ELAB-* feedback loop).

Exit status is 1 if any snapshot shows a loop or a negative setup WNS
(hold is reported but not flagged: pre-CTS synthesis does no hold fixing).
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SWEEP = os.path.join(HERE, "results_sweep")
FABRICS = ["generic", "hiperf", "fused"]

LOOP_LOG_PATTERNS = [
    r"OPT-314",                     # Disabling timing arc ... to break a timing loop
    r"combinational (feedback )?loop",
    r"timing loop",
]


def read_kv(path):
    d = {}
    if not os.path.isfile(path):
        return d
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                d[k.strip()] = v.strip()
    return d


QOR_KEYS = {
    "Critical Path Slack":    "setup_wns",
    "Total Negative Slack":   "setup_tns",
    "No. of Violating Paths": "setup_nvp",
    "Worst Hold Violation":   "hold_wns",
    "Total Hold Violation":   "hold_tns",
    "No. of Hold Violations": "hold_nvp",
    "Critical Path Clk Period": "clk_period",
}


def read_qor(path):
    """report_qor -> {group: {setup_wns, setup_tns, setup_nvp, hold_*, clk_period}}"""
    groups = {}
    if not os.path.isfile(path):
        return groups
    cur = None
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*Timing Path Group '([^']+)'", line)
            if m:
                cur = m.group(1)
                groups[cur] = {}
                continue
            if cur is None:
                continue
            for label, key in QOR_KEYS.items():
                m = re.match(r"\s*" + re.escape(label) + r":\s+([-\d.]+)", line)
                if m:
                    groups[cur][key] = float(m.group(1))
            if line.strip().startswith("Cell Count"):
                cur = None
    return groups


def qor_overall(groups):
    """Overall WNS = min over groups, TNS/NVP = sum over groups (DC's own
    report_qor convention). Groups with no paths are skipped."""
    out = {}
    if not groups:
        return out
    for pfx in ("setup", "hold"):
        w = [g[pfx + "_wns"] for g in groups.values() if pfx + "_wns" in g]
        if w:
            out[pfx + "_wns"] = min(w)
            out[pfx + "_tns"] = sum(g.get(pfx + "_tns", 0.0) for g in groups.values())
            out[pfx + "_nvp"] = sum(g.get(pfx + "_nvp", 0.0) for g in groups.values())
    for gname, g in groups.items():
        for k in ("setup_wns", "setup_tns", "setup_nvp", "hold_wns", "clk_period"):
            if k in g:
                out[f"{k}[{gname}]"] = g[k]
    return out


def count_loop_warnings(log_path):
    if not os.path.isfile(log_path):
        return None
    n = 0
    pat = re.compile("|".join(LOOP_LOG_PATTERNS), re.IGNORECASE)
    with open(log_path, errors="replace") as f:
        for line in f:
            # "Information: Checking loops..." is check_timing's section header
            if line.startswith("Information: Checking loops"):
                continue
            if pat.search(line):
                n += 1
    return n


def load(tag):
    out = {}
    for fab in FABRICS:
        d = os.path.join(SWEEP, tag, fab)
        if not os.path.isdir(d):
            continue
        kv = read_kv(os.path.join(d, "report.timing_summary"))
        # report_qor is the canonical source for slack numbers
        kv.update(qor_overall(read_qor(os.path.join(d, "report.qor"))))
        kv.update({("run_" + k): v for k, v in read_kv(os.path.join(d, "run.info")).items()})
        kv["log_loop_warnings"] = count_loop_warnings(os.path.join(d, "synthesis.log"))
        out[fab] = kv
    return out


def fnum(v, fmt="{:.3f}"):
    try:
        return fmt.format(float(v))
    except (TypeError, ValueError):
        return "n/a" if v is None else str(v)


MAIN_CLK = "free_clk"

ROWS = [
    (f"{MAIN_CLK} period",  f"clk_period[{MAIN_CLK}]", "{:.2f}"),
    (f"{MAIN_CLK} WNS",     f"setup_wns[{MAIN_CLK}]",  "{:.3f}"),
    (f"{MAIN_CLK} TNS",     f"setup_tns[{MAIN_CLK}]",  "{:.2f}"),
    (f"{MAIN_CLK} NVP",     f"setup_nvp[{MAIN_CLK}]",  "{:.0f}"),
    (f"{MAIN_CLK} hold WNS", f"hold_wns[{MAIN_CLK}]",  "{:.3f}"),
    ("setup WNS (ns)",     "setup_wns",          "{:.3f}"),
    ("setup TNS (ns)",     "setup_tns",          "{:.3f}"),
    ("setup NVP",          "setup_nvp",          "{:.0f}"),
    ("hold WNS (ns)",      "hold_wns",           "{:.3f}"),
    ("hold TNS (ns)",      "hold_tns",           "{:.3f}"),
    ("hold NVP",           "hold_nvp",           "{:.0f}"),
    ("timing loops",       "timing_loops",       "{:.0f}"),
    ("loop-broken arcs",   "loop_disabled_arcs", "{:.0f}"),
    ("loop warnings (log)", "log_loop_warnings", "{:.0f}"),
    ("fabric area",        "area_fabric",        "{:.1f}"),
    ("core area",          "area_core",          "{:.1f}"),
    ("chip area",          "area_chip",          "{:.1f}"),
    ("run rc",             "run_run_rc",         "{:.0f}"),
    ("elapsed (s)",        "run_elapsed_s",      "{:.0f}"),
]


def problems(kv):
    """List of reasons this run needs attention. Hold violations are not
    flagged: synthesis is pre-CTS and does no hold fixing."""
    def f(k):
        try:
            return float(kv.get(k))
        except (TypeError, ValueError):
            return None
    out = []
    for k, name in (("timing_loops", "timing loops"),
                    ("loop_disabled_arcs", "loop-broken arcs"),
                    ("log_loop_warnings", "loop warnings in log")):
        if (f(k) or 0) > 0:
            out.append(name)
    v = f("setup_wns")
    if v is None:
        out.append("no timing data")
    elif v < 0:
        out.append("setup violation")
    if kv.get("run_run_rc") not in (None, "0"):
        out.append("run_syn rc=" + str(kv.get("run_run_rc")))
    return out


def is_bad(kv):
    return bool(problems(kv))


def print_single(tag, data):
    fabs = [f for f in FABRICS if f in data]
    print(f"\nSnapshot: {tag}")
    for f in fabs:
        print(f"  {f:8s} {data[f].get('run_date','')}  ips={data[f].get('run_arvern_ips_git','?')}  "
              f"core={data[f].get('run_arvern_git','?')}  soc={data[f].get('run_arvern_soc_git','?')}")
    print()
    print(f"{'':22s}" + "".join(f"{f.upper():>14s}" for f in fabs))
    for label, key, fmt in ROWS:
        print(f"{label:22s}" + "".join(f"{fnum(data[f].get(key), fmt):>14s}" for f in fabs))
    bad = [f"{f}: {', '.join(problems(data[f]))}" for f in fabs if is_bad(data[f])]
    print()
    print("STATUS: " + ("OK (no loops, no setup violation)" if not bad else "ATTENTION -- " + "; ".join(bad)))
    return bool(bad)


def print_compare(tag_a, a, tag_b, b):
    fabs = [f for f in FABRICS if f in a or f in b]
    print(f"\nCompare: {tag_a}  ->  {tag_b}")
    for f in fabs:
        print(f"  {f:8s} A: ips={a.get(f,{}).get('run_arvern_ips_git','?')}   B: ips={b.get(f,{}).get('run_arvern_ips_git','?')}")
    print()
    hdr = f"{'':22s}"
    for f in fabs:
        hdr += f"{f.upper()+' A':>13s}{f.upper()+' B':>13s}{'delta':>11s}"
    print(hdr)
    for label, key, fmt in ROWS:
        line = f"{label:22s}"
        for f in fabs:
            va = a.get(f, {}).get(key)
            vb = b.get(f, {}).get(key)
            try:
                delta = fmt.format(float(vb) - float(va))
                if not delta.startswith("-"):
                    delta = "+" + delta
            except (TypeError, ValueError):
                delta = "n/a"
            line += f"{fnum(va, fmt):>13s}{fnum(vb, fmt):>13s}{delta:>11s}"
        print(line)
    bad = [f"{f}: {', '.join(problems(b[f]))}" for f in fabs if f in b and is_bad(b[f])]
    print()
    print("STATUS (" + tag_b + "): " + ("OK (no loops, no setup violation)" if not bad else "ATTENTION -- " + "; ".join(bad)))
    return bool(bad)


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        sys.exit(2)
    tag_a = sys.argv[1]
    a = load(tag_a)
    if not a:
        print(f"no snapshot found under {os.path.join(SWEEP, tag_a)}")
        sys.exit(2)
    if len(sys.argv) == 2:
        sys.exit(1 if print_single(tag_a, a) else 0)
    tag_b = sys.argv[2]
    b = load(tag_b)
    if not b:
        print(f"no snapshot found under {os.path.join(SWEEP, tag_b)}")
        sys.exit(2)
    sys.exit(1 if print_compare(tag_a, a, tag_b, b) else 0)


if __name__ == "__main__":
    main()

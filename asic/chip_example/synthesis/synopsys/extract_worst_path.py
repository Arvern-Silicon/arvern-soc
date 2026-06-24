#!/usr/bin/env python3
"""
extract_worst_path.py  —  Print the worst (or N worst) timing paths from a
Synopsys DC 'report_timing -path full' output file.

Usage:
    python3 extract_worst_path.py [report_file] [N]

Defaults:
    report_file  : results/report.full_paths.max
    N            : 1   (print only the single worst path)
"""

import re
import sys
import os

REPORT_FILE = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.join(os.path.dirname(__file__), "results/report.full_paths.max")
N_PATHS = int(sys.argv[2]) if len(sys.argv) > 2 else 1

with open(REPORT_FILE) as f:
    content = f.read()

# Split on 'Startpoint:' — each path block begins there
raw_blocks = re.split(r'(?=[ \t]+Startpoint:)', content)

paths = []
for blk in raw_blocks:
    slack_m  = re.search(r'slack\s+\((?P<status>MET|VIOLATED)\)\s+(?P<val>[-\d.]+)', blk)
    start_m  = re.search(r'Startpoint\s*:\s+(\S+)', blk)
    end_m    = re.search(r'Endpoint\s*:\s+(\S+)',   blk)
    if slack_m and start_m and end_m:
        paths.append({
            'slack'  : float(slack_m.group('val')),
            'status' : slack_m.group('status'),
            'start'  : start_m.group(1),
            'end'    : end_m.group(1),
            'text'   : blk.rstrip(),
        })

paths.sort(key=lambda p: p['slack'])

print(f"  {'#':<4} {'Slack':>8}  {'Status':<10}  Startpoint → Endpoint")
print("  " + "-" * 100)
for i, p in enumerate(paths):
    tag = "[VIOLATED]" if p['status'] == 'VIOLATED' else "[MET]     "
    print(f"  {i+1:<4} {p['slack']:>+8.3f}  {tag}  {p['start']}  →  {p['end']}")
print()

for i in range(min(N_PATHS, len(paths))):
    p = paths[i]
    print("=" * 78)
    print(f"  PATH {i+1}  slack={p['slack']:+.3f} ({p['status']})")
    print("=" * 78)
    print(p['text'])
    print()

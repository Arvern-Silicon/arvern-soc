#!/usr/bin/env python3
"""Flatten a GowinSynthesis netlist (.vg): print '<primitive> <full/instance/path>'."""
import re, sys
txt = open(sys.argv[1]).read()
top = sys.argv[2] if len(sys.argv) > 2 else 'arvern_fpga'
mods = {}
for m in re.finditer(r'^module\s+(\\\S+|\w+)\s*\((.*?)^endmodule', txt, re.M | re.S):
    name = m.group(1).lstrip('\\')
    body = m.group(2)
    insts = re.findall(r'^\s*(\\\S+|[A-Za-z_][\w$]*)\s+(?:#\s*\(.*?\)\s*)?(\\\S+|[A-Za-z_][\w$\[\]\.]*)\s*\(', body, re.M | re.S)
    mods[name] = [(t.lstrip('\\'), i.lstrip('\\')) for t, i in insts
                  if t not in ('input', 'output', 'inout', 'wire', 'reg', 'assign', 'defparam', 'module')]
out = []
def walk(mod, path):
    for t, i in mods.get(mod, []):
        full = path + i
        if t in mods:
            walk(t, full + '/')
        else:
            out.append((t, full))
walk(top, '')
for t, f in out:
    print(t, f)

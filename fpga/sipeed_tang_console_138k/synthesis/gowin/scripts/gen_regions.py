#!/usr/bin/env python3
"""Floorplan constraints (CST) from the synthesized netlist.

  gen_regions.py <netlist.vg> [--pins <posp>] [--pin-prefix P ...]
                 [--fence R[a:b]C[c:d]] [--fence-prefix P ...]
                 [--gfx R[a:b]C[c:d]]

--pins   : pins the BSRAMs whose names start with a --pin-prefix to the sites a
           reference build's post-place file (.posp) gives them
--fence  : places every other cell whose name starts with a --fence-prefix in
           that region (a hard GROUP / GRP_LOC constraint)
--gfx    : places the GPU, the Belenos backend and the VRAM in that region
The CST parser is line based: a group is created with its first 50 names, then
extended with +=.
"""
import argparse, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument('vg')
ap.add_argument('--pins')
ap.add_argument('--pin-prefix', nargs='*', default=['ahb_bus_system_inst/pmem_inst', 'ahb_bus_system_inst/sram'])
ap.add_argument('--fence')
ap.add_argument('--fence-prefix', nargs='*', default=['dut/', 'ahb_bus_system_inst/',
                'belenos_inst/u_core/u_ahb_vram/', 'belenos_inst/u_core/u_ahb_reg/',
                'belenos_inst/u_core/u_vram_arb/', 'belenos_inst/u_core/u_regs/',
                'belenos_inst/u_core/u_gate'])
ap.add_argument('--gfx')
a = ap.parse_args()

here = os.path.dirname(os.path.abspath(__file__))
flat = subprocess.run([sys.executable, os.path.join(here, 'vg_flat.py'), a.vg],
                      capture_output=True, text=True, check=True).stdout.split('\n')
SKIP = ('VCC', 'GND', 'IBUF', 'OBUF', 'IOBUF', 'TBUF', 'PLL', 'ODDR', 'DCE')
GFX = ('belenos_inst/u_core/G_GPU', 'belenos_inst/u_core/u_backend/',
       'belenos_inst/u_core/G_LUT', 'vram_inst/', 'lut_inst/')
BSRAM = ('SDPB', 'DPB', 'SP', 'SDPX9B', 'DPX9B', 'SPX9', 'pROM', 'pROMX9')

pins = {}
if a.pins:
    for l in open(a.pins):
        m = re.match(r'(\S+)\s+PLACE_(BSRAM_R\d+\[\d+\](?:\[[AB]\])?)', l)
        if m and m.group(1).startswith(tuple(a.pin_prefix)):
            pins[m.group(1)] = m.group(2)

fence, gfx = [], []
for line in flat:
    if not line:
        continue
    prim, name = line.split(' ', 1)
    if prim in SKIP or name in pins:
        continue
    if a.fence and name.startswith(tuple(a.fence_prefix)) and not (a.pins and prim in BSRAM):
        fence.append(name)
    elif a.gfx and name.startswith(GFX):
        gfx.append(name)

def grp(g, names, loc):
    out = []
    for k in range(0, len(names), 50):
        out.append('GROUP %s %s { %s };' % (g, '=' if k == 0 else '+=',
                   ' '.join('"%s"' % n for n in names[k:k + 50])))
    out.append('GRP_LOC %s %s;' % (g, loc))
    return '\n'.join(out)

print('// Floorplan (generated from the synthesized netlist)')
for n, site in sorted(pins.items()):
    print('INS_LOC "%s" %s;' % (n, site))
if fence:
    print(grp('fp_fence', fence, a.fence))
if gfx:
    print(grp('fp_gfx', gfx, a.gfx))
print('// %d pinned BSRAMs, %d fenced cells, %d graphics cells' % (len(pins), len(fence), len(gfx)))

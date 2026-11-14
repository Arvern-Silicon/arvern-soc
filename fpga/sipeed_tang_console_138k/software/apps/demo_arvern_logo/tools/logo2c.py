#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    logo2c.py (demo_arvern_logo)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Converts the aRVern logo SVGs into run tables for the Belenos demo.
#
# The light logo is rendered once per size without antialiasing, every path
# recoloured with a unique ID colour (one for its fill, one for its stroke),
# so each pixel maps back to the path part that painted it. Each part then
# gets a palette index:
#   0            transparent (background)
#   1 ..         one entry per distinct (light colour, dark colour) pair
#   TRACE_BASE.. one entry per blue circuit trace, so the LUT can animate them
# The dark theme reuses the light geometry with the dark logo's colours (same
# path IDs), so a theme change is a LUT bank switch.
#
# The traces are numbered by distance from the RV mark, outermost first, so a
# rotation of their LUT entries runs inward along the circuit.
#
# The wordmark (the "aRVern" text of the label logo) and the demo's text lines
# (IBM Plex Sans, SIL Open Font License) are antialiased to three coverage
# levels and stored as 2-bit bitmaps (0 none, 1 light, 2 half, 3 full ink;
# four pixels per byte, leftmost in bits [1:0], rows padded to a byte). The
# firmware maps the levels to its own colour ramp.
#
# Output: logo_data.h (palettes and, per image, runs grouped by palette index:
# a layer per colour, painted in SVG order) and, in --preview DIR, a PNG per
# image and theme rendered from the runs exactly as the firmware draws them.
#
#   tools/logo2c.py <light.svg> <dark.svg> <label.svg> <out.h> [--preview DIR]
#----------------------------------------------------------------------------
import argparse
import os
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

TRACE_COLOUR = '#4286bd'
FONT = os.path.expanduser('~/Library/Fonts/IBMPlexSans-SemiBold.ttf')

# Text lines: (C name, text, pixel size)
TEXTS = [
    ('txt_sub',     'RISC-V core  +  Belenos graphics', 14),
    # aRVern core page
    ('txt_a0',      'RV32 RISC-V core', 16),
    ('txt_a1',      'RV32I / RV32E  -  M  -  Zba Zbb Zbs Zbc', 14),
    ('txt_a2',      'Zca Zcb Zcmp Zcmt  -  M / S / U modes', 14),
    ('txt_a3',      'PMP + Smepmp  -  Zicntr  -  Zihpm', 14),
    ('txt_a4',      'Debug 1.0 + Sdtrig  -  Smrnmi  -  Sm/Ssdbltrp', 14),
    # Belenos page
    ('txt_b0',      'Belenos graphics controller', 16),
    ('txt_b1',      '1 to 16 bpp  -  RGB888 LUT  -  2D GPU', 14),
    ('txt_b2',      'raster ops  -  lines  -  remap  -  mask', 14),
    ('txt_b3',      'affine blits  -  blending  -  triangles', 14),
    ('txt_b4',      'DBI, DPI and HDMI  -  hardware cursor', 14),
    # both pages
    ('txt_git',     'github.com/Arvern-Silicon', 14),
]


def parse_paths(svg):
    """Return [(id, fill, stroke, start, end)] for every <path>, in document order."""
    out = []
    for m in re.finditer(r'<path\b([^>]*)>', svg):
        a = m.group(1)
        pid = re.search(r'\bid="([^"]*)"', a).group(1)
        st = re.search(r'style="([^"]*)"', a).group(1)
        f = re.search(r'fill:([^;]*)', st)
        k = re.search(r'stroke:([^;]*)', st)
        fill = f.group(1) if f and f.group(1) != 'none' else None
        strk = k.group(1) if k and k.group(1) != 'none' else None
        out.append((pid, fill, strk, m.start(1), m.end(1)))
    return out


def id_colour(n):
    return '#%02x%02x%02x' % ((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF)


def render_ids(svg, paths, width, tmp):
    """Render with every fill / stroke replaced by an ID colour; return (image, part list)."""
    parts = []                                   # part n+1 = (path index, 'fill'|'stroke')
    s = svg
    for pi in reversed(range(len(paths))):       # edit from the end: offsets stay valid
        pid, fill, strk, a0, a1 = paths[pi]
        attrs = s[a0:a1]
        st = re.search(r'style="([^"]*)"', attrs).group(1)
        new = st
        if fill:
            parts.append((pi, 'fill'))
            new = re.sub(r'fill:[^;]*', 'fill:' + id_colour(len(parts) * 7), new)
        if strk:
            parts.append((pi, 'stroke'))
            new = re.sub(r'stroke:[^;]*', 'stroke:' + id_colour(len(parts) * 7), new)
        new = re.sub(r'(fill|stroke)-opacity:[^;]*', r'\1-opacity:1', new)
        s = s[:a0] + attrs.replace(st, new) + s[a1:]
    src = os.path.join(tmp, 'ids.svg')
    png = os.path.join(tmp, 'ids_%d.png' % width)
    open(src, 'w').write(s)
    subprocess.run(['inkscape', src, '--export-type=png', '--export-filename=' + png,
                    '--export-area-drawing', '--export-width=%d' % width,
                    '--export-png-antialias=0', '--export-background-opacity=0'],
                   check=True, capture_output=True)
    lut = {(n * 7): p for n, p in enumerate(parts, 1)}
    return Image.open(png).convert('RGBA'), lut


def hex_rgb(c):
    c = c.lstrip('#')
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))


def build(light, dark, width, palette, tmp, centroids=None):
    """Index image (list of rows) for one size; extends palette as needed."""
    lp = parse_paths(light)
    dk = {p[0]: p for p in parse_paths(dark)}
    img, lut = render_ids(light, lp, width, tmp)
    w, h = img.size
    px = img.load()
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                row.append(0)
                continue
            part = lut.get((r << 16) | (g << 8) | b)
            if part is None:                         # edge mix: take the left neighbour
                row.append(row[-1] if row else 0)
                continue
            pi, kind = part
            pid, fill, strk, _, _ = lp[pi]
            if kind == 'stroke' and strk == TRACE_COLOUR:
                key = ('trace', pid)
            else:
                lc = fill if kind == 'fill' else strk
                dp = dk.get(pid)
                dc = (dp[1] if kind == 'fill' else dp[2]) if dp else None
                key = ('colour', lc, dc or lc)
            if key not in palette['index']:
                palette['index'][key] = None             # numbered after all sizes are seen
                palette['order'].append(key)
            row.append(key)
            if centroids is not None:
                c = centroids.setdefault(key, [0, 0, 0])
                c[0] += x; c[1] += y; c[2] += 1
        rows.append(row)
    return rows


def number_palette(palette, centroids):
    colours = [k for k in palette['order'] if k[0] == 'colour']
    traces = [k for k in palette['order'] if k[0] == 'trace']
    rv = [k for k in colours if k[1].lower() == '#ffffff']           # the white RV mark
    cx, cy = (centroids[rv[0]][0] / centroids[rv[0]][2], centroids[rv[0]][1] / centroids[rv[0]][2])

    def dist(k):
        c = centroids[k]
        return -((c[0] / c[2] - cx) ** 2 + (c[1] / c[2] - cy) ** 2)  # outermost first
    traces.sort(key=dist)
    n = 1
    for k in colours:
        palette['index'][k] = n
        n += 1
    trace_base = n
    for k in traces:
        palette['index'][k] = n
        n += 1
    return trace_base, len(traces), n


def runs_by_index(rows, palette):
    """{index: [(y, x, len)]}, in first-appearance (SVG paint) order of the keys."""
    out = {}
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            k = row[x]
            e = x
            while e < len(row) and row[e] == k:
                e += 1
            if k != 0:
                out.setdefault(palette['index'][k], []).append((y, x, e - x))
            x = e
    return out


def preview(rows, palette, pal_rgb, path, scale=2, bg=(0, 0, 0)):
    h, w = len(rows), len(rows[0])
    img = Image.new('RGB', (w, h), bg)
    px = img.load()
    for y, row in enumerate(rows):
        for x, k in enumerate(row):
            if k != 0:
                px[x, y] = pal_rgb[palette['index'][k]]
    img.resize((w * scale, h * scale), Image.NEAREST).save(path)


def render_wordmark(label, width, tmp):
    """Rows of coverage levels 0-3: the label logo's text element only."""
    src = os.path.join(tmp, 'label.svg')
    png = os.path.join(tmp, 'wordmark.png')
    open(src, 'w').write(label)
    tid = re.search(r'<text\b[^>]*\bid="([^"]*)"', label).group(1)
    subprocess.run(['inkscape', src, '--export-type=png', '--export-filename=' + png,
                    '--export-id=' + tid, '--export-id-only', '--export-width=%d' % width,
                    '--export-background-opacity=0'],
                   check=True, capture_output=True)
    img = Image.open(png).convert('RGBA')
    return [[level(img.getpixel((x, y))[3]) for x in range(img.width)]
            for y in range(img.height)]


def level(a):
    """Coverage 0-255 to a level 0-3."""
    return 0 if a < 40 else 1 if a < 110 else 2 if a < 190 else 3


def render_text(text, size):
    """Rows of coverage levels 0-3 for one line of text, cropped to its ink."""
    font = ImageFont.truetype(FONT, size)
    l, t, r, b = font.getbbox(text)
    img = Image.new('L', (r - l + 2, b - t + 2), 0)
    ImageDraw.Draw(img).text((1 - l, 1 - t), text, font=font, fill=255)
    img = img.crop(img.getbbox())
    return [[level(img.getpixel((x, y))) for x in range(img.width)]
            for y in range(img.height)]


def level_runs(rows):
    """{level: [(y, x, len)]} for a coverage image, full ink first."""
    out = {3: [], 2: [], 1: []}
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            v = row[x]
            e = x
            while e < len(row) and row[e] == v:
                e += 1
            if v:
                out[v].append((y, x, e - x))
            x = e
    return {k: v for k, v in out.items() if v}


def emit_runs(name, runs, w, h):
    lines = []
    order = list(runs.keys())
    total = sum(len(v) for v in runs.values())
    lines.append('#define %s_W  %du' % (name.upper(), w))
    lines.append('#define %s_H  %du' % (name.upper(), h))
    lines.append('#define %s_LAYERS  %du' % (name.upper(), len(order)))
    lines.append('#define %s_RUNS  %du' % (name.upper(), total))
    lines.append('')
    lines.append('/* Layer i: runs [%s_layer_start[i], %s_layer_start[i+1]) in colour %s_layer_idx[i] */'
                 % (name, name, name))
    lines.append('static const uint8_t  %s_layer_idx[%d] = {' % (name, len(order)))
    lines.append('    ' + ', '.join(str(i) for i in order) + '};')
    starts, n = [], 0
    for i in order:
        starts.append(n)
        n += len(runs[i])
    starts.append(n)
    lines.append('static const uint16_t %s_layer_start[%d] = {' % (name, len(starts)))
    lines.append('    ' + ', '.join(str(s) for s in starts) + '};')
    lines.append('')
    assert w <= 255 and h <= 256
    lines.append('/* Run: three bytes y, x, length */')
    lines.append('static const uint8_t  %s_runs[%d] = {' % (name, 3 * total))
    words = []
    for i in order:
        for y, x, ln in runs[i]:
            words.append('%d,%d,%d' % (y, x, ln))
    for k in range(0, len(words), 8):
        lines.append('    ' + ', '.join(words[k:k + 8]) + ',')
    lines.append('};')
    return lines, 3 * total


def emit_bitmap(name, rows):
    """2-bit coverage bitmap, four pixels per byte, leftmost in bits [1:0]."""
    w, h = len(rows[0]), len(rows)
    stride = (w + 3) // 4
    data = []
    for row in rows:
        for b in range(stride):
            v = 0
            for k in range(4):
                x = 4 * b + k
                if x < w:
                    v |= row[x] << (2 * k)
            data.append(v)
    lines = ['#define %s_W  %du' % (name.upper(), w),
             '#define %s_H  %du' % (name.upper(), h),
             '#define %s_STRIDE  %du' % (name.upper(), stride),
             'static const uint8_t  %s_bits[%d] = {' % (name, len(data))]
    for k in range(0, len(data), 16):
        lines.append('    ' + ', '.join('0x%02x' % v for v in data[k:k + 16]) + ',')
    lines.append('};')
    return lines, len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('light')
    ap.add_argument('dark')
    ap.add_argument('label')
    ap.add_argument('out_h')
    ap.add_argument('--width', type=int, default=200)
    ap.add_argument('--sprite-width', type=int, default=64)
    ap.add_argument('--wordmark-width', type=int, default=150)
    ap.add_argument('--preview')
    args = ap.parse_args()

    light = open(args.light).read()
    dark = open(args.dark).read()
    label = open(args.label).read()
    palette = {'index': {}, 'order': []}
    centroids = {}
    with tempfile.TemporaryDirectory() as tmp:
        big = build(light, dark, args.width, palette, tmp, centroids)
        small = build(light, dark, args.sprite_width, palette, tmp)
        word = render_wordmark(label, args.wordmark_width, tmp)
    trace_base, trace_nr, pal_nr = number_palette(palette, centroids)

    pal_light = [(0, 0, 0)] * pal_nr
    pal_dark = [(0, 0, 0)] * pal_nr
    for k, i in palette['index'].items():
        if k[0] == 'colour':
            pal_light[i], pal_dark[i] = hex_rgb(k[1]), hex_rgb(k[2])
        else:
            pal_light[i] = pal_dark[i] = hex_rgb(TRACE_COLOUR)

    out = ['/* Generated by tools/logo2c.py from the aRVern logo SVGs: do not edit. */',
           '#ifndef LOGO_DATA_H', '#define LOGO_DATA_H', '', '#include <stdint.h>', '',
           '#define LOGO_PAL_NR      %du' % pal_nr,
           '#define LOGO_TRACE_BASE  %du   /* first trace entry, outermost trace first */' % trace_base,
           '#define LOGO_TRACE_NR    %du' % trace_nr, '',
           '/* RGB888 palettes, entry 0 = transparent (background) */']
    for nm, pal in (('logo_pal_light', pal_light), ('logo_pal_dark', pal_dark)):
        out.append('static const uint32_t %s[%d] = {' % (nm, pal_nr))
        ws = ['0x%06xu' % ((r << 16) | (g << 8) | b) for r, g, b in pal]
        for k in range(0, len(ws), 8):
            out.append('    ' + ', '.join(ws[k:k + 8]) + ',')
        out.append('};')
    out.append('')
    images = [('logo', big, runs_by_index(big, palette)),
              ('sprite', small, runs_by_index(small, palette))]
    bitmaps = [('wordmark', word)] + [(nm, render_text(t, sz)) for nm, t, sz in TEXTS]
    report = []
    for nm, rows, runs in images:
        lines, n = emit_runs(nm, runs, len(rows[0]), len(rows))
        out += lines + ['']
        report.append('%-10s %3dx%-3d runs   %5d bytes' % (nm, len(rows[0]), len(rows), n))
    for nm, rows in bitmaps:
        lines, n = emit_bitmap(nm, rows)
        out += lines + ['']
        report.append('%-10s %3dx%-3d bitmap %5d bytes' % (nm, len(rows[0]), len(rows), n))
    out += ['#endif /* LOGO_DATA_H */', '']
    defs = [l for l in out if l.startswith('#define ') and not l.startswith('#define LOGO_DATA_H')]
    body = [l for l in out if l not in defs]
    i = body.index('/* RGB888 palettes, entry 0 = transparent (background) */')
    j = body.index('#endif /* LOGO_DATA_H */')
    text = (body[:i] + defs + ['', '/* Tables: one translation unit defines LOGO_DATA_TABLES */',
                               '#ifdef LOGO_DATA_TABLES'] + body[i:j] +
            ['#endif /* LOGO_DATA_TABLES */', ''] + body[j:])
    open(args.out_h, 'w').write('\n'.join(text))

    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        for nm, rows in (('logo', big), ('sprite', small)):
            preview(rows, palette, pal_light, os.path.join(args.preview, '%s_light.png' % nm))
            preview(rows, palette, pal_dark, os.path.join(args.preview, '%s_dark.png' % nm))
        for nm, rows in bitmaps:
            img = Image.new('L', (len(rows[0]), len(rows)))
            img.putdata([85 * v for r in rows for v in r])
            img.save(os.path.join(args.preview, nm + '.png'))
    print('palette %d entries (%d colours, %d traces from %d)' % (pal_nr, trace_base - 1, trace_nr, trace_base))
    for r in report:
        print(r)


if __name__ == '__main__':
    main()

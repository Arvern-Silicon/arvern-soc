#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    fb2png.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Convert panel-model images ($writememh of the ILI9341 model frame buffer,
# one RGB565 word per line, row-major) into PNG files next to them.
#
#   fb2png.py [-W 240] [-H 320] [--transpose] [--scale 2] fb_0.hex [...]
#
# --transpose shows the image as the frame buffer that Belenos scans with
# DISP_CFG.CL_SWAP (panel row = frame-buffer x). Unwritten (x) pixels are
# drawn black. No dependency beyond the Python standard library.
#----------------------------------------------------------------------------
import argparse
import struct
import sys
import zlib


def read_hex(path):
    vals = []
    with open(path) as f:
        for line in f:
            line = line.split('//')[0].strip()
            if not line or line.startswith('@'):
                continue
            try:
                vals.append(int(line, 16))
            except ValueError:
                vals.append(0)
    return vals


def rgb565(v):
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    return bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))


def write_png(path, width, height, rows):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + r for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def convert(path, w, h, transpose, scale):
    vals = read_hex(path)
    vals += [0] * (w * h - len(vals))
    px = [rgb565(v) for v in vals[:w * h]]
    if transpose:
        out_w, out_h = h, w
        get = lambda x, y: px[x * w + y]
    else:
        out_w, out_h = w, h
        get = lambda x, y: px[y * w + x]
    rows = []
    for y in range(out_h):
        row = b''.join(get(x, y) * scale for x in range(out_w))
        rows.extend([row] * scale)
    dst = path.rsplit('.', 1)[0] + '.png'
    write_png(dst, out_w * scale, out_h * scale, rows)
    return dst


def main():
    ap = argparse.ArgumentParser(description='Panel-model RGB565 dump -> PNG')
    ap.add_argument('-W', '--width',  type=int, default=240, help='panel width (default 240)')
    ap.add_argument('-H', '--height', type=int, default=320, help='panel height (default 320)')
    ap.add_argument('--transpose', action='store_true', help='show the CL_SWAP frame buffer')
    ap.add_argument('--scale', type=int, default=2, help='pixel scale (default 2)')
    ap.add_argument('files', nargs='+')
    args = ap.parse_args()
    for f in args.files:
        print(convert(f, args.width, args.height, args.transpose, args.scale))
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    make_gfx.py (keen4)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# The level graphics in the form the Belenos GPU draws: Keen 4's 16 x 16
# tiles, masked 16 x 16 tiles and sprites, taken out of EGAGRAPH.CK4 (Huffman
# chunks of planar EGA data), written at 4 bits per pixel into GFX4BPP.CK4.
# The program reads one record from the card when it first draws it.
#
#   tools/make_gfx.py DATA_DIR      (EGAGRAPH, EGAHEAD, EGADICT, GFXINFOE)
#
# GFX4BPP.CK4, little-endian:
#   0   'K4BP', u16 version (1), u16 tiles16, u16 tiles16m, u16 sprites
#   12  u32 file offset of the tiles16 records, of the tiles16m records,
#       of the sprite directory (one u32 offset per sprite, 0: no sprite),
#       of the presence bits (tiles16, then tiles16m; 1: the chunk exists)
#   records:
#     tile16   128 bytes, 4 bpp
#     tile16m  32 bytes of mask (1 bpp), then 128 bytes, 4 bpp
#     sprite   the mask (width / 8 bytes per row), then the pixels (width / 2
#              bytes per row), at a 4-byte boundary
# Pixels in Belenos order: the first pixel of a row in the least significant
# bits (low nibble at 4 bpp, bit 0 at 1 bpp). A mask bit is 1 where the
# picture is drawn; colour bits under a clear mask bit are kept as EGA has
# them (the draw ORs them in, as the EGA latches did).
#----------------------------------------------------------------------------
import os, struct, sys


def huff_expand(src, explen, dic):
    out = bytearray()
    node = 254
    for byte in src:
        for bit in range(8):
            v = dic[node][(byte >> bit) & 1]
            if v < 256:
                out.append(v)
                if len(out) == explen:
                    return bytes(out)
                node = 254
            else:
                node = v - 256
    return bytes(out.ljust(explen, b'\0'))


def chunky(planes, w, h, mask=None):
    """Planar EGA (4 planes of w/8 bytes x h) -> 4 bpp rows, and the mask
    plane (1 = transparent) -> 1 bpp rows (1 = drawn)"""
    bw = w // 8
    pix = bytearray(w // 2 * h)
    msk = bytearray(bw * h) if mask is not None else None
    for y in range(h):
        for x in range(w):
            i = y * bw + x // 8
            b = 7 - (x & 7)
            c = 0
            for p in range(4):
                c |= ((planes[p][i] >> b) & 1) << p
            o = y * (w // 2) + x // 2
            pix[o] |= c << (4 * (x & 1))
            if msk is not None and not ((mask[i] >> b) & 1):
                msk[y * bw + x // 8] |= 1 << (x & 7)
    return pix, msk


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data')
    rd = lambda n: open(os.path.join(d, n), 'rb').read()
    graph, head, dic_b, info = rd('EGAGRAPH.CK4'), rd('EGAHEAD.CK4'), rd('EGADICT.CK4'), rd('GFXINFOE.CK4')
    dic = [struct.unpack_from('<HH', dic_b, 4 * i) for i in range(256)]
    gi = struct.unpack_from('<23H', info)
    (n8, n8m, n16, n16m, n32, n32m, o8, o8m, o16, o16m, o32, o32m,
     nbmp, nmsk, nspr, obmp, omsk, ospr, hbmp, hmsk, hspr, nbin, obin) = gi
    nchunks = len(head) // 3
    starts = [head[3 * i] | head[3 * i + 1] << 8 | head[3 * i + 2] << 16 for i in range(nchunks)]

    def chunk(c, explen=None):
        s = starts[c]
        if s == 0xFFFFFF:
            return None
        e = next((starts[k] for k in range(c + 1, nchunks) if starts[k] != 0xFFFFFF), len(graph))
        raw = graph[s:e]
        if explen is None:
            explen = struct.unpack_from('<I', raw)[0]
            raw = raw[4:]
        return huff_expand(raw, explen, dic)

    spr_tab = chunk(hspr)
    out = bytearray(32)
    have16, have16m = bytearray((n16 + 7) // 8), bytearray((n16m + 7) // 8)

    off16 = len(out)
    for t in range(n16):
        c = chunk(o16 + t, 128)
        if c is not None:
            have16[t // 8] |= 1 << (t & 7)
            pix, _ = chunky([c[32 * p:32 * p + 32] for p in range(4)], 16, 16)
        else:
            pix = bytes(128)
        out += pix

    off16m = len(out)
    for t in range(n16m):
        c = chunk(o16m + t, 160)
        if c is not None:
            have16m[t // 8] |= 1 << (t & 7)
            pix, msk = chunky([c[32 + 32 * p:64 + 32 * p] for p in range(4)], 16, 16, c[:32])
        else:
            pix, msk = bytes(128), bytes(32)
        out += msk + pix

    offdir = len(out)
    out += bytes(4 * nspr)
    for s in range(nspr):
        bw, h = struct.unpack_from('<HH', spr_tab, 18 * s)
        plane = bw * h
        c = chunk(ospr + s)
        if c is None or bw == 0 or h == 0:
            continue
        c = c.ljust(5 * plane, b'\0')
        pix, msk = chunky([c[plane * (p + 1):plane * (p + 2)] for p in range(4)], bw * 8, h, c[:plane])
        while len(out) & 3:
            out.append(0)
        struct.pack_into('<I', out, offdir + 4 * s, len(out))
        out += msk + pix

    offhave = len(out)
    out += have16 + have16m
    out[0:28] = b'K4BP' + struct.pack('<4H4I', 1, n16, n16m, nspr, off16, off16m, offdir, offhave)
    open(os.path.join(d, 'GFX4BPP.CK4'), 'wb').write(out)
    print('GFX4BPP.CK4: %d tiles16, %d tiles16m, %d sprites, %d bytes' % (n16, n16m, nspr, len(out)))

    # The title screen as a picture (the boot menu's preview: sd_install.py --image)
    try:
        from PIL import Image
    except ImportError:
        return
    import re
    title = int(re.search(r'%int PIC_TITLESCREEN (\d+)', rd('GFXCHUNK.CK4').decode('latin1')).group(1))
    bw, h = struct.unpack_from('<HH', chunk(hbmp), 4 * (title - obmp))
    planes = chunk(title)
    pix, _ = chunky([planes[bw * h * p:bw * h * (p + 1)] for p in range(4)], bw * 8, h)
    ega = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170), (170, 85, 0),
           (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
           (255, 85, 255), (255, 255, 85), (255, 255, 255)]
    img = Image.new('RGB', (bw * 8, h))
    img.putdata([ega[(pix[i // 2] >> (4 * (i & 1))) & 15] for i in range(bw * 8 * h)])
    img.save(os.path.join(d, 'TITLE.PNG'))
    print('TITLE.PNG: the title screen, %d x %d' % (bw * 8, h))


if __name__ == '__main__':
    main()

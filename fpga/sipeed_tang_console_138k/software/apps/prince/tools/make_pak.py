#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    make_pak.py (prince)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: GPL-3.0-or-later
# PALETTE_ROWS is picopop's (tools/assets/convert.py, Andrei Nigmatulin;
# picopop is GPL-3.0, see COPYING).
#----------------------------------------------------------------------------
"""Prince of Persia's resources for the Tang: one pack per DAT file.

    tools/make_pak.py GAMEDATA_DIR OUT_DIR

GAMEDATA_DIR holds the game's resources as SDLPoP distributes them: DAT files,
or for a DAT name X.DAT a folder X/ with resN.png (images, 16 colours),
resN.pal (sprite-set headers) and resN.bin (anything else).

Each pack (X.PAK) is read into the VRAM when the game opens X.DAT. Images
are kept at 4 bits per pixel (the 16 colours of their sprite set; 0 is
transparent) with the palette row of their set, which the blitter adds as it
draws: at 8 bits per pixel the Kid alone would take 180 KB.

X.PAK, little-endian:
  0    'PPAK', u16 count, u16 images
  8    count x { u16 resource id, u8 kind (0 raw, 1 image), u8 palette row,
                 u32 offset from the file's start, u32 length, u16 w, u16 h }
  then the data, each at a 4-byte boundary; an image is h rows of (w + 1) / 2
  bytes, the left pixel of a pair in the low nibble.

The palette rows come from the game's own calls (see PALETTE_ROWS, taken
from picopop's tools/assets/convert.py, which cites each one).
"""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from datfile import Dat, decode_image, looks_like_image, looks_like_shpl, read_shpl

DAT_FILES = [
    "DIGISND1.DAT", "DIGISND2.DAT", "DIGISND3.DAT", "FAT.DAT",
    "GUARD1.DAT", "GUARD2.DAT", "GUARD.DAT", "KID.DAT", "LEVELS.DAT",
    "MIDISND1.DAT", "MIDISND2.DAT", "PRINCE.DAT", "PV.DAT", "SHADOW.DAT",
    "SKEL.DAT", "TITLE.DAT", "VDUNGEON.DAT", "VIZIER.DAT", "VPALACE.DAT",
]

PALETTE_ROWS = {
    ("PRINCE.DAT", 700): 2, ("PRINCE.DAT", 150): 3,
    ("VDUNGEON.DAT", 200): 5, ("VPALACE.DAT", 200): 5,
    ("VDUNGEON.DAT", 360): 6, ("VPALACE.DAT", 360): 6,
    ("KID.DAT", 400): 7,
    ("GUARD1.DAT", 750): 8, ("GUARD2.DAT", 750): 8, ("FAT.DAT", 750): 8,
    ("SKEL.DAT", 750): 8, ("VIZIER.DAT", 750): 8, ("SHADOW.DAT", 750): 8,
    ("PV.DAT", 800): 9, ("PV.DAT", 850): 10, ("PV.DAT", 900): 10,
    ("TITLE.DAT", 40): 11, ("TITLE.DAT", 50): 12,
    ("PV.DAT", 950): 13, ("PV.DAT", 980): 14,
}
IMAGES_FROM = {("GUARD1.DAT", 750): "GUARD.DAT", ("GUARD2.DAT", 750): "GUARD.DAT"}
OPTGRAF_BASE, OPTGRAF_SET = 1200, 200


class DirDat:
    """A DAT given as a folder of resN.png / .pal / .bin files"""

    def __init__(self, path):
        from PIL import Image
        self.res, self.img = {}, {}
        for f in os.listdir(path):
            m = re.match(r'res(\d+)\.(png|pal|bin)$', f)
            if not m:
                continue
            rid = int(m.group(1))
            p = os.path.join(path, f)
            if m.group(2) == 'png':
                im = Image.open(p)
                assert im.mode == 'P', p
                px = bytes(im.tobytes())
                if max(px, default=0) >= 16:    # a few of SDLPoP's images: the nearest of the 16
                    pal = im.getpalette()
                    near = lambda c: min(range(1, 16), key=lambda k: sum((pal[3 * k + j] - pal[3 * c + j]) ** 2
                                                                         for j in range(3)))
                    px = bytes(v if v < 16 else near(v) for v in px)
                self.img[rid] = (im.size[0], im.size[1], px)
                self.res[rid] = b'IMG'
            else:
                self.res[rid] = open(p, 'rb').read()
        self.ids = sorted(self.res)

    def raw(self, rid):
        return self.res[rid]

    def image(self, rid):
        return self.img.get(rid)


def load(gamedata, name):
    p = os.path.join(gamedata, name)
    if os.path.isfile(p):
        d = Dat(p)
        d.image = lambda rid, d=d: (decode_image(d.raw(rid)) if looks_like_image(d.raw(rid)) else None)
        return d
    return DirDat(os.path.join(gamedata, name[:-4]))


def main():
    gamedata, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    dats = {n: load(gamedata, n) for n in DAT_FILES}

    owner = {}                                  # (dat, rid) -> palette row
    for (dat, sid), row in PALETTE_ROWS.items():
        raw = dats[dat].raw(sid)
        assert looks_like_shpl(raw), (dat, sid)
        n, _ = read_shpl(raw)
        img_dat = IMAGES_FROM.get((dat, sid), dat)
        for i in range(1, n + 1):
            owner.setdefault((img_dat, sid + i), row)
    for dat in ("VDUNGEON.DAT", "VPALACE.DAT"):
        for rid in dats[dat].ids:
            if rid > OPTGRAF_BASE and dats[dat].image(rid):
                owner.setdefault((dat, rid), PALETTE_ROWS[(dat, OPTGRAF_SET)])

    total = 0
    for name in DAT_FILES:
        d = dats[name]
        entries, blob = [], bytearray()
        nimg = 0
        for rid in d.ids:
            img = d.image(rid)
            while len(blob) & 3:
                blob.append(0)
            off = len(blob)
            if img is not None and (name, rid) in owner:
                w, h, px = img
                assert max(px, default=0) < 16, (name, rid)
                pitch = (w + 1) // 2
                for y in range(h):
                    row = px[y * w:(y + 1) * w]
                    for x in range(0, w, 2):
                        blob.append(row[x] | ((row[x + 1] << 4) if x + 1 < w else 0))
                entries.append((rid, 1, owner[(name, rid)], off, pitch * h, w, h))
                nimg += 1
            else:
                if img is not None:
                    print('%s res%d: an image no sprite set owns, left out' % (name, rid))
                    continue
                raw = d.raw(rid)
                blob += raw
                entries.append((rid, 0, 0, off, len(raw), 0, 0))
        base = 8 + 16 * len(entries)
        base = (base + 3) & ~3
        hdr = b'PPAK' + struct.pack('<HH', len(entries), nimg)
        hdr += b''.join(struct.pack('<HBBIIHH', rid, k, row, base + o, n, w, h)
                        for rid, k, row, o, n, w, h in entries)
        hdr = hdr.ljust(base, b'\0')
        open(os.path.join(out, name[:-4] + '.PAK'), 'wb').write(hdr + blob)
        total += len(hdr) + len(blob)
        print('%-12s %3d resources (%3d images) %7d bytes' % (name[:-4] + '.PAK', len(entries), nimg,
                                                             len(hdr) + len(blob)))
    print('total %d bytes' % total)


if __name__ == '__main__':
    main()

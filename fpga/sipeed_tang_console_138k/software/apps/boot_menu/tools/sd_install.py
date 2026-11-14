#!/usr/bin/env python3
"""Put an app on the microSD card of the boot menu.

    sd_install.py APP.elf CARD [--name NAME] [--in FOLDER] [--title TITLE] [--desc TEXT]
                               [--image PICTURE] [--add FILE]...
    sd_install.py --bin APP.elf OUT.bin

The first form writes CARD/APPS/NAME/APP.BIN, the app's program memory image
from 0x20000000 (what the J-Link would load), CARD/APPS/NAME/APP.CRC (its
CRC-32, that of zlib, as eight hex digits: the menu checks the image against
it while loading) and CARD/APPS/NAME/APP.TXT (a
title line and a description line) when a title or a description is given or
there is no APP.TXT yet; --image writes CARD/APPS/NAME/APP.IMG, the menu's
preview of the app, from a picture (PNG, JPEG, ...: needs Pillow); --add
copies other files the app reads into the same folder (8.3 names, upper
case). CARD is the card's mount point (e.g. /Volumes/DISK_ARVERN).
NAME defaults to the ELF's name, upper case, cut to 8 characters (FAT 8.3).
--in puts the app in a folder of programs, CARD/APPS/FOLDER/NAME/ (one
level only), which the menu lists after the programs and opens like one;
the folder gets an APP.TXT (its name as the title) when it has none yet, to
be edited like an app's.
The second form only writes the image.

The ELF must be the hardware build (APP.elf, not APP_sim.elf). An image must
end below the boot menu's launch routine, in the last 256 bytes of the
program memory. Pure Python, no host tool needed (Pillow for --image).

APP.IMG: a header of 12 bytes, "AIMG", width and height (16 bits each,
little-endian, at most 128 x 96), the number of colours N (one byte, 1 to
112) and three zero bytes; N colours of three bytes (red, green, blue); then
width x height pixels of one byte, row by row from the top, each a colour
number below N."""
import argparse, os, re, shutil, struct, sys, zlib

ROM_BASE = 0x20000000
APP_MAX  = 0x28000 - 256            # the launch routine sits above
IMG_W, IMG_H, IMG_COLOURS = 128, 96, 112


def rom_image(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        sys.exit("%s: not a 32-bit ELF file" % path)
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    img = bytearray()
    for i in range(phnum):
        p_type, p_offset, _, p_paddr, p_filesz = struct.unpack_from("<5I", data, phoff + i * phentsize)
        if p_type != 1 or p_filesz == 0:              # PT_LOAD with file contents
            continue
        if not (ROM_BASE <= p_paddr and p_paddr + p_filesz <= ROM_BASE + APP_MAX):
            sys.exit("%s: a segment at 0x%08x (+%d bytes) is outside 0x%08x..0x%08x"
                     % (path, p_paddr, p_filesz, ROM_BASE, ROM_BASE + APP_MAX))
        off = p_paddr - ROM_BASE
        if len(img) < off + p_filesz:
            img.extend(bytes(off + p_filesz - len(img)))
        img[off:off + p_filesz] = data[p_offset:p_offset + p_filesz]
    if not img:
        sys.exit("%s: no program memory contents" % path)
    img.extend(bytes(-len(img) % 4))
    return bytes(img)


def preview(path):
    """A picture as APP.IMG: fitted into IMG_W x IMG_H, IMG_COLOURS colours."""
    try:
        from PIL import Image
    except ImportError:
        sys.exit("--image needs Pillow (pip install Pillow)")
    try:
        im = Image.open(path).convert("RGB")
    except OSError as e:
        sys.exit("%s: %s" % (path, e))
    im.thumbnail((IMG_W, IMG_H), getattr(Image, "Resampling", Image).LANCZOS)
    q = im.quantize(colors=IMG_COLOURS, dither=0)
    pal = q.getpalette()[:3 * IMG_COLOURS]
    px = q.tobytes()
    n = max(px) + 1
    return (b"AIMG" + struct.pack("<HHB3x", q.width, q.height, n) + bytes(pal[:3 * n]) + px)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", action="store_true", help="only write the image to the second argument")
    ap.add_argument("elf")
    ap.add_argument("dest", help="card mount point (or output file with --bin)")
    ap.add_argument("--name", help="folder name under /APPS (8.3, upper case)")
    ap.add_argument("--in", dest="group", metavar="FOLDER", help="a folder of programs under /APPS (8.3, upper case)")
    ap.add_argument("--title", help="title line of APP.TXT")
    ap.add_argument("--desc", help="description line of APP.TXT")
    ap.add_argument("--image", metavar="PICTURE", help="the menu's preview of the app (APP.IMG)")
    ap.add_argument("--add", action="append", default=[], metavar="FILE",
                    help="another file for the app's folder (repeatable)")
    a = ap.parse_args()

    if os.path.basename(a.elf).endswith("_sim.elf"):
        sys.exit("%s is the simulation build: use the hardware ELF" % a.elf)
    img = rom_image(a.elf)
    pic = preview(a.image) if a.image else None
    if a.bin:
        open(a.dest, "wb").write(img)
        return

    stem = os.path.splitext(os.path.basename(a.elf))[0]
    name = (a.name or re.sub(r"[^A-Z0-9_]", "", stem.upper())[:8])
    if not re.fullmatch(r"[A-Z0-9_]{1,8}", name):
        sys.exit("folder name %r: 1 to 8 characters among A-Z 0-9 _" % name)
    if not os.path.isdir(a.dest):
        sys.exit("%s: no such directory (is the card mounted?)" % a.dest)
    if a.group and not re.fullmatch(r"[A-Z0-9_]{1,8}", a.group):
        sys.exit("folder name %r: 1 to 8 characters among A-Z 0-9 _" % a.group)
    parent = os.path.join(a.dest, "APPS", *([a.group] if a.group else []))
    if a.group:
        if os.path.exists(os.path.join(parent, "APP.BIN")):
            sys.exit("%s holds a program: not a folder of programs" % parent)
        os.makedirs(parent, exist_ok=True)
        gtxt = os.path.join(parent, "APP.TXT")
        if not os.path.exists(gtxt):
            with open(gtxt, "w", newline="\r\n") as f:
                f.write(a.group.capitalize() + "\n\n")
    folder = os.path.join(parent, name)
    os.makedirs(folder, exist_ok=True)
    open(os.path.join(folder, "APP.BIN"), "wb").write(img)
    open(os.path.join(folder, "APP.CRC"), "w", newline="\r\n").write("%08X\n" % zlib.crc32(img))
    txt = os.path.join(folder, "APP.TXT")
    if a.title or a.desc or not os.path.exists(txt):
        with open(txt, "w", newline="\r\n") as f:
            f.write((a.title or stem) + "\n" + (a.desc or "") + "\n")
    if pic:
        open(os.path.join(folder, "APP.IMG"), "wb").write(pic)
        print("  + APP.IMG (%d x %d, %d colours)" % (pic[4] | pic[5] << 8, pic[6] | pic[7] << 8, pic[8]))
    for f in a.add:
        base = os.path.basename(f).upper()
        if not re.fullmatch(r"[A-Z0-9_]{1,8}(\.[A-Z0-9_]{1,3})?", base):
            sys.exit("%s: not an 8.3 name" % f)
        shutil.copyfile(f, os.path.join(folder, base))
        print("  + %s" % base)
    print("%s -> %s (%d bytes, %d KB of %d KB)" % (a.elf, folder, len(img), (len(img) + 1023) // 1024,
                                                  APP_MAX // 1024))


if __name__ == "__main__":
    main()

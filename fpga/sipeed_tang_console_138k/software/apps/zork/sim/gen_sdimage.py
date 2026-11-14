#!/usr/bin/env python3
"""The simulated microSD card: a 2 MB image laid out like a real card (MBR,
one FAT partition, formatted by macOS) with the card's directory structure,
as sd_image.bin and sd_image.mem (one byte per line, for $readmemh).

   gen_sdimage.py out_dir file...

Card layout (the convention of every Tang program on the card):
   /APPS/<APP>/        one folder per program: its data, its saves; the
                       program runs with this folder as current directory
   /MEDIA/PICTURES/    shared pictures
   /MEDIA/MUSIC/       shared music
The given files go to /APPS/ZORK (the story files of stories/)."""
import os, shutil, subprocess, sys, tempfile

SIZE = 2 * 1024 * 1024
out, files = sys.argv[1], sys.argv[2:]

def run(*cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

with tempfile.TemporaryDirectory() as tmp:
    dmg, mnt = os.path.join(tmp, 'card.dmg'), os.path.join(tmp, 'mnt')
    run('hdiutil', 'create', '-size', '2m', '-layout', 'MBRSPUD', '-fs', 'MS-DOS FAT12',
        '-volname', 'ARVERN', '-o', dmg)
    run('hdiutil', 'attach', dmg, '-nobrowse', '-mountpoint', mnt)
    try:
        for d in ('APPS/ZORK', 'MEDIA/PICTURES', 'MEDIA/MUSIC'):
            os.makedirs(os.path.join(mnt, d))
        for f in files:
            shutil.copyfile(f, os.path.join(mnt, 'APPS', 'ZORK', os.path.basename(f).upper()))
        run('dot_clean', '-m', mnt)
    finally:
        run('hdiutil', 'detach', mnt)
    run('hdiutil', 'convert', dmg, '-format', 'UDTO', '-o', os.path.join(tmp, 'card'))
    img = open(os.path.join(tmp, 'card.cdr'), 'rb').read()

if len(img) > SIZE:
    sys.exit("image larger than the card model (%d bytes)" % len(img))
img = img.ljust(SIZE, b'\0')
open(os.path.join(out, 'sd_image.bin'), 'wb').write(img)
with open(os.path.join(out, 'sd_image.mem'), 'w') as f:
    f.write('\n'.join('%02x' % b for b in img) + '\n')
print("%d files in /APPS/ZORK, %d-byte image" % (len(files), len(img)))

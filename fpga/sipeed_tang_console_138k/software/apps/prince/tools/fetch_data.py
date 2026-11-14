#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    fetch_data.py (prince)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Fetches the game data, which this repository does not hold: the data/
# folder of SDLPoP's repository at a fixed commit (the DOS game's resources,
# as DAT files or folders of PNG/BIN files), checked against the SHA-256 of
# its file list, and makes the files the port reads:
#   *.PAK         one pack per DAT file (make_pak.py, needs Pillow)
#   MUSnn.ADP     the MIDI tunes played through an OPL model
#                 (render_music.py)
#   TITLE.PNG     the title screen, for the boot menu's preview
#
#   tools/fetch_data.py [OUT_DIR]       (default: data/)
#
# Put the *.PAK and MUS*.ADP files of OUT_DIR in the card's /APPS/PRINCE.
#----------------------------------------------------------------------------
import hashlib, io, os, subprocess, sys, tarfile, tempfile, urllib.request

COMMIT = "3c5add5fb7f83d4ceb542823ab66d00146c4271b"
URL = "https://codeload.github.com/NagyD/SDLPoP/tar.gz/" + COMMIT
# SHA-256 of the lines "<path under data/> <SHA-256 of the file>\n", sorted:
# the files themselves, whatever the compression of the archive
DATA_SHA256 = "3a232ae47a8a60236f67eac68d25e7bd15c9f4e88e2afdf6be0505b613f94dc9"
HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "data")
    os.makedirs(out, exist_ok=True)
    print("fetching %s" % URL)
    with urllib.request.urlopen(URL) as r:
        archive = r.read()
    with tempfile.TemporaryDirectory() as tmp:
        game = os.path.join(tmp, "data")
        digests = []
        with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
            for m in tf.getmembers():
                parts = m.name.split("/", 1)
                if not (m.isfile() and len(parts) == 2 and parts[1].startswith("data/")):
                    continue
                rel = parts[1][len("data/"):]
                if rel.startswith("/") or ".." in rel.split("/"):
                    sys.exit("unexpected path in the archive: %s" % m.name)
                f = tf.extractfile(m)
                if f is None:
                    continue
                data = f.read()
                digests.append("%s %s\n" % (rel, hashlib.sha256(data).hexdigest()))
                path = os.path.join(game, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                open(path, "wb").write(data)
        digest = hashlib.sha256("".join(sorted(digests)).encode()).hexdigest()
        if digest != DATA_SHA256:
            sys.exit("unexpected game data (SHA-256 %s)" % digest)
        subprocess.run([sys.executable, os.path.join(HERE, "make_pak.py"), game, out], check=True)
        from PIL import Image
        Image.open(os.path.join(game, "TITLE", "res51.png")).convert("RGB").save(os.path.join(out, "TITLE.PNG"))
    subprocess.run([sys.executable, os.path.join(HERE, "render_music.py"), out, out], check=True)
    print("data in %s" % out)


if __name__ == "__main__":
    main()

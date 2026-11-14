#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    fetch_data.py (keen4)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Fetches the game data, which this repository does not hold: the shareware
# episode of Commander Keen 4 (v1.4) as Omnispeak's author distributes it,
# the archive and every file checked against their SHA-256; adds Omnispeak's
# own data files (omnispeak/data/keen4: headers, dictionaries, the action and
# episode scripts) and makes the files the port reads besides them:
#   GFX4BPP.CK4   the tiles and sprites at 4 bits per pixel (make_gfx.py)
#   MUSnn.ADP     the songs (render_music.c, built with the host's cc)
#   TITLE.PNG     the title screen, for the boot menu's preview (with Pillow)
#
#   tools/fetch_data.py [OUT_DIR]       (default: data/)
#
# Put the *.CK4 and MUS*.ADP files of OUT_DIR in the card's /APPS/KEEN4.
#----------------------------------------------------------------------------
import glob, hashlib, io, os, shutil, subprocess, sys, urllib.request, zipfile

URL = "https://davidgow.net/keen/4keen14.zip"
ZIP_SHA256 = "f92e2d02d3779a2e717a50287ef3ed116c22fdf7bcc12ccf676a412e97195cfe"
FILES = {
    "AUDIO.CK4":    "66dcf6070a4bc5c8933815b15dab7145ddcccb959f8f43af2a0449b085a04366",
    "EGAGRAPH.CK4": "9805168fb910385ad40455541cbcfa1aaa61f9d81b2504274ee0ca0ec4b71550",
    "GAMEMAPS.CK4": "01186f8cb99257b69a4f475186deb9ba79b872ed52378131c79c21f6344753ea",
}
HERE = os.path.dirname(os.path.abspath(__file__))
OMNI = os.path.join(HERE, "..", "omnispeak", "data", "keen4")


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "data")
    os.makedirs(out, exist_ok=True)
    print("fetching %s" % URL)
    with urllib.request.urlopen(URL) as r:
        z = r.read()
    if sha256(z) != ZIP_SHA256:
        sys.exit("unexpected archive (SHA-256 %s)" % sha256(z))
    with zipfile.ZipFile(io.BytesIO(z)) as zf:
        names = {n.upper(): n for n in zf.namelist()}
        for name, digest in FILES.items():
            data = zf.read(names[name])
            if sha256(data) != digest:
                sys.exit("%s: unexpected SHA-256" % name)
            open(os.path.join(out, name), "wb").write(data)
    for f in glob.glob(os.path.join(OMNI, "*.CK4")):
        shutil.copyfile(f, os.path.join(out, os.path.basename(f)))
    subprocess.run([sys.executable, os.path.join(HERE, "make_gfx.py"), out], check=True)
    opl = os.path.join(HERE, "..", "omnispeak", "src", "opl")
    exe = os.path.join(HERE, "render_music")
    src = exe + ".c"
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        subprocess.run(["cc", "-O2", "-I" + opl, src, os.path.join(opl, "dbopl.c"), "-lm", "-o", exe], check=True)
    subprocess.run([exe, out, out], check=True)
    print("data in %s" % out)


if __name__ == "__main__":
    main()

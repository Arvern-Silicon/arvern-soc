#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    fetch_data.py (wolf4SDL)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Fetches the game data, which this repository does not hold: id Software's
# shareware episode of Wolfenstein 3D (v1.4), from the Internet Archive, its
# archive and every file checked against their SHA-256; then makes the files
# the port reads besides it, with the tools of this folder (a host C compiler,
# cc):
#   SOUNDS.SFX       the sound effects (tools/render_sfx)
#   music/MUSnn.ADP  the songs (tools/render_music)
#
#   tools/fetch_data.py [OUT_DIR]       (default: data/)
#
# Put the .WL1 files, SOUNDS.SFX and music/*.ADP in the card's /APPS/WOLF3D.
#----------------------------------------------------------------------------
import hashlib, io, os, subprocess, sys, urllib.request, zipfile

URL = "https://archive.org/download/wolf3dsw/wolf3dsw.zip"
ZIP_SHA256 = "76ee5e73e7d6341aefff620989bb5f828e9d295982afd5415b62dee7fe54eb64"
FILES = {
    "AUDIOHED.WL1": "39351624ae6f8eef4b873e060c1a6f3e5ee7e81c4939c485275a89b145336338",
    "AUDIOT.WL1":   "1e2c9ae30398a14c61a4ddd39aabaa0dbcc984cc4924a3e51df75574c259cfb9",
    "GAMEMAPS.WL1": "a6a6654b342f2c027bcb22bfce0a41f9fc0063b775e9e4da0c771970e53e11aa",
    "MAPHEAD.WL1":  "3458f661c9b875bca99ea22a7267771fad8f2a33c699ea732cf7c3322909bf8c",
    "VGADICT.WL1":  "59878fec65f033b00dbb1240317d1793f5858213dc8013b4d68f9ac8b45b0c80",
    "VGAGRAPH.WL1": "d5176f843c53415132db199c19f38591eaf3cedd35a4f7eb2698c1865d83030d",
    "VGAHEAD.WL1":  "f4cc800dc8444373092d4eaa5d6ab59d63d23a510a9d38b73ca9b3dbb700d18b",
    "VSWAP.WL1":    "698f217257e2cbb951a4d110ba09140291f38d0121b3784d1d6be59c03a6b47b",
}
HERE = os.path.dirname(os.path.abspath(__file__))
OPL = os.path.join(HERE, "..", "Wolf4SDL", "mame")


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def tool(name):
    exe = os.path.join(HERE, name)
    src = exe + ".c"
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        subprocess.run(["cc", "-O2", "-I" + OPL, src, os.path.join(OPL, "fmopl.c"), "-lm", "-o", exe], check=True)
    return exe


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "data")
    os.makedirs(out, exist_ok=True)
    print("fetching %s" % URL)
    with urllib.request.urlopen(URL) as r:
        z = r.read()
    if sha256(z) != ZIP_SHA256:
        sys.exit("wolf3dsw.zip: unexpected SHA-256 (%s): not the archive this port was made with" % sha256(z))
    with zipfile.ZipFile(io.BytesIO(z)) as zf:
        for name, digest in FILES.items():
            b = zf.read(name)
            if sha256(b) != digest:
                sys.exit("%s: unexpected SHA-256" % name)
            open(os.path.join(out, name), "wb").write(b)
    print("  %d game files in %s" % (len(FILES), out))

    p = lambda n: os.path.join(out, n)
    subprocess.run([tool("render_sfx"), p("VSWAP.WL1"), p("AUDIOHED.WL1"), p("AUDIOT.WL1"), p("SOUNDS.SFX")],
                   check=True, stdout=subprocess.DEVNULL)
    os.makedirs(p("music"), exist_ok=True)
    subprocess.run([tool("render_music"), p("AUDIOHED.WL1"), p("AUDIOT.WL1"), p("music")],
                   check=True, stdout=subprocess.DEVNULL)
    print("  SOUNDS.SFX and music/ made")


if __name__ == "__main__":
    main()

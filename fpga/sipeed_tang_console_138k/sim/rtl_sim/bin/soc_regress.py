#!/usr/bin/env python3
# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      soc_regress.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------
# The board regression (run/run_all): every test of a tier runs the firmware
# on the SoC RTL (Verilator, ./run <test> -nodump) in its own directory
# run_<test>/ beside run/, side by side. A test passes when
#   1. the stimulus reports its PASSED banner (and no FAILED banner),
#   2. the panel images it saved (frame_log/fb_<n>.hex) match its golden list
#      golden/<test>.txt, when the test has one. The images compared are the
#      sync points: when the stimulus logs the firmware's LED with each frame
#      ("frame <n> done ... (LED=0x..)"), the last frame of every LED step and
#      the last frame of the run, so a change of CPU or GPU timing (a frame
#      caught while a scene is being drawn) does not count; otherwise every
#      distinct image in order. A different pixel at a sync point fails,
#   3. the Belenos GPU trace (slipstream.log) passes slipstream_summary.py
#      (every primitive opened and closed once, no error), when it holds a
#      command.
# -bless writes the golden lists of the tests that passed 1 and 3 (check
# their PNG images, run_<test>/frame_log/fb_*.png, before committing them).
#
# The firmware is built first, one application after the other (the
# applications share libraries), then the simulations run in parallel.
#
#   soc_regress.py [-j N] [--tier quick|nightly] [--bless] [test ...]
# ----------------------------------------------------------------------------

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SIM_DIR = Path(__file__).resolve().parents[1]           # sim/rtl_sim
RUN_DIR = SIM_DIR / "run"
GOLDEN_DIR = SIM_DIR / "golden"
APPS_DIR = SIM_DIR.parents[1] / "software" / "apps"
SLIPSTREAM = (SIM_DIR.parents[4] / "arvern-belenos" / "sim" / "rtl_sim" / "bin"
              / "slipstream_summary.py")

# quick: the deterministic tests without the SD card (minutes each);
# nightly adds the SD-card applications and the games (tens of minutes each).
# A stimulus without its own limit ("// defines: -D TIMEOUT_MS=...") gets
# DEFAULT_TIMEOUT_MS here instead of the bench's 500 ms: the demos' -DSIMULATION
# passes run longer than that on this board
DEFAULT_TIMEOUT_MS = 3000
TIMEOUT_MS = {"demo_effects": 6000}           # tests that need longer than the default
# A test may name a build variant, "<test>@<variant>": the bench defines below
# (the HDMI board, BOARD_HDMI, instead of the RGB panel); it runs in
# run_<test>_<variant>/ and has its own golden list
VARIANTS = {"hdmi": "-D BOARD_HDMI"}

TIERS = {
    "quick":   ["tst_blns", "tst_blns@hdmi", "tst_vram_starve", "demo_ui", "demo_text", "demo_effects",
                "demo_arvern_logo", "demo_mode7", "demo_water", "demo_raycaster"],
    "nightly": ["boot_menu", "picture_viewer", "audio_player", "zork", "prince",
                "keen4", "wolf4SDL", "lvgl_demo", "lvgl_demo@hdmi"],
}
TIERS["nightly"] = TIERS["quick"] + TIERS["nightly"]


def build(test):
    """Build the firmware (board and simulation ELFs) in its application."""
    r = subprocess.run(["make"], cwd=APPS_DIR / test, capture_output=True, text=True)
    return r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]


def frames(run_dir):
    """SHA-1 of the sync-point panel images (see the header), in order, with
    their file."""
    out = []
    fl = run_dir / "frame_log"
    files = sorted(fl.glob("fb_*.hex"), key=lambda f: int(re.findall(r"\d+", f.name)[-1]))
    log = (run_dir / "sim.log").read_text(errors="replace") if (run_dir / "sim.log").exists() else ""
    led = {int(m.group(1)): m.group(2) for m in
           re.finditer(r"frame (\d+) done at .*?\(LED=0x([0-9a-fA-F]+)\)", log)}
    if led and files:
        idx = [int(re.findall(r"\d+", f.name)[-1]) for f in files]
        for k, f in zip(idx, files):
            if k == idx[-1] or led.get(k + 1) != led.get(k):
                out.append((hashlib.sha1(f.read_bytes()).hexdigest(), f.name))
        return out
    for f in files:
        h = hashlib.sha1(f.read_bytes()).hexdigest()
        if not out or out[-1][0] != h:
            out.append((h, f.name))
    return out


def golden_read(test):
    p = GOLDEN_DIR / f"{test}.txt"
    if not p.exists():
        return None
    return [ln.split()[0] for ln in p.read_text().splitlines()
            if ln.strip() and not ln.startswith("#")]


def golden_write(test, fr):
    GOLDEN_DIR.mkdir(exist_ok=True)
    lines = [f"# {test}: SHA-1 of the sync-point panel images, in order (soc_regress.py --bless)"]
    lines += [f"{h}  {name}" for h, name in fr]
    (GOLDEN_DIR / f"{test}.txt").write_text("\n".join(lines) + "\n")


def run_one(spec, bless):
    test, _, variant = spec.partition("@")
    rd = SIM_DIR / ("run_" + spec.replace("@", "_"))
    if rd.exists():
        shutil.rmtree(rd)
    rd.mkdir()
    shutil.copy2(RUN_DIR / "run", rd / "run")
    if (RUN_DIR / "waivers.vlt").exists():
        shutil.copy2(RUN_DIR / "waivers.vlt", rd / "waivers.vlt")
    log = rd / "sim.log"
    env = dict(os.environ)
    # no instruction trace (asphalt.log grows by gigabytes over a long run)
    env["SIMULATION_DEFINES"] = (env.get("SIMULATION_DEFINES", "") + " -D NOTRACE").strip()
    if variant:
        env["SIMULATION_DEFINES"] = (env.get("SIMULATION_DEFINES", "") + " " + VARIANTS[variant]).strip()
    stim = (SIM_DIR / "src" / f"{test}.v").read_text(errors="replace")
    if not re.search(r"^//\s*defines:.*TIMEOUT_MS", stim, re.M):
        env["SIMULATION_DEFINES"] = (env.get("SIMULATION_DEFINES", "") +
                                     f" -D TIMEOUT_MS={TIMEOUT_MS.get(test, DEFAULT_TIMEOUT_MS)}").strip()
    with log.open("w") as fh:
        subprocess.run(["./run", test, "-nodump"], cwd=rd, stdout=fh, stderr=subprocess.STDOUT, env=env)
    text = log.read_text(errors="replace")
    notes = []
    passed = re.search(r"PASSED", text) is not None and re.search(r"FAILED", text) is None
    if not passed:
        notes.append("stimulus did not pass")
    ss = rd / "slipstream.log"
    if ss.exists() and SLIPSTREAM.exists() and re.search(r"^\s*\d+\s+END\s", ss.read_text(errors="replace"), re.M):
        r = subprocess.run([sys.executable, str(SLIPSTREAM), str(ss), "--quiet"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            passed = False
            notes.append("slipstream check failed")
    fr = frames(rd)
    gname = spec.replace("@", "_")
    gold = golden_read(gname)
    status = "PASS" if passed else "FAIL"
    if passed and bless and fr:
        golden_write(gname, fr)
        notes.append(f"blessed {len(fr)} images")
    elif gold is not None:
        got = [h for h, _ in fr]
        if got != gold:
            status = "FAIL"
            i = next((k for k in range(min(len(got), len(gold))) if got[k] != gold[k]),
                     min(len(got), len(gold)))
            where = fr[i][1] if i < len(fr) else "end of run"
            notes.append(f"images differ from golden at distinct image {i} ({where}; "
                         f"{len(got)} vs {len(gold)} distinct)")
        else:
            notes.append(f"{len(got)} images match golden")
    elif fr:
        notes.append(f"{len(fr)} images, no golden list")
    if status == "PASS" and not bless:
        shutil.rmtree(rd / "obj_dir", ignore_errors=True)
    return spec, status, "; ".join(notes)


def main():
    ap = argparse.ArgumentParser(description="arvern Tang board regression")
    ap.add_argument("tests", nargs="*")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--tier", default="quick", choices=sorted(TIERS))
    ap.add_argument("--bless", action="store_true")
    args = ap.parse_args()
    tests = args.tests or TIERS[args.tier]

    built, done = [], {}
    for spec in tests:
        t, _, v = spec.partition("@")
        if (not (APPS_DIR / t).is_dir() or not (SIM_DIR / "src" / f"{t}.v").exists() or
                (v and v not in VARIANTS)):
            print(f"  {spec:<20} MISSING")
            continue
        if t not in done:
            done[t] = build(t)
            print(f"  {t:<20} build {'ok' if done[t][0] else 'FAILED'}", flush=True)
            if not done[t][0]:
                print(done[t][1])
        if done[t][0]:
            built.append(spec)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda t: run_one(t, args.bless), built))

    print("\n" + "=" * 78)
    print(f"  arvern Tang board regression -- {len(results)} tests")
    print("=" * 78)
    lines = [f"{t:<20} {s:<5} {n}" for t, s, n in results]
    print("\n".join(lines))
    (SIM_DIR / "regress_summary.log").write_text("\n".join(lines) + "\n")
    bad = sum(1 for _, s, _ in results if s != "PASS") + (len(tests) - len(built))
    print(f"  passed: {len(results) - bad + (len(tests) - len(built))}  failed: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

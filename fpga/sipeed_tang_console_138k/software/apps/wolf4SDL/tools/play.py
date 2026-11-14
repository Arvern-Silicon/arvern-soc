# play.py elf [load]: drive Wolf3D over the J-Link (gamepad injection) and log counters every second
import os, sys, time, subprocess, pylink

ELF = sys.argv[1]
LOAD = len(sys.argv) > 2
HERE = os.path.dirname(os.path.abspath(__file__))
TK = 5555.556                                   # mtime ticks per ms

sym = {}
for l in subprocess.run(["riscv-none-elf-nm", ELF], capture_output=True, text=True).stdout.splitlines():
    f = l.split()
    if len(f) == 3:
        sym[f[2]] = int(f[0], 16)

L, R, U, D, A, B, X, Y, SEL, ST = [1 << i for i in range(10)]


def jlink_cmds(cmds):
    cf = os.path.join(HERE, "cmd.jlink")
    with open(cf, "w") as fh:
        fh.write("\n".join(cmds) + "\nqc\n")
    return subprocess.run(["JLinkExe", "-device", "RISC-V", "-if", "JTAG", "-speed", "4000", "-jtagconf", "-1,-1",
                           "-autoconnect", "1", "-nogui", "1", "-ExitOnError", "1", "-CommandFile", cf],
                          text=True, capture_output=True).stdout


if LOAD:
    out = jlink_cmds(["h", "loadfile " + ELF, "r", "g"])
    print("load:", "ok" if "Downloading file" in out else out[-400:], flush=True)
    time.sleep(50)

j = pylink.JLink()
j.open()
j.set_tif(pylink.enums.JLinkInterfaces.JTAG)
j.set_speed(4000)
j.connect("RISC-V")


def halt():
    j._dll.JLINKARM_Halt()


def w32(name, v):
    halt()
    j.memory_write32(sym[name], [v])
    j.restart()


NAMES = ("PMMisses", "PMMissTicks", "CAGrLoads", "CAGrTicks", "arv_read_calls", "arv_read_bytes", "arv_read_ticks")


def rd():
    halt()
    v = {n: j.memory_read32(sym[n], 1)[0] for n in NAMES}
    v["fps"] = j.memory_read32(sym["fps"], 1)[0]
    v["log"] = j.memory_read16(sym["CAGrLog"], 16)
    v["ingame"] = j.memory_read8(sym["ingame"], 1)[0]
    v["demo"] = j.memory_read8(sym["demoplayback"], 1)[0]
    j.memory_write32(sym["fpscounter"], [1])
    j.restart()
    return v


prev = rd()
t0 = time.time()


def log(tag):
    global prev
    v = rd()
    d = {k: (v[k] - prev[k]) & 0xFFFFFFFF for k in NAMES}
    print("%5.1fs %-10s fps %2d ingame %d demo %d | pages %3d %6.1fms | gr %3d %6.1fms | reads %5d %7dB %6.1fms" %
          (time.time() - t0, tag, v["fps"], v["ingame"], v["demo"], d["PMMisses"], d["PMMissTicks"] / TK,
           d["CAGrLoads"], d["CAGrTicks"] / TK, d["arv_read_calls"], d["arv_read_bytes"], d["arv_read_ticks"] / TK),
          flush=True)
    n = d["CAGrLoads"]
    if n:
        ids = [v["log"][(v["CAGrLoads"] - 1 - i) & 15] for i in range(min(n, 16))]
        print("        chunks:", ids[::-1], flush=True)
    prev = v


def press(b, t=0.3, after=1.5, tag=None):
    w32("arv_pad_inject", b)
    time.sleep(t)
    w32("arv_pad_inject", 0)
    time.sleep(after)
    if tag:
        log(tag)


for b, tag in ((ST, "start"), (A, "newgame"), (A, "episode"), (A, "difficulty")):
    press(b, tag=tag)
time.sleep(3)
log("loaded")

for k in range(int(os.environ.get("STEPS", "40"))):
    if k % 8 == 4:
        press(X, 0.2, 0.3)
    hold = U if k % 10 < 7 else (R if k % 20 < 10 else L)
    w32("arv_pad_inject", hold | (A if k % 6 == 0 else 0))
    time.sleep(1.0)
    log("fwd" if hold == U else "turn")
w32("arv_pad_inject", 0)
j.close()


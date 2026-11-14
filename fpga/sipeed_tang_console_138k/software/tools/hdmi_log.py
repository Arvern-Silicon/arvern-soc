#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    hdmi_log.py (Tang Console 138K)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
"""The HDMI link of the running program, logged through a J-Link without
halting the core: a time-stamped line at each change of the monitor's
hot-plug line (DPI_STATUS.PRESENT), of the link (DPI_CFG.LCD_ON) and of the
scan (DPI_STATUS.LINE moving or not); with the program's ELF, also its
restart counter and its watch state (hdmi_restarts, hdmi_state: demo kit
and boot menu; Wolfenstein's own names alike).

    hdmi_log.py [--elf PROGRAM.elf] [--seconds N]      (Ctrl-C to stop)
"""
import argparse, os, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../apps/boot_menu/tools'))
from sdlink import JLink, SdLinkError                 # J-Link Commander through its prompt

DPI_CFG, DPI_STATUS = 0x10050180, 0x10050194


def symbols(elf):
    out = subprocess.run(['riscv-none-elf-nm', elf], capture_output=True, text=True).stdout
    return {l.split()[2]: int(l.split()[0], 16) for l in out.splitlines() if len(l.split()) == 3}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf')
    ap.add_argument('--seconds', type=float, default=0.0)
    ap.add_argument('--speed', type=int, default=4000)
    a = ap.parse_args()
    sym = symbols(a.elf) if a.elf else {}
    j = JLink(a.speed)
    t0 = time.time()
    prev, last_line = None, None
    print('time(s)  HPD  link  scan    restarts  watch')
    try:
        while not a.seconds or time.time() - t0 < a.seconds:
            cfg = j.rd(DPI_CFG)[0]
            st = j.rd(DPI_STATUS)[0]
            line = (st >> 16) & 0x1FFF
            scan = 'moving' if line != last_line else 'still'
            last_line = line
            restarts = j.rd(sym['hdmi_restarts'])[0] if 'hdmi_restarts' in sym else None
            watch = j.rd(sym['hdmi_state'])[0] if 'hdmi_state' in sym else None
            cur = ((st >> 8) & 1, cfg & 1, scan, restarts, watch)
            if cur != prev:
                print('%7.2f  %-3s  %-4s  %-6s  %8s  %s' % (time.time() - t0, 'up' if cur[0] else 'low',
                      'on' if cur[1] else 'off', cur[2], '' if restarts is None else restarts,
                      '' if watch is None else ('idle', 'settle', 'off', 'probe')[watch] if watch < 4 else watch))
                sys.stdout.flush()
                prev = cur
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    except SdLinkError as e:
        sys.exit('hdmi_log: %s' % e)
    finally:
        j.close()


if __name__ == '__main__':
    main()

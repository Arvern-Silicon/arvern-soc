#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    sdlink.py (boot_menu)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
"""The Tang's microSD card from the host, through a J-Link, while the boot
menu runs (its SD link, sdlink.h): the menu does the file operations with
its FatFs, this script fills its mailbox and moves the data, the core never
halted. Paths on the card are absolute 8.3 names (upper case).

    sdlink.py ls [CARD_DIR]                    the entries (default /)
    sdlink.py get CARD_FILE [LOCAL]            a file from the card
    sdlink.py put LOCAL CARD_PATH              a file onto the card (CARD_PATH a
                                               folder: same name in it)
    sdlink.py rm CARD_PATH                     a file or an empty folder
    sdlink.py mkdir CARD_DIR
    sdlink.py sync LOCAL_DIR CARD_DIR          the folder tree onto the card: the
                                               files that differ (size, CRC-32)
                                               and the missing folders

A file is written under a temporary name in its folder, checked against its
CRC-32 computed by the menu, then renamed: an interrupted copy never leaves
a half-written file under the real name. Uses J-Link Commander (JLinkExe)
only. --speed sets the JTAG clock (kHz, default 4000).
As a library: SdLink() opens a session (the menu shows its panel), its
methods are the commands, close() ends it (the menu reads the card again)."""
import argparse, os, pty, re, select, subprocess, sys, tempfile, time, zlib

MAILBOX, MAGIC = 0x8001FE00, 0x4B4C4453
HELLO, LIST, STAT, READ, WRITE, MKDIR, RENAME, DELETE, CRC, BYE = range(1, 11)
# mailbox words (sdlink_box_t)
W_MAGIC, W_VERSION, W_BUF, W_SIZE, W_SEQ, W_DONE, W_CMD, W_STATUS, W_OFFSET, W_LENGTH, W_RESULT, W_ALIVE = range(12)
PATH_OFF, PATH2_OFF, PATH_LEN = 16 * 4, 16 * 4 + 120, 120
FRESULT = {0: 'ok', 1: 'disk error', 2: 'internal error', 3: 'not ready', 4: 'no file', 5: 'no path',
           6: 'invalid name', 7: 'denied', 8: 'exists', 9: 'invalid object', 10: 'write protected',
           11: 'invalid drive', 12: 'not enabled', 13: 'no file system', 100: 'unknown command',
           101: 'length over the buffer', 102: 'no card'}


class SdLinkError(Exception):
    pass


class JLink:
    """J-Link Commander driven through its prompt, on a pseudo-terminal (on
    a pipe it buffers its output and the prompt never comes)"""

    def __init__(self, speed):
        self.fd, slave = pty.openpty()
        self.p = subprocess.Popen(['JLinkExe', '-device', 'RISC-V', '-if', 'JTAG', '-JTAGConf', '-1,-1',
                                   '-speed', str(speed), '-autoconnect', '1', '-NoGui', '1'],
                                  stdin=slave, stdout=slave, stderr=slave, close_fds=True)
        os.close(slave)
        self.tmp = tempfile.mkdtemp(prefix='sdlink')
        out = self._prompt(20.0)
        if 'Connecting to J-Link ...O.K.' not in out:
            raise SdLinkError('J-Link: no connection\n' + out)

    def _prompt(self, timeout=10.0):
        buf, end = b'', time.time() + timeout
        while not buf.endswith(b'J-Link>'):
            left = end - time.time()
            if left <= 0 or self.p.poll() is not None:
                raise SdLinkError('J-Link: no answer\n' + buf.decode(errors='replace')[-2000:])
            r, _, _ = select.select([self.fd], [], [], left)
            if r:
                try:
                    c = os.read(self.fd, 65536)
                except OSError:
                    c = b''
                if not c:
                    raise SdLinkError('J-Link: exited\n' + buf.decode(errors='replace')[-2000:])
                buf += c
        return buf.decode(errors='replace').replace('\r', '')

    def cmd(self, line, timeout=30.0):
        os.write(self.fd, (line + '\n').encode())
        out = self._prompt(timeout)
        if 'Error' in out or 'error' in out.split('\n', 1)[-1]:
            raise SdLinkError('J-Link: %s\n%s' % (line, out))
        return out

    def rd(self, addr, n=1):
        out = self.cmd('mem32 %08X %d' % (addr, n))
        words = []
        for m in re.finditer(r'^[0-9A-Fa-f]{8} = ((?:[0-9A-Fa-f]{8} ?)+)', out, re.M):
            words += [int(w, 16) for w in m.group(1).split()]
        if len(words) < n:
            raise SdLinkError('J-Link: mem32 %08X: %s' % (addr, out))
        return words[:n]

    def wr(self, addr, value):
        self.cmd('w4 %08X %08X' % (addr, value & 0xFFFFFFFF))

    def write_bytes(self, addr, data):
        f = os.path.join(self.tmp, 'w.bin')
        open(f, 'wb').write(data)
        self.cmd('loadbin %s,%08X noreset' % (f, addr), 120.0)

    def read_bytes(self, addr, n):
        f = os.path.join(self.tmp, 'r.bin')
        self.cmd('savebin %s,%08X,%X' % (f, addr, n), 120.0)
        return open(f, 'rb').read()

    def close(self):
        try:
            os.write(self.fd, b'qc\n')
            self.p.wait(10)
        except Exception:
            self.p.kill()


class SdLink:
    def __init__(self, speed=4000):
        self.j = JLink(speed)
        w = self.j.rd(MAILBOX, 4)
        if w[W_MAGIC] != MAGIC:
            self.j.close()
            raise SdLinkError('no SD link at %08X: is the boot menu running (a build with sdlink)?' % MAILBOX)
        self.buf, self.size = w[W_BUF], w[W_SIZE]
        if w[W_VERSION] >= 2:                           # the menu counts its polls: is it serving?
            a0 = self.j.rd(MAILBOX + 4 * W_ALIVE, 1)[0]
            end = time.time() + 1.0
            while self.j.rd(MAILBOX + 4 * W_ALIVE, 1)[0] == a0:
                if time.time() > end:
                    self.j.close()
                    raise SdLinkError('the boot menu is not serving its link: go back to its main screen '
                                      '(a program started from it does not serve the card)')
                time.sleep(0.05)
        self._run(HELLO)

    def _run(self, cmd, path='', path2='', offset=0, length=0, timeout=60.0):
        j = self.j
        if path or path2:
            pb = path.encode().ljust(PATH_LEN, b'\0')[:PATH_LEN] + path2.encode().ljust(PATH_LEN, b'\0')[:PATH_LEN]
            j.write_bytes(MAILBOX + PATH_OFF, pb)
        j.wr(MAILBOX + 4 * W_CMD, cmd)
        j.wr(MAILBOX + 4 * W_OFFSET, offset)
        j.wr(MAILBOX + 4 * W_LENGTH, length)
        seq, done = j.rd(MAILBOX + 4 * W_SEQ, 2)
        if seq != done:
            raise SdLinkError('the menu is busy (seq %d, done %d)' % (seq, done))
        j.wr(MAILBOX + 4 * W_SEQ, seq + 1)
        end = time.time() + timeout
        while True:
            w = j.rd(MAILBOX + 4 * W_DONE, 6)               # done, cmd, status, offset, length, result
            if w[0] == (seq + 1) & 0xFFFFFFFF:
                return w[2], w[4], w[5]
            if time.time() > end:
                raise SdLinkError('the menu did not answer (command %d %s)' % (cmd, path))
            time.sleep(0.01)

    def _check(self, what, st):
        if st:
            raise SdLinkError('%s: %s' % (what, FRESULT.get(st, 'error %d' % st)))

    def ls(self, path='/'):
        st, n, _ = self._run(LIST, path)
        self._check('ls ' + path, st)
        out = []
        for line in self.j.read_bytes(self.buf, n).decode(errors='replace').splitlines() if n else []:
            parts = line.split(' ')
            out.append(('D', 0, parts[1]) if parts[0] == 'D' else ('F', int(parts[1]), parts[2]))
        return out

    def stat(self, path):
        st, _, size = self._run(STAT, path)
        return None if st else size

    def crc(self, path):
        st, _, c = self._run(CRC, path, timeout=300.0)
        self._check('crc ' + path, st)
        return c

    def get(self, path):
        size = self.stat(path)
        if size is None:
            raise SdLinkError('get %s: no file' % path)
        data = b''
        while len(data) < size:
            st, n, _ = self._run(READ, path, offset=len(data), length=min(self.size, size - len(data)))
            self._check('get ' + path, st)
            data += self.j.read_bytes(self.buf, n)
        return data

    def put(self, path, data, progress=None):
        folder, name = path.rsplit('/', 1)
        tmp = (folder or '') + '/SDLINK.TMP'
        off = 0
        while off < len(data) or off == 0:
            chunk = data[off:off + self.size]
            if chunk:
                self.j.write_bytes(self.buf, chunk)
            st, n, _ = self._run(WRITE, tmp, offset=off, length=len(chunk))
            self._check('put ' + path, st)
            if n != len(chunk):
                raise SdLinkError('put %s: %d of %d bytes written (card full?)' % (path, n, len(chunk)))
            off += len(chunk)
            if progress:
                progress(off, len(data))
            if not chunk:
                break
        if self.crc(tmp) != zlib.crc32(data) & 0xFFFFFFFF:
            self._run(DELETE, tmp)
            raise SdLinkError('put %s: CRC mismatch after writing' % path)
        st, _, _ = self._run(RENAME, tmp, path)
        self._check('put %s (rename)' % path, st)

    def rm(self, path):
        st, _, _ = self._run(DELETE, path)
        self._check('rm ' + path, st)

    def mkdir(self, path):
        st, _, _ = self._run(MKDIR, path)
        if st not in (0, 8):                                # 8: it exists
            self._check('mkdir ' + path, st)

    def close(self):
        try:
            self._run(BYE)
        finally:
            self.j.close()


def card_name(name):
    n = name.upper()
    if not re.fullmatch(r'[A-Z0-9_~!#$%&\'()@^`{}-]{1,8}(\.[A-Z0-9_~!#$%&\'()@^`{}-]{1,3})?', n):
        raise SdLinkError('%s: not an 8.3 name' % name)
    return n


def bar(done, total):
    w = 30
    k = w * done // max(total, 1)
    sys.stdout.write('\r  [%s%s] %d / %d KB' % ('#' * k, '.' * (w - k), done // 1024, (total + 1023) // 1024))
    sys.stdout.flush()


def sync(s, local, card):
    s.mkdir(card)
    present = {e[2]: e for e in s.ls(card)}
    for entry in sorted(os.listdir(local)):
        if entry.startswith('.'):
            continue
        lp = os.path.join(local, entry)
        cp = card.rstrip('/') + '/' + card_name(entry)
        if os.path.isdir(lp):
            sync(s, lp, cp)
            continue
        data = open(lp, 'rb').read()
        e = present.get(card_name(entry))
        if e and e[0] == 'F' and e[1] == len(data) and s.crc(cp) == zlib.crc32(data) & 0xFFFFFFFF:
            print('  same     %s' % cp)
            continue
        t0 = time.time()
        s.put(cp, data, bar)
        dt = time.time() - t0
        print('\r  written  %s (%d bytes, %.1f s, %.0f KB/s)%s' % (cp, len(data), dt, len(data) / 1024 / max(dt, 1e-3), ' ' * 10))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--speed', type=int, default=4000, help='JTAG clock in kHz')
    ap.add_argument('op', choices=['ls', 'get', 'put', 'rm', 'mkdir', 'sync'])
    ap.add_argument('args', nargs='*')
    a = ap.parse_args()
    s = SdLink(a.speed)
    try:
        if a.op == 'ls':
            for kind, size, name in s.ls(a.args[0] if a.args else '/'):
                print('%s %9s  %s' % (kind, '' if kind == 'D' else size, name))
        elif a.op == 'get':
            data = s.get(a.args[0])
            out = a.args[1] if len(a.args) > 1 else os.path.basename(a.args[0])
            open(out, 'wb').write(data)
            print('%s -> %s (%d bytes)' % (a.args[0], out, len(data)))
        elif a.op == 'put':
            local, dest = a.args
            data = open(local, 'rb').read()
            entries = None
            try:
                entries = s.ls(dest)
            except SdLinkError:
                pass
            if entries is not None:                         # a folder: the same name in it
                dest = dest.rstrip('/') + '/' + card_name(os.path.basename(local))
            t0 = time.time()
            s.put(dest, data, bar)
            dt = time.time() - t0
            print('\r%s -> %s (%d bytes, %.1f s, %.0f KB/s)%s' % (local, dest, len(data), dt, len(data) / 1024 / max(dt, 1e-3), ' ' * 10))
        elif a.op == 'rm':
            s.rm(a.args[0])
        elif a.op == 'mkdir':
            s.mkdir(a.args[0])
        elif a.op == 'sync':
            sync(s, a.args[0], a.args[1])
    except SdLinkError as e:
        sys.exit('sdlink: %s' % e)
    finally:
        s.close()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    render_music.py (prince)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: GPL-3.0-or-later
# The sequencer is a port of SDLPoP's midi.c (see COPYING):
#   SDLPoP, a port/conversion of the DOS game Prince of Persia.
#   Copyright (C) 2013-2025  Dávid Nagy
#   midi.c's credits: its OPL integration is based in part on Chocolate
#   Doom's MIDI interface, by Simon Howard; its playback code was also
#   published as the standalone program 'popmidi' by Falcury.
# The chip model is checked against the output of the Nuked OPL3 emulator
# (Alexey Khokholov) that SDLPoP uses.
#----------------------------------------------------------------------------
# Prince of Persia's MIDI tunes rendered offline, for the Tang (arv/music.c
# streams them from the card while the game plays):
#
#   tools/render_music.py PAK_DIR OUT_DIR [NR ...]
#
# reads the instruments (resource 1 of PRINCE.PAK) and the tunes (resources
# 10000 + NR of MIDISND1.PAK / MIDISND2.PAK, NR 24..56) from the packs of
# make_pak.py and writes OUT_DIR/MUSnn.ADP for each tune: 'KADP', the rate
# (22050), the number of samples, 0 (u32 each, little-endian), then IMA
# ADPCM, the first sample of a pair in the low nibble.
#
# The sequencer is SDLPoP's: its voice allocation, its note frequencies and
# carrier volumes, its timing (float32 like the C), called for 1024 frames at
# a time like the game's audio callback, so a tune has the same length. The
# sound chip is an OPL3 in its two-operator mode, the 9 voices SDLPoP uses,
# modelled from the chip's documented behaviour (log-sine and exponent
# tables, envelope rates, tremolo, vibrato, feedback, the 8 waveforms) at its
# own rate, 49716 Hz, resampled linearly to 22050 Hz. Pure Python: the tunes
# render in parallel.
#----------------------------------------------------------------------------
import math, os, struct, sys
from multiprocessing import Pool

RATE = 22050
OPL_RATE = 49716
FIRST, LAST = 24, 56
VOICES = 9

# ---------------------------------------------------------------------------
# The chip
# ---------------------------------------------------------------------------
LOGSIN = [round(-math.log2(math.sin((i + 0.5) * math.pi / 512)) * 256) for i in range(256)]
EXP = [round(1024 * 2 ** ((255 - i) / 256)) for i in range(256)]
MULT = [1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30]   # x2
KSL_ROM = [0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64]
KSL_SHIFT = [8, 1, 2, 0]
# Envelope increments: the 8-step patterns of each rate (low two bits) and,
# from rate 52, the larger steps of the fastest rates
STEPS = [[0, 1, 0, 1, 0, 1, 0, 1], [0, 1, 0, 1, 1, 1, 0, 1], [0, 1, 1, 1, 0, 1, 1, 1], [0, 1, 1, 1, 1, 1, 1, 1]]
FAST = {13: [[1, 1, 1, 1, 1, 1, 1, 1], [1, 1, 1, 2, 1, 1, 1, 2], [1, 2, 1, 2, 1, 2, 1, 2], [1, 2, 2, 2, 1, 2, 2, 2]],
        14: [[2, 2, 2, 2, 2, 2, 2, 2], [2, 2, 2, 4, 2, 2, 2, 4], [2, 4, 2, 4, 2, 4, 2, 4], [2, 4, 4, 4, 2, 4, 4, 4]],
        15: [[4] * 8] * 4}
ATTACK, DECAY, SUSTAIN, RELEASE = range(4)


def eg_inc(rate, cnt):
    """The envelope step of a 6-bit rate at sample `cnt`"""
    if rate < 4:
        return 0
    hi = rate >> 2
    if hi >= 13:
        return FAST[hi][rate & 3][cnt & 7]
    shift = 12 - hi
    if cnt & ((1 << shift) - 1):
        return 0
    return STEPS[rate & 3][(cnt >> shift) & 7]


def operator(phase, wf, env):
    """One operator's output: phase (10 bits), waveform, attenuation (9 bits)"""
    phase &= 0x3FF
    neg = False
    if wf == 0:
        neg = phase & 0x200
        lg = LOGSIN[(phase & 0xFF) ^ 0xFF] if phase & 0x100 else LOGSIN[phase & 0xFF]
    elif wf == 1:
        if phase & 0x200:
            return 0
        lg = LOGSIN[(phase & 0xFF) ^ 0xFF] if phase & 0x100 else LOGSIN[phase & 0xFF]
    elif wf == 2:
        lg = LOGSIN[(phase & 0xFF) ^ 0xFF] if phase & 0x100 else LOGSIN[phase & 0xFF]
    elif wf == 3:
        if phase & 0x100:
            return 0
        lg = LOGSIN[phase & 0xFF]
    elif wf == 4 or wf == 5:
        if phase & 0x200:
            return 0
        neg = wf == 4 and phase & 0x100
        lg = LOGSIN[((phase ^ 0xFF) << 1) & 0xFF] if phase & 0x80 else LOGSIN[(phase << 1) & 0xFF]
    elif wf == 6:
        neg = phase & 0x200
        lg = 0
    else:
        neg = phase & 0x200
        if neg:
            phase = (phase & 0x1FF) ^ 0x1FF
        lg = phase << 3
    lv = lg + (env << 3)
    if lv > 0x1FFF:
        lv = 0x1FFF
    out = (EXP[lv & 0xFF] << 1) >> (lv >> 8)
    return -out - 1 if neg else out


class Slot:
    __slots__ = ("am", "vib", "egt", "ksr", "mult", "ksl", "tl", "ar", "dr", "sl", "rr", "wf",
                 "att", "state", "phase", "out", "prout")

    def __init__(self):
        self.am = self.vib = self.egt = self.ksr = self.mult = 0
        self.ksl = self.tl = self.ar = self.dr = self.sl = self.rr = self.wf = 0
        self.att, self.state, self.phase, self.out, self.prout = 0x1FF, RELEASE, 0, 0, 0


class Opl:
    """The OPL3 in two-operator mode: 9 voices (channels 0-8), stereo off"""

    def __init__(self, rate):
        self.slots = [[Slot(), Slot()] for _ in range(VOICES)]
        self.fnum = [0] * VOICES
        self.block = [0] * VOICES
        self.key = [0] * VOICES
        self.fb = [0] * VOICES
        self.con = [0] * VOICES
        self.cnt = 0                    # samples since reset (envelope, LFO timing)
        self.trem_pos = 0
        self.vib_pos = 0
        self.ratio = (rate << 10) // OPL_RATE
        self.scnt = 0
        self.old = 0
        self.cur = 0

    # Register writes (the addresses midi.c uses: bank 0, operators 0-17)
    def write(self, reg, val):
        lo = reg & 0xFF
        grp = lo & 0xF0
        if 0x20 <= lo < 0xA0 or lo >= 0xE0:
            off = lo & 0x1F
            col = off & 7
            row = off >> 3
            if col > 5 or row > 2:
                return
            voice = row * 3 + (col % 3)
            s = self.slots[voice][col // 3]
            if grp in (0x20, 0x30):
                s.am, s.vib, s.egt, s.ksr, s.mult = val >> 7 & 1, val >> 6 & 1, val >> 5 & 1, val >> 4 & 1, val & 15
            elif grp in (0x40, 0x50):
                s.ksl, s.tl = val >> 6, val & 63
            elif grp in (0x60, 0x70):
                s.ar, s.dr = val >> 4, val & 15
            elif grp in (0x80, 0x90):
                s.sl, s.rr = val >> 4, val & 15
            else:
                s.wf = val & 7
        elif 0xA0 <= lo <= 0xA8:
            v = lo - 0xA0
            self.fnum[v] = (self.fnum[v] & 0x300) | val
        elif 0xB0 <= lo <= 0xB8:
            v = lo - 0xB0
            self.fnum[v] = (self.fnum[v] & 0xFF) | (val & 3) << 8
            self.block[v] = val >> 2 & 7
            k = val >> 5 & 1
            if k and not self.key[v]:
                for s in self.slots[v]:
                    s.state, s.phase = ATTACK, 0
            elif not k and self.key[v]:
                for s in self.slots[v]:
                    s.state = RELEASE
            self.key[v] = k
        elif 0xC0 <= lo <= 0xC8:
            v = lo - 0xC0
            self.fb[v], self.con[v] = val >> 1 & 7, val & 1

    def _sample(self):
        """One sample at the chip's rate"""
        cnt = self.cnt
        if cnt & 0x3F == 0x3F:
            self.trem_pos = (self.trem_pos + 1) % 210
        if cnt & 0x3FF == 0x3FF:
            self.vib_pos = (self.vib_pos + 1) & 7
        tp = self.trem_pos
        trem = (tp if tp < 105 else 210 - tp) >> 4
        vp = self.vib_pos
        mix = 0
        for v in range(VOICES):
            s0, s1 = self.slots[v]
            if s0.state == RELEASE and s0.att >= 0x1FF and s1.state == RELEASE and s1.att >= 0x1FF:
                s0.out = s0.prout = s1.out = 0
                continue
            fnum, block = self.fnum[v], self.block[v]
            kcode = block << 1 | fnum >> 9
            ksl = KSL_ROM[fnum >> 6] * 4 - (8 - block) * 32
            if ksl < 0:
                ksl = 0
            vib = (fnum >> 7) & 7
            if not vp & 3:
                vib = 0
            elif vp & 1:
                vib >>= 1
            vib >>= 1
            if vp & 4:
                vib = -vib
            for i, s in ((0, s0), (1, s1)):
                # envelope
                st = s.state
                if st == ATTACK:
                    r = s.ar
                elif st == DECAY:
                    r = s.dr
                elif st == SUSTAIN:
                    r = 0 if s.egt else s.rr
                else:
                    r = s.rr
                rate = r * 4 + (kcode if s.ksr else kcode >> 2) if r else 0
                if rate > 63:
                    rate = 63
                att = s.att
                if st == ATTACK:
                    if rate >= 60:
                        att = 0
                    else:
                        inc = eg_inc(rate, cnt)
                        if inc:
                            att += ((~att) * inc) >> 3
                    if att <= 0:
                        att, s.state = 0, DECAY
                else:
                    inc = eg_inc(rate, cnt)
                    att += inc
                    if att > 0x1FF:
                        att = 0x1FF
                    if st == DECAY:
                        sl = 0x1F if s.sl == 15 else s.sl
                        if att >> 4 >= sl:
                            s.state = SUSTAIN
                s.att = att
                env = att + (s.tl << 2) + (ksl >> KSL_SHIFT[s.ksl])
                if s.am:
                    env += trem
                if env > 0x1FF:
                    env = 0x1FF
                # phase
                f = fnum + vib if s.vib else fnum
                ph = s.phase
                s.phase = (ph + (((f << block) >> 1) * MULT[s.mult] >> 1)) & 0xFFFFFFFF
                ph >>= 9
                if i == 0:
                    fb = self.fb[v]
                    mod = (s.prout + s.out) >> (9 - fb) if fb else 0
                    s.prout = s.out
                    s.out = operator(ph + mod, s.wf, env)
                else:
                    mod = 0 if self.con[v] else s0.out
                    s.out = operator(ph + mod, s.wf, env)
            mix += s1.out + (s0.out if self.con[v] else 0)
        self.cnt = cnt + 1
        if mix > 32767:
            mix = 32767
        elif mix < -32768:
            mix = -32768
        return mix

    def generate(self, n):
        """n samples at the output rate (linear interpolation)"""
        out = []
        for _ in range(n):
            while self.scnt >= self.ratio:
                self.old = self.cur
                self.cur = self._sample()
                self.scnt -= self.ratio
            out.append(int((self.old * (self.ratio - self.scnt) + self.cur * self.scnt) / self.ratio))
            self.scnt += 1 << 10
        return out


class NullOpl:
    """The sequencer alone (its voice allocation carries from tune to tune)"""

    def write(self, reg, val):
        pass

    def generate(self, n):
        return [0] * n


# ---------------------------------------------------------------------------
# The sequencer (SDLPoP's midi.c)
# ---------------------------------------------------------------------------
def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def varlen(d, p):
    v = 0
    for i in range(4):
        b = d[p + i]
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            break
    return v, p + i + 1


def parse(midi):
    """Tracks of (delta, type, channel, p1, p2, data), or None"""
    if midi[:4] != b"MThd" or struct.unpack(">I", midi[4:8])[0] != 6:
        return None
    fmt, ntrk, div = struct.unpack(">HHH", midi[8:14])
    if fmt >= 2 or ntrk < 1:
        return None
    tracks, p, last = [], 14, 0
    for _ in range(ntrk):
        if midi[p:p + 4] != b"MTrk":
            return None
        nxt = p + 8 + struct.unpack(">I", midi[p + 4:p + 8])[0]
        q, ev = p + 8, []
        while True:
            dt, q = varlen(midi, q)
            et = midi[q]
            if et & 0x80:
                if et < 0xF8:
                    last = et
                q += 1
            else:
                et = last
            hi = et & 0xF0
            if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                ev.append((dt, hi, et & 15, midi[q], midi[q + 1], None))
                q += 2
            elif hi in (0xC0, 0xD0):
                ev.append((dt, hi, et & 15, midi[q], 0, None))
                q += 1
            elif et in (0xF0, 0xF7):
                n, q = varlen(midi, q)
                ev.append((dt, et, 0, 0, 0, midi[q:q + n]))
                q += n
            elif et == 0xFF:
                typ = midi[q]
                n, q = varlen(midi, q + 1)
                ev.append((dt, 0xFF, 0, typ, 0, midi[q:q + n]))
                q += n
                if typ == 0x2F:
                    break
            else:
                return None
            if q >= nxt:
                return None
        tracks.append(ev)
        p = nxt
    return div, tracks


HARDCODED = bytes([0x13, 0x09, 0x04, 0x02, 0x8D, 0xD7, 0x37, 0x00, 0x03, 0x03, 0xF5, 0x18, 0x00, 0x00, 0x00, 0x00])
SBPRO_OP = [0, 1, 2, 6, 7, 8, 12, 13, 14]
PAIR = [0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x008, 0x009, 0x00A, 0x00B, 0x00C, 0x00D, 0x010, 0x011, 0x012,
        0x013, 0x014, 0x015]
TEMPO_MOD = {53: -0.03, 54: 0.03}


class Song:
    def __init__(self, nr, midi, instruments, chip, last_voice):
        parsed = parse(midi)
        assert parsed is not None
        self.chip, self.ins = chip, instruments
        self.cache = [0] * 512
        self.voice_note = [0] * VOICES
        self.voice_ins = [0] * VOICES
        self.voice_ch = [0] * VOICES
        self.ch_ins = list(range(16))
        self.last_voice = last_voice
        div, self.tracks = parsed
        self.tpb = div
        self.idx = [0] * len(self.tracks)
        self.next_pause = [0] * len(self.tracks)
        self.pos = 0
        self.fract = 0.0
        self.to_pause = 0
        self.semis = 0
        self.us_per_beat = 500000
        self.mod = f32(TEMPO_MOD.get(nr, 0.0))
        self.playing = True
        self.reg(0x105, 0x01)
        for v in range(VOICES):
            self.instrument(self.ins[0], v)

    def reg(self, r, v):
        self.chip.write(r, v)
        self.cache[r] = v

    def masked(self, r, v, m):
        self.reg(r, (self.cache[r] & ~m & 0xFF) | (v & m))

    def instrument(self, ins, voice):
        self.reg(0xC0 + voice, ins[2] | 0x30)
        for o in range(2):
            op = ins[3 + 5 * o:8 + 5 * o]
            r = PAIR[SBPRO_OP[voice]] + (3 if o else 0)
            self.reg(0x20 + r, op[0])
            self.reg(0x40 + r, op[1])
            self.reg(0x60 + r, op[2])
            self.reg(0x80 + r, op[3])
            self.reg(0xE0 + r, op[4])

    def note_off(self, ch, note):
        for v in range(VOICES):
            if self.voice_ch[v] == ch and self.voice_note[v] == note:
                self.masked(0xB0 + v, 0, 0x20)
                self.voice_note[v] = 0
                break

    def note_on(self, ch, note, vel):
        iid = self.ch_ins[ch]
        ins = self.ins[iid] if 0 <= iid < len(self.ins) else self.ins[0]
        if vel == 0:
            self.note_off(ch, note)
            return
        voice, t = -1, self.last_voice
        for _ in range(VOICES):
            t = (t + 1) % VOICES
            if self.voice_note[t] == 0:
                voice = t
                break
        self.last_voice = voice
        if voice < 0:
            return
        if self.voice_ins[voice] != iid:
            self.instrument(ins, voice)
            self.voice_ins[voice] = iid
        self.voice_note[voice] = note
        self.voice_ch[voice] = ch
        octaves = f32(f32(note - 69 - 12 + self.semis) / f32(12.0))
        freq = f32(f32(2.0 ** octaves) * f32(440.0))
        fnf = f32(f32(freq * f32(1 << 20)) / f32(49716.0))
        block = int(f32(f32(math.log2(fnf)) - f32(9.0))) & 7
        f = (int(fnf) >> block) & 1023
        self.reg(0xA0 + voice, f & 0xFF)
        self.reg(0xB0 + voice, 0x20 | block << 2 | f >> 8)
        r = PAIR[SBPRO_OP[voice]]
        self.masked(0x40 + r, ins[4], 0x3F)
        vol = ((ins[9] & 0x3F) + 64) * 225 // (vel + 161)
        vol = min(max(vol, 64), 127) - 64
        self.masked(0x40 + r + 3, vol, 0x3F)

    def event(self, e):
        dt, t, ch, p1, p2, data = e
        if t == 0x80:
            self.note_off(ch, p1)
        elif t == 0x90:
            self.note_on(ch, p1, p2)
        elif t == 0xC0:
            self.ch_ins[ch] = p1
        elif t == 0xF0:
            if len(data) == 7 and data[2] == 0x34 and data[3] in (0, 1) and data[4] == 0:
                self.semis = data[5] - 256 if data[5] > 127 else data[5]
        elif t == 0xFF and p1 == 0x51:
            tempo = data[0] << 16 | data[1] << 8 | data[2]
            self.us_per_beat = int(f32(f32(tempo) * f32(1.0 + self.mod))) & 0xFFFFFFFF

    def callback(self, frames):
        """midi_callback() for `frames` frames: the samples, and whether it still plays"""
        out = []
        while frames > 0:
            if self.to_pause > 0:
                us_to_pause = ((self.to_pause * self.us_per_beat) & 0xFFFFFFFF) // self.tpb
                us_needed = frames * 1000000 // RATE
                adv_us = min(us_to_pause, us_needed)
                avail = (adv_us * RATE + 1000000 - 1) // 1000000
                adv = min(avail, frames)
                adv_us = adv * 1000000 // RATE
                out += self.chip.generate(adv)
                frames -= adv
                tef = f32(f32(f32(adv_us) * f32(self.tpb)) / f32(self.us_per_beat))
                te = int(tef)
                self.fract = f32(self.fract + f32(tef - f32(te)))
                if self.fract > 1.0:
                    self.fract = f32(self.fract - 1.0)
                    te += 1
                self.pos += te
                self.to_pause -= te
            else:
                done = 0
                for i, tr in enumerate(self.tracks):
                    while self.pos >= self.next_pause[i]:
                        left = len(tr) - self.idx[i]
                        if left > 0:
                            e = tr[self.idx[i]]
                            self.idx[i] += 1
                            self.event(e)
                            if left > 1 and tr[self.idx[i]][0]:
                                self.next_pause[i] += tr[self.idx[i]][0]
                        else:
                            done += 1
                            break
                if done >= len(self.tracks):
                    out += [0] * frames
                    self.playing = False
                    return out
                first = min((self.next_pause[i] for i, tr in enumerate(self.tracks)
                             if self.idx[i] < len(tr) and self.pos < self.next_pause[i]), default=None)
                if first is None:
                    raise RuntimeError("MIDI: no next event")
                self.to_pause = first - self.pos
        return out


# ---------------------------------------------------------------------------
# Packs, ADPCM, the tunes
# ---------------------------------------------------------------------------
def pak_find(pak, rid):
    n = struct.unpack("<H", pak[4:6])[0]
    for i in range(n):
        e = pak[8 + 16 * i:24 + 16 * i]
        r, _, _, off, ln = struct.unpack("<HBBII", e[:12])
        if r == rid:
            return pak[off:off + ln]
    return None


STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
        5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
        24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def adpcm(pcm):
    pred, idx, codes = 0, 0, []
    for s in pcm:
        step = STEP[idx]
        diff, code, d = s - pred, 0, step >> 3
        if diff < 0:
            code, diff = 8, -diff
        if diff >= step:
            code |= 4; diff -= step; d += step
        if diff >= step >> 1:
            code |= 2; diff -= step >> 1; d += step >> 1
        if diff >= step >> 2:
            code |= 1; d += step >> 2
        pred = max(-32768, min(32767, pred - d if code & 8 else pred + d))
        idx = max(0, min(88, idx + INDEX[code]))
        codes.append(code)
    if len(codes) & 1:
        codes.append(0)
    body = bytes(codes[i] | codes[i + 1] << 4 for i in range(0, len(codes), 2))
    return b"KADP" + struct.pack("<III", RATE, len(pcm), 0) + body


def load(pak_dir):
    prince = open(os.path.join(pak_dir, "PRINCE.PAK"), "rb").read()
    midis = [open(os.path.join(pak_dir, n), "rb").read() for n in ("MIDISND1.PAK", "MIDISND2.PAK")]
    raw = pak_find(prince, 1)
    if raw and len(raw) == 1 + raw[0] * 16:
        ins = [raw[1 + 16 * i:17 + 16 * i] for i in range(raw[0])]
    else:
        ins = [HARDCODED]
    tunes = {}
    for nr in range(FIRST, LAST + 1):
        for m in midis:
            d = pak_find(m, 10000 + nr)
            if d is not None:
                tunes[nr] = d[1:]
                break
    return ins, tunes


def play(nr, midi, ins, chip, last_voice):
    song = Song(nr, midi, ins, chip, last_voice)
    pcm = []
    while song.playing:
        pcm += song.callback(1024)
    return pcm, song.last_voice


def render(job):
    nr, midi, ins, last_voice, out = job
    pcm, _ = play(nr, midi, ins, Opl(RATE), last_voice)
    open(os.path.join(out, "MUS%02d.ADP" % nr), "wb").write(adpcm(pcm))
    return nr, len(pcm)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__ if __doc__ else "render_music.py PAK_DIR OUT_DIR [NR ...]")
    pak_dir, out = sys.argv[1], sys.argv[2]
    only = {int(a) for a in sys.argv[3:]}
    os.makedirs(out, exist_ok=True)
    ins, tunes = load(pak_dir)
    # The voice the game would start each tune from, playing them in order
    jobs, last = [], 0
    for nr in sorted(tunes):
        if parse(tunes[nr]) is None:
            print("MUS%02d: not a MIDI file, left out" % nr)
            continue
        if not only or nr in only:
            jobs.append((nr, tunes[nr], ins, last, out))
        _, last = play(nr, tunes[nr], ins, NullOpl(), last)
    jobs.sort(key=lambda j: -len(j[1]))        # the longest first
    with Pool() as pool:
        for nr, n in pool.imap_unordered(render, jobs):
            print("MUS%02d.ADP  %6.1f s" % (nr, n / RATE))


if __name__ == "__main__":
    main()

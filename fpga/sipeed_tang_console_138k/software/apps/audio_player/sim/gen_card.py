#!/usr/bin/env python3
"""The simulated microSD card of the audio_player test: a 2 MB image laid out
like a real card (an MBR, one FAT12 partition) with the card's directory
structure and test WAV files, written as sd_image.bin and sd_image.mem (one
byte per line, for $readmemh by sim/rtl_sim/src/sd_card_img.v).

   gen_card.py out_dir

/MEDIA/MUSIC holds, in name order:
   BROKEN.WAV    24-bit PCM: not playable, skipped (track 0)
   NOTES.TXT     not a WAV file, ignored
   TRACK01.WAV   22050 Hz mono 16-bit, 1000 Hz at 8000, a LIST chunk before
                 "data" (track 1, 60 ms)
   TRACK02.WAV   44100 Hz stereo 16-bit, left 500 Hz at 6000, right 1500 Hz
                 at 12000 (track 2, 150 ms)
   TRACK03.WAV   11025 Hz mono 8-bit, 2000 Hz at 100 / 128, a 4096-byte
                 data offset (track 3, 60 ms)
   TRACK04.WAV   22050 Hz stereo IMA ADPCM, left 750 Hz at 10000, right
                 2500 Hz at 5000, 1024-byte blocks, the last one short
                 (track 4, 150 ms)
   TRACK05.QOA   44100 Hz stereo QOA, left 600 Hz at 9000, right 3000 Hz at
                 4000, a full frame and a short one (track 5, 150 ms)
   TRACK06.FLA   44100 Hz stereo FLAC, left 800 Hz at 7000, right 2000 Hz at
                 11000, mid/side, LPC and FIXED subframes, 1152-sample blocks,
                 a title in its Vorbis comment (track 6, 150 ms)
   TRACK07.MOD   a ProTracker module: a 16-byte looped sine on channel 0
                 (panned left) at period 214, 3546895 / 214 / 16 = 1036 Hz,
                 speed 3, tempo 255, ended by a pattern break after 6 rows
                 (track 7, 176 ms)
   TRACK08.S3M   a Scream Tracker 3 module: a 16-sample looped sine, 16-bit
                 unsigned, on channel 0 (panned right), note C-5 at a C-4
                 rate of 10000 Hz: 14317056 / 715 / 16 = 1251.5 Hz, master
                 volume 48; speed 3, tempo 255, ended by a pattern break after
                 6 rows (track 8, 176 ms)
   TRACK10.MP3   44100 Hz stereo MPEG-1 Layer III at 320 kbit/s, left 900 Hz,
                 right 2200 Hz (track 10, 150 ms and the codec's delay)
   TRACK09.XM    a FastTracker 2 module, linear frequencies: instrument 1, a
                 16-sample looped sine, 16-bit delta-coded, sample panning
                 192, relative note +2, a sustained volume envelope; note C-5
                 on channel 0 plays D-5: 8363 x 2^(14 / 12) / 16 = 1173.4 Hz;
                 speed 3, tempo 255, ended by a pattern break after 6 rows
                 (track 9, 176 ms)
Pure Python, no host tool: the same image on any system. The ADPCM and QOA
encoders below follow the formats' usual encoders (IMA: the reference
nibble search; QOA: every scale factor tried per slice, the least squared
error kept; FLAC: fixed or LPC subframes as asked, Rice parameters from each
partition's mean)."""
import math, os, struct, sys

BLOCKS   = 4096                    # the card model's size
PART_LBA = 63
SECTORS  = BLOCKS - PART_LBA
RSV, NFATS, ROOT_ENTS, SPC = 1, 2, 64, 1
FAT_SECS = 12
ROOT_SECS = ROOT_ENTS * 32 // 512
DATA_LBA = RSV + NFATS * FAT_SECS + ROOT_SECS          # partition-relative
CLUSTERS = (SECTORS - DATA_LBA) // SPC
assert CLUSTERS < 4085 and (CLUSTERS + 2) * 3 // 2 <= FAT_SECS * 512   # FAT12


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def wav(rate, chans, bits, frames, extra=b"", fmt_tag=1):
    if bits == 16:
        data = b"".join(struct.pack("<%dh" % chans, *f) for f in frames)
    elif bits == 8:
        data = b"".join(bytes(v + 128 for v in f) for f in frames)
    else:
        data = b"".join(b"".join(struct.pack("<i", v << 8)[:3] for v in f) for f in frames)
    fmt = struct.pack("<HHIIHH", fmt_tag, chans, rate, rate * chans * bits // 8, chans * bits // 8, bits)
    body = b"WAVE" + chunk(b"fmt ", fmt) + extra + chunk(b"data", data)
    return b"RIFF" + struct.pack("<I", len(body)) + body


IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
            50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
            253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
            1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
            3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima_encode(state, v):
    """One sample: the nibble, the decoder's prediction and index updated"""
    pred, index = state
    step = IMA_STEP[index]
    d, n = v - pred, 0
    if d < 0:
        n, d = 8, -d
    for bit, part in ((4, step), (2, step >> 1), (1, step >> 2)):
        if d >= part:
            n |= bit
            d -= part
    diff = step >> 3
    if n & 1: diff += step >> 2
    if n & 2: diff += step >> 1
    if n & 4: diff += step
    pred = max(-32768, min(32767, pred - diff if n & 8 else pred + diff))
    index = max(0, min(88, index + IMA_INDEX[n & 7]))
    return n, (pred, index)


def adpcm_wav(rate, chans, frames, block, extra=b""):
    spb = (block - 4 * chans) * 2 // chans + 1
    st = [(0, 0)] * chans
    data = b""
    for b0 in range(0, len(frames), spb):
        blk = frames[b0:b0 + spb]
        out = b""
        for c in range(chans):
            st[c] = (blk[0][c], st[c][1])
            out += struct.pack("<hBB", blk[0][c], st[c][1], 0)
        nib = [[] for _ in range(chans)]
        for f in blk[1:]:
            for c in range(chans):
                n, st[c] = ima_encode(st[c], f[c])
                nib[c].append(n)
        if chans == 1:
            nib[0] += [0] * (len(nib[0]) & 1)
            out += bytes(nib[0][i] | (nib[0][i + 1] << 4) for i in range(0, len(nib[0]), 2))
        else:
            k = (len(nib[0]) + 7) // 8 * 8
            for c in range(2):
                nib[c] += [0] * (k - len(nib[c]))
            for g in range(0, k, 8):
                for c in range(2):
                    out += bytes(nib[c][g + i] | (nib[c][g + i + 1] << 4) for i in range(0, 8, 2))
        data += out
    fmt = struct.pack("<HHIIHHHH", 0x11, chans, rate, rate * block // spb, block, 4, 2, spb)
    body = (b"WAVE" + chunk(b"fmt ", fmt) + chunk(b"fact", struct.pack("<I", len(frames))) +
            extra + chunk(b"data", data))
    return b"RIFF" + struct.pack("<I", len(body)) + body


QOA_DEQUANT = [[int(math.copysign(math.floor(abs(round((s + 1) ** 2.75) * q) + 0.5), q))
                for q in (0.75, -0.75, 2.5, -2.5, 4.5, -4.5, 7, -7)] for s in range(16)]
QOA_RECIP = [((1 << 16) + round((s + 1) ** 2.75) - 1) // round((s + 1) ** 2.75) for s in range(16)]
QOA_QUANT = [7, 7, 7, 5, 5, 3, 3, 1, 0, 0, 2, 2, 4, 4, 6, 6, 6]


def qoa_file(rate, chans, frames):
    lms = [[[0, 0, 0, 0], [0, 0, -(1 << 13), 1 << 14]] for _ in range(chans)]
    out = b"qoaf" + struct.pack(">I", len(frames))
    for f0 in range(0, len(frames), 5120):
        fr = frames[f0:f0 + 5120]
        slices = (len(fr) + 19) // 20
        out += struct.pack(">BBHHH", chans, rate >> 16, rate & 0xFFFF, len(fr), 8 + 16 * chans + 8 * slices * chans)
        for h, w in lms:
            out += struct.pack(">4h4h", *h, *w)
        for s0 in range(0, len(fr), 20):
            for c in range(chans):
                best = None
                for sf in range(16):
                    h, w = list(lms[c][0]), list(lms[c][1])
                    err, word = 0, sf
                    for f in fr[s0:s0 + 20]:
                        pred = sum(a * b for a, b in zip(h, w)) >> 13
                        res = f[c] - pred
                        n = (res * QOA_RECIP[sf] + (1 << 15)) >> 16
                        n += (res > 0) - (res < 0) - ((n > 0) - (n < 0))
                        q = QOA_QUANT[max(-8, min(8, n)) + 8]
                        dq = QOA_DEQUANT[sf][q]
                        v = max(-32768, min(32767, pred + dq))
                        err += (f[c] - v) ** 2
                        d = dq >> 4
                        w = [wi + (-d if hi < 0 else d) for wi, hi in zip(w, h)]
                        h = h[1:] + [v]
                        word = (word << 3) | q
                    if best is None or err < best[0]:
                        best = (err, word, h, w)
                word = best[1] << (3 * (20 - len(fr[s0:s0 + 20])))
                out += struct.pack(">Q", word)
                lms[c] = [best[2], best[3]]
    return out


class Bits:
    """MSB-first bit writer"""
    def __init__(self):
        self.v, self.n = 0, 0

    def put(self, n, v):
        self.v = (self.v << n) | (v & ((1 << n) - 1))
        self.n += n

    def unary(self, q):
        self.put(q + 1, 1)

    def align(self):
        self.put(-self.n % 8, 0)

    def bytes(self):
        assert self.n % 8 == 0
        return self.v.to_bytes(self.n // 8, "big")


def crc(data, poly, width):
    c, top, mask = 0, 1 << (width - 1), (1 << width) - 1
    for b in data:
        c ^= b << (width - 8)
        for _ in range(8):
            c = ((c << 1) ^ poly if c & top else c << 1) & mask
    return c


def rice_residual(bw, res, order, block, porder, escape_part=None):
    """The residual of a subframe: Rice coded, one parameter a partition"""
    parts, n0 = 1 << porder, block >> porder
    ks, chunks, pos = [], [], 0
    for p in range(parts):
        n = n0 - (order if p == 0 else 0)
        chunk = res[pos:pos + n]
        pos += n
        m = sum((2 * v if v >= 0 else -2 * v - 1) for v in chunk) // max(1, n)
        ks.append(min(30, max(0, m.bit_length() - 1)))
        chunks.append(chunk)
    method = 1 if max(ks) > 14 or escape_part == "wide" else 0
    pb, esc = (5, 31) if method else (4, 15)
    bw.put(2, method)
    bw.put(4, porder)
    for p, (k, chunk) in enumerate(zip(ks, chunks)):
        if escape_part == p:
            nb = max(1, max(abs(v) for v in chunk).bit_length() + 1) if chunk else 1
            bw.put(pb, esc)
            bw.put(5, nb)
            for v in chunk:
                bw.put(nb, v)
            continue
        k = min(k, esc - 1)
        bw.put(pb, k)
        for v in chunk:
            u = 2 * v if v >= 0 else -2 * v - 1
            bw.unary(u >> k)
            bw.put(k, u)


def lpc_coefs(x, order, prec):
    """Autocorrelation and Levinson-Durbin, the coefficients quantized"""
    n = len(x)
    w = [x[i] * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) for i in range(n)]
    r = [sum(w[i] * w[i - l] for i in range(l, n)) for l in range(order + 1)]
    if r[0] == 0:
        return [0] * order, 0
    a, e = [0.0] * order, r[0]
    for i in range(order):
        if e <= r[0] * 1e-12:                   # a perfect prediction: the rest stays 0
            break
        k = (r[i + 1] - sum(a[j] * r[i - j] for j in range(i))) / e
        a = [a[j] - k * a[i - 1 - j] for j in range(i)] + [k] + a[i + 1:]
        e *= max(0.0, 1 - k * k)
    if not all(math.isfinite(c) for c in a):
        a = [0.0] * order
    cmax = max(abs(c) for c in a) or 1.0
    shift = max(0, min(15, prec - 1 - math.ceil(math.log2(cmax + 1e-9)) - 1))
    lim = (1 << (prec - 1)) - 1
    return [max(-lim - 1, min(lim, round(c * (1 << shift)))) for c in a], shift


def flac_subframe(bw, x, bps, kind, block):
    """One subframe of kind: ("constant",) ("verbatim",) ("fixed", o)
    ("lpc", o, prec) with ("wasted",) or ("escape",) / ("wide",) added"""
    wasted = 0
    if "wasted" in kind:
        while wasted < bps - 1 and all(v % (1 << (wasted + 1)) == 0 for v in x):
            wasted += 1
        x = [v >> wasted for v in x]
    b = bps - wasted
    esc = 1 if "escape" in kind else ("wide" if "wide" in kind else None)

    def head(t):
        bw.put(1, 0)
        bw.put(6, t)
        if wasted:
            bw.put(1, 1)
            bw.unary(wasted - 1)
        else:
            bw.put(1, 0)
    if kind[0] == "constant" and len(set(x)) == 1:
        head(0)
        bw.put(b, x[0])
    elif kind[0] == "verbatim" or kind[0] == "constant":
        head(1)
        for v in x:
            bw.put(b, v)
    elif kind[0] == "fixed":
        o = kind[1]
        coef = [[], [1], [2, -1], [3, -3, 1], [4, -6, 4, -1]][o]
        res = [x[i] - sum(c * x[i - 1 - j] for j, c in enumerate(coef)) for i in range(o, block)]
        head(8 + o)
        for v in x[:o]:
            bw.put(b, v)
        porder = 2 if block % 4 == 0 and block // 4 >= o else 0
        rice_residual(bw, res, o, block, porder, esc)
    else:
        o, prec = kind[1], kind[2]
        q, shift = lpc_coefs(x, o, prec)
        res = [x[i] - (sum(c * x[i - 1 - j] for j, c in enumerate(q)) >> shift) for i in range(o, block)]
        head(31 + o)
        for v in x[:o]:
            bw.put(b, v)
        bw.put(4, prec - 1)
        bw.put(5, shift)
        for c in q:
            bw.put(prec, c)
        porder = 3 if block % 8 == 0 and block // 8 >= o else 0
        rice_residual(bw, res, o, block, porder, esc)


FLAC_BLOCK_CODES = {192: 1, 576: 2, 1152: 3, 2304: 4, 4608: 5, 256: 8, 512: 9, 1024: 10, 2048: 11,
                    4096: 12, 8192: 13, 16384: 14, 32768: 15}
FLAC_RATE_CODES = {88200: 1, 176400: 2, 192000: 3, 8000: 4, 16000: 5, 22050: 6, 24000: 7,
                   32000: 8, 44100: 9, 48000: 10, 96000: 11}


def flac_file(rate, chans, bps, frames, block, title="", artist="", plan=None):
    """A FLAC stream. plan(i) gives frame i's channel mode (0 independent,
    8 left/side, 9 side/right, 10 mid/side) and its subframe kinds"""
    import hashlib
    plan = plan or (lambda i: (10 if chans == 2 else 0, [("fixed", 2), ("lpc", 8, 12)]))
    md5 = hashlib.md5(b"".join(b"".join(v.to_bytes(bps // 8, "little", signed=True) for v in f)
                               for f in frames)).digest()
    si = Bits()
    for n, v in ((16, block), (16, block), (24, 0), (24, 0), (20, rate), (3, chans - 1),
                 (5, bps - 1), (36, len(frames))):
        si.put(n, v)
    out = b"fLaC" + bytes([0]) + (34).to_bytes(3, "big") + si.bytes() + md5
    fields = [f.encode() for f in (("TITLE=" + title) if title else "", ("ARTIST=" + artist) if artist else "") if f]
    vc = (6).to_bytes(4, "little") + b"aRVern" + len(fields).to_bytes(4, "little") + \
         b"".join(len(f).to_bytes(4, "little") + f for f in fields)
    out += bytes([4]) + len(vc).to_bytes(3, "big") + vc
    art = bytes(range(256)) * 2                                              # a picture block: skipped
    pic = (3).to_bytes(4, "big") + len(b"image/png").to_bytes(4, "big") + b"image/png" + \
          (0).to_bytes(4, "big") + b"".join(v.to_bytes(4, "big") for v in (16, 16, 24, 0)) + \
          len(art).to_bytes(4, "big") + art
    out += bytes([6]) + len(pic).to_bytes(3, "big") + pic
    out += bytes([0x80 | 1]) + (64).to_bytes(3, "big") + bytes(64)            # padding, the last block
    for fi, f0 in enumerate(range(0, len(frames), block)):
        fr = frames[f0:f0 + block]
        n = len(fr)
        mode, kinds = plan(fi)
        bw = Bits()
        bw.put(15, 0x7FFC)
        bw.put(1, 0)                                                         # fixed block size
        bc = FLAC_BLOCK_CODES.get(n, 6 if n <= 256 else 7)
        rc = FLAC_RATE_CODES.get(rate, 13)
        bw.put(4, bc)
        bw.put(4, rc)
        bw.put(4, mode if mode else chans - 1)                               # independent: channels - 1
        bw.put(3, {8: 1, 12: 2, 16: 4, 20: 5, 24: 6}[bps])
        bw.put(1, 0)
        if fi < 0x80:                                                        # UTF-8 frame number
            bw.put(8, fi)
        else:
            bw.put(8, 0xC0 | (fi >> 6))
            bw.put(8, 0x80 | (fi & 0x3F))
        if bc == 6:
            bw.put(8, n - 1)
        elif bc == 7:
            bw.put(16, n - 1)
        if rc == 13:
            bw.put(16, rate)
        bw.put(8, crc(bw.bytes(), 0x07, 8))
        ch = [[f[c] for f in fr] for c in range(chans)]
        if chans == 2 and mode:
            l, r = ch
            side = [a - b for a, b in zip(l, r)]
            ch = {8: [l, side], 9: [side, r], 10: [[(a + b) >> 1 for a, b in zip(l, r)], side]}[mode]
        for c in range(chans):
            sb = bps + (1 if (mode in (8, 10) and c == 1) or (mode == 9 and c == 0) else 0)
            flac_subframe(bw, ch[c], sb, kinds[c % len(kinds)], n)
        bw.align()
        body = bw.bytes()
        out += body + crc(body, 0x8005, 16).to_bytes(2, "big")
    return out


def mod_file(title, sample, period, rows):
    """A 4-channel ProTracker module: sample 1 (looped whole) plays at
    period on channel 0 from row 0; row 0 sets speed 3 (channel 2) and
    tempo 255 (channel 3); a pattern break on row rows - 1 ends the song"""
    h = title.encode().ljust(20, b"\0")[:20]
    n = len(sample) // 2
    h += b"sine".ljust(22, b"\0") + struct.pack(">HBBHH", n, 0, 64, 0, n)
    h += (b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)) * 30
    h += bytes([1, 127]) + bytes(128) + b"M.K."
    cells = bytearray(64 * 4 * 4)
    def cell(row, ch, smp, per, eff, par):
        o = (row * 4 + ch) * 4
        cells[o:o + 4] = bytes([(smp & 0xF0) | (per >> 8), per & 0xFF, ((smp & 15) << 4) | eff, par])
    cell(0, 0, 1, period, 0, 0)
    cell(0, 2, 0, 0, 0xF, 3)
    cell(0, 3, 0, 0, 0xF, 255)
    cell(rows - 1, 1, 0, 0, 0xD, 0)
    return h + bytes(cells) + bytes((v + 256) & 255 for v in sample)


def s3m_file(title, sample, c2spd, note, rows):
    """A Scream Tracker 3 module, stereo, two channels (0 right, 1 left):
    master volume 48 (0xB0: stereo, 48); instrument 1, the sample as
    16-bit unsigned (looped whole), plays note
    on channel 0 from row 0 (volume 64 in the volume column); row 0 sets
    speed 3 (A) and tempo 255 (T, channel 1); a pattern break (C) on row
    rows - 1 ends the song"""
    ordn, insn, patn = 2, 1, 1
    chs = bytes([8, 1]) + bytes([255] * 30)
    h = title.encode().ljust(28, b"\0")[:28] + bytes([0x1A, 16, 0, 0])
    h += struct.pack("<6H", ordn, insn, patn, 0, 0x1320, 2) + b"SCRM"
    h += bytes([64, 6, 125, 0xB0, 0, 0]) + bytes(10) + chs
    tbl_end = len(h) + ordn + 2 * insn + 2 * patn
    ins_at = (tbl_end + 15) // 16 * 16
    rowdata = bytearray()
    for r in range(64):
        if r == 0:
            rowdata += bytes([0 | 32 | 64 | 128, note, 1, 64, 1, 3])     # note, instrument, volume, A03
            rowdata += bytes([1 | 128, 20, 255])                           # T FF
        if r == rows - 1:
            rowdata += bytes([1 | 128, 3, 0])                              # C00
        rowdata.append(0)
    pat = struct.pack("<H", len(rowdata) + 2) + rowdata
    pat_at = ins_at + 0x50
    smp_at = (pat_at + len(pat) + 15) // 16 * 16
    n = len(sample)
    ins = bytes([1]) + b"sine.raw".ljust(12, b"\0") + bytes([smp_at >> 20]) + struct.pack("<H", (smp_at >> 4) & 0xFFFF)
    ins += struct.pack("<III", n, 0, n) + bytes([64, 0, 0, 1 | 4]) + struct.pack("<I", c2spd) + bytes(12)
    ins += b"sine".ljust(28, b"\0") + b"SCRS"
    out = bytearray(h + bytes([0, 255]) + struct.pack("<H", ins_at >> 4) + struct.pack("<H", pat_at >> 4))
    out += bytes(ins_at - len(out)) + ins + pat
    out += bytes(smp_at - len(out)) + b"".join(struct.pack("<H", (v + 32768) & 0xFFFF) for v in sample)
    return bytes(out)


def xm_file(title, sample, note, rel, pan, rows):
    """A FastTracker 2 module, two channels, linear frequencies, speed 3,
    tempo 255: instrument 1 (its one sample, 16-bit delta-coded, looped
    whole, volume 64, panning pan, relative note rel; a volume envelope
    held at 64 by its sustain point) plays note on channel 0 from row 0; a
    pattern break (D00) on row rows - 1 ends the song"""
    h = b"Extended Module: " + title.encode().ljust(20, b" ")[:20] + b"\x1a" + b"gen_card".ljust(20, b" ")
    h += struct.pack("<HIHHHHHHHH", 0x0104, 276, 1, 0, 2, 1, 1, 1, 3, 255) + bytes(256)
    rowdata = bytearray()
    for r in range(64):
        rowdata += bytes([0x80 | 0x07, note, 1, 0x50]) if r == 0 else bytes([0x80])
        rowdata += bytes([0x80 | 0x18, 0x0D, 0]) if r == rows - 1 else bytes([0x80])
    pat = struct.pack("<IBHH", 9, 0, 64, len(rowdata)) + rowdata
    env = struct.pack("<4H", 0, 64, 1, 64) + bytes(40)
    ins = struct.pack("<I", 263) + b"sine".ljust(22, b"\0") + bytes([0]) + struct.pack("<HI", 1, 40) + bytes(96)
    ins += env + bytes(48) + bytes([2, 0, 1, 0, 0, 0, 0, 0, 1 | 2, 0, 0, 0, 0, 0]) + struct.pack("<H", 0)
    ins += bytes(263 - len(ins))
    n = len(sample)
    ins += struct.pack("<IIIBbBBbB", 2 * n, 0, 2 * n, 64, 0, 1 | 16, pan, rel, 0) + b"sine".ljust(22, b"\0")
    prev, data = 0, bytearray()
    for v in sample:
        data += struct.pack("<h", ((v - prev + 32768) & 0xFFFF) - 32768)
        prev = v
    return h + pat + ins + bytes(data)


def mp3_file(left, right):
    """MPEG-1 Layer III, 44.1 kHz stereo, 320 kbit/s (1044-byte frames, no
    padding): the polyphase analysis filterbank (window D / 32), frequency
    inversion, a 36-point MDCT with the normal window, the encoder's alias
    reduction, one global gain a granule and channel (the largest value
    about 30), scale factors 0, table 31 for every big value (its code
    words from the decoder's tree, mp3_tab.h), no count1 region, no bit
    reservoir. left, right: samples, full scale 1.0."""
    tab_h = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mp3_tab.h")).read()
    def table(name):
        body = tab_h.split(name + "[", 1)[1].split("{", 1)[1].split("};", 1)[0]
        return [int(v) for v in body.replace(",", " ").split()]
    D = [v / (1 << 30) for v in table("synth_d")]
    HT, HI = table("huff_tab"), table("huff_index")
    code = {}
    def walk(base, w, prefix):
        for p in range(1 << w):
            leaf = HT[HI[24] + base + p]
            if leaf >= 0:
                n = leaf >> 8
                key = (leaf & 15, (leaf >> 4) & 15)
                bits = prefix + (format(p >> (w - n), "0%db" % n) if n else "")
                if key not in code or len(code[key]) > len(bits):
                    code[key] = bits
            else:
                walk(-(leaf >> 3), leaf & 7, prefix + format(p, "0%db" % w))
    walk(0, 5, "")
    C = [d / 32.0 for d in D]
    M = [[math.cos((2 * k + 1) * (i - 16) * math.pi / 64) for i in range(64)] for k in range(32)]
    win = [math.sin(math.pi / 36 * (i + 0.5)) for i in range(36)]
    mdct = [[math.cos(math.pi / 72 * (2 * n + 19) * (2 * k + 1)) * win[n] for n in range(36)] for k in range(18)]
    ci = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
    cs = [1 / math.sqrt(1 + c * c) for c in ci]
    ca = [c / math.sqrt(1 + c * c) for c in ci]
    nfr = (len(left) + 1151) // 1152 + 2
    sig = [list(ch) + [0.0] * (nfr * 1152 - len(ch)) for ch in (left, right)]
    fifo = [[0.0] * 512, [0.0] * 512]
    prev = [[[0.0] * 18 for _ in range(32)] for _ in range(2)]
    flen = 144 * 320000 // 44100
    out = bytearray()
    for f in range(nfr):
        side, md = [], ""
        for g in range(2):
            for c in range(2):
                pcm = sig[c][f * 1152 + g * 576: f * 1152 + g * 576 + 576]
                sb = [[0.0] * 18 for _ in range(32)]
                for t in range(18):                     # the analysis filterbank
                    x = fifo[c]
                    x[32:] = x[:-32]
                    x[:32] = pcm[32 * t: 32 * t + 32][::-1]
                    y = [sum(C[i + 64 * j] * x[i + 64 * j] for j in range(8)) for i in range(64)]
                    for k in range(32):
                        v = sum(M[k][i] * y[i] for i in range(64))
                        sb[k][t] = -v if (k & 1 and t & 1) else v
                xr = []
                for k in range(32):                     # the MDCT, with the previous granule
                    z = prev[c][k] + sb[k]
                    xr += [sum(row[n] * z[n] for n in range(36)) / 9.0 for row in mdct]
                prev[c] = sb
                for k in range(1, 32):                  # alias reduction
                    for i in range(8):
                        bu, bd = xr[18 * k - 1 - i], xr[18 * k + i]
                        xr[18 * k - 1 - i] = bu * cs[i] + bd * ca[i]
                        xr[18 * k + i] = bd * cs[i] - bu * ca[i]
                mx = max(abs(v) for v in xr)
                gg = int(math.ceil(210 + 4 * math.log2(mx / 30.0 ** (4 / 3)))) if mx > 1e-9 else 210
                ix = [(1 if v > 0 else -1) * int(math.floor((abs(v) / 2 ** ((gg - 210) / 4)) ** 0.75 + 0.4054)) for v in xr]
                n = 576
                while n and not ix[n - 1]:
                    n -= 1
                n += n & 1
                bits = ""
                for i in range(0, n, 2):
                    a, b = abs(ix[i]), abs(ix[i + 1])
                    bits += code[(min(a, 15), min(b, 15))]
                    if a >= 15: bits += format(a - 15, "013b")
                    if a: bits += "1" if ix[i] < 0 else "0"
                    if b >= 15: bits += format(b - 15, "013b")
                    if b: bits += "1" if ix[i + 1] < 0 else "0"
                side.append((len(bits), n // 2, gg))
                md += bits
        s = "0" * 9 + "0" * 3 + "0" * 8
        for p23, bv, gg in side:
            s += format(p23, "012b") + format(bv, "09b") + format(gg, "08b") + "0000" + "0"
            s += format(31, "05b") * 3 + format(15, "04b") + format(7, "03b") + "000"
        body = s + md
        assert len(body) <= (flen - 4) * 8, "an MP3 frame over its budget"
        body += "0" * ((flen - 4) * 8 - len(body))
        out += bytes([0xFF, 0xFB, 0xE0, 0x00]) + int(body, 2).to_bytes(flen - 4, "big")
    return bytes(out)


def tone(rate, ms, *chans):
    n = rate * ms // 1000
    return [tuple(int(round(a * math.sin(2 * math.pi * f * i / rate))) for f, a in chans) for i in range(n)]


def files():
    t3_pad = chunk(b"FLLR", bytes(4096 - 12 - 24 - 8 - 8))      # data at offset 4096
    return [
        ("BROKEN.WAV",  wav(22050, 1, 24, tone(22050, 10, (1000, 8000)))),
        ("NOTES.TXT",   b"Test card of the audio_player simulation.\r\n"),
        ("TRACK01.WAV", wav(22050, 1, 16, tone(22050, 60, (1000, 8000)),
                            extra=chunk(b"LIST", b"INFOINAM\x06\0\0\0tone1\0"))),
        ("TRACK02.WAV", wav(44100, 2, 16, tone(44100, 150, (500, 6000), (1500, 12000)))),
        ("TRACK03.WAV", wav(11025, 1, 8, tone(11025, 60, (2000, 100)), extra=t3_pad)),
        ("TRACK04.WAV", adpcm_wav(22050, 2, tone(22050, 150, (750, 10000), (2500, 5000)), 1024)),
        ("TRACK05.QOA", qoa_file(44100, 2, tone(44100, 150, (600, 9000), (3000, 4000)))),
        ("TRACK06.FLA", flac_file(44100, 2, 16, tone(44100, 150, (800, 7000), (2000, 11000)), 1152,
                                  "flac tone", "aRVern", lambda i: (10, [("lpc", 8, 12), ("fixed", 2)]))),
        ("TRACK07.MOD", mod_file("mod tone", [round(127 * math.sin(2 * math.pi * i / 16)) for i in range(16)], 214, 6)),
        ("TRACK08.S3M", s3m_file("s3m tone", [round(32767 * math.sin(2 * math.pi * i / 16)) for i in range(16)],
                                 10000, 0x50, 6)),
        ("TRACK09.XM",  xm_file("xm tone", [round(32767 * math.sin(2 * math.pi * i / 16)) for i in range(16)],
                                61, 2, 192, 6)),
        ("TRACK10.MP3", mp3_file([0.30 * math.sin(2 * math.pi * 900 * i / 44100) for i in range(6615)],
                                 [0.20 * math.sin(2 * math.pi * 2200 * i / 44100) for i in range(6615)])),
    ]


class Fat12:
    def __init__(self):
        self.img = bytearray(SECTORS * 512)
        self.fat = [0] * (CLUSTERS + 2)
        self.fat[0], self.fat[1] = 0xFF8, 0xFFF
        self.next = 2

    def alloc(self, data):
        n = max(1, -(-len(data) // (SPC * 512)))
        first = self.next
        for k in range(n):
            self.fat[first + k] = first + k + 1 if k < n - 1 else 0xFFF
        off = (DATA_LBA + (first - 2) * SPC) * 512
        self.img[off:off + len(data)] = data
        self.next += n
        return first

    @staticmethod
    def entry(name, attr, cl, size):
        base, _, ext = name.partition(".")
        n = (base.ljust(8) + ext.ljust(3)).encode() if name not in (".", "..") else name.ljust(11).encode()
        return n + bytes([attr]) + bytes(14) + struct.pack("<HI", cl, size)

    def directory(self, tree, parent):
        """Write a directory (dict name -> bytes or dict); returns its cluster."""
        ents = []
        size = (len(tree) + 2) * 32
        me = self.alloc(bytes(size))
        ents += [self.entry(".", 0x10, me, 0), self.entry("..", 0x10, parent, 0)]
        ents += self.children(tree, me)
        d = b"".join(ents)
        off = (DATA_LBA + (me - 2) * SPC) * 512
        self.img[off:off + len(d)] = d
        return me

    def children(self, tree, me):
        ents = []
        for name, v in tree.items():
            if isinstance(v, dict):
                ents.append(self.entry(name, 0x10, self.directory(v, me), 0))
            else:
                ents.append(self.entry(name, 0x20, self.alloc(v), len(v)))
        return ents

    def build(self, tree):
        root = b"".join(self.children(tree, 0))
        assert len(root) <= ROOT_ENTS * 32
        bpb = struct.pack("<3s8sHBHBHHBHHHII", b"\xEB\x3C\x90", b"ARVERN  ", 512, SPC, RSV, NFATS,
                          ROOT_ENTS, SECTORS, 0xF8, FAT_SECS, 63, 255, PART_LBA, 0)
        bpb += struct.pack("<BBBI11s8s", 0x80, 0, 0x29, 0x20261022, b"ARVERN     ", b"FAT12   ")
        self.img[0:len(bpb)] = bpb
        self.img[510:512] = b"\x55\xAA"
        fat = bytearray(FAT_SECS * 512)
        for i, v in enumerate(self.fat):
            o = i * 3 // 2
            if i & 1:
                fat[o] = (fat[o] & 0x0F) | ((v << 4) & 0xF0)
                fat[o + 1] = (v >> 4) & 0xFF
            else:
                fat[o] = v & 0xFF
                fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F)
        for k in range(NFATS):
            o = (RSV + k * FAT_SECS) * 512
            self.img[o:o + len(fat)] = fat
        o = (RSV + NFATS * FAT_SECS) * 512
        self.img[o:o + len(root)] = root
        return bytes(self.img)


def main():
    out = sys.argv[1]
    tree = {"APPS": {}, "MEDIA": {"PICTURES": {}, "MUSIC": dict(files())}}
    part = Fat12().build(tree)
    mbr = bytearray(512)
    mbr[446:462] = struct.pack("<B3sB3sII", 0x00, b"\0\x01\0", 0x01, b"\xFE\xFF\xFF", PART_LBA, SECTORS)
    mbr[510:512] = b"\x55\xAA"
    img = bytes(mbr) + bytes((PART_LBA - 1) * 512) + part
    assert len(img) == BLOCKS * 512
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "sd_image.bin"), "wb").write(img)
    with open(os.path.join(out, "sd_image.mem"), "w") as f:
        f.write("\n".join("%02x" % b for b in img) + "\n")
    print("sd_image: %d WAV/TXT files in /MEDIA/MUSIC, %d bytes" % (len(files()), len(img)))


if __name__ == "__main__":
    main()

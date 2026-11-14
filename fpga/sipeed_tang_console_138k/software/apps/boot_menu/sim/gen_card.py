#!/usr/bin/env python3
"""The simulated microSD card of the boot_menu test: a 2 MB image laid out
like a real card (an MBR, one FAT12 partition), written as sd_image.bin and
sd_image.mem (one byte per line, for $readmemh by sim/rtl_sim/src/sd_card_img.v).

   gen_card.py out_dir [crc]  (reads out_dir/app_a.bin and out_dir/app_b.bin)

/APPS holds, in name order:
   APPA/      APP.BIN (test program A, a few bytes: read straight into place), APP.TXT,
              APP.CRC (its CRC-32)
   APPB/      APP.BIN (test program B, about 159 KB: over the loader block), APP.TXT;
              with "crc", APP.CRC (the check then runs on the load the test starts)
   EMPTY/     no APP.BIN: not listed
   GROUP/     no APP.BIN, a folder of programs: APP.TXT and APPC/ (program A
              again, with its APP.TXT): listed last, opened by the menu
   HUGE/      an APP.BIN one word over the size limit: listed, refused
   NOTES.TXT  a file, not a folder: ignored
The test programs (apptest.S) check the sum of their image against their
last word, filled in here.
Pure Python, no host tool: the same image on any system."""
import os, struct, sys, zlib

BLOCKS   = 4096                    # the card model's size
PART_LBA = 63
SECTORS  = BLOCKS - PART_LBA
RSV, NFATS, ROOT_ENTS, SPC = 1, 2, 64, 1
FAT_SECS = 12
ROOT_SECS = ROOT_ENTS * 32 // 512
DATA_LBA = RSV + NFATS * FAT_SECS + ROOT_SECS          # partition-relative
CLUSTERS = (SECTORS - DATA_LBA) // SPC
assert CLUSTERS < 4085 and (CLUSTERS + 2) * 3 // 2 <= FAT_SECS * 512   # FAT12


APP_MAX = 0x28000 - 256


def with_sum(img):
    """The image with its last word set to the sum of the others (mod 2^32)."""
    assert len(img) % 4 == 0, "a test program image is not a whole number of words"
    n = len(img) // 4 - 1
    total = sum(struct.unpack_from("<%dI" % n, img)) & 0xFFFFFFFF
    return img[:-4] + struct.pack("<I", total)


def crc(data):
    return b"%08X\r\n" % zlib.crc32(data)


def files(out, b_crc):
    a = with_sum(open(os.path.join(out, "app_a.bin"), "rb").read())
    b = with_sum(open(os.path.join(out, "app_b.bin"), "rb").read())
    appb = {"APP.BIN": b, "APP.TXT": b"Test program B\r\nOver the loader block: in place, staged, in place\r\n"}
    if b_crc:
        appb["APP.CRC"] = crc(b)
    return {
        "APPA":  {"APP.BIN": a, "APP.CRC": crc(a), "APP.TXT": b"Test program A\r\nA small image: read in place\r\n"},
        "APPB":  appb,
        "EMPTY": {"README.TXT": b"No program here.\r\n"},
        "GROUP": {"APP.TXT": b"Test folder\r\nPrograms in a folder\r\n",
                  "APPC": {"APP.BIN": a, "APP.TXT": b"Test program C\r\nProgram A in a folder\r\n"}},
        "HUGE":  {"APP.BIN": bytes(APP_MAX + 4)},
        "NOTES.TXT": b"Test card of the boot_menu simulation.\r\n",
    }


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
    tree = {"APPS": files(out, sys.argv[2:] == ["crc"]), "MEDIA": {"PICTURES": {}, "MUSIC": {}}}
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
    print("sd_image: %d entries in /APPS, %d bytes" % (len(tree["APPS"]), len(img)))


if __name__ == "__main__":
    main()

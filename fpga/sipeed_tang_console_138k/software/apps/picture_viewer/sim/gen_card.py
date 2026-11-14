#!/usr/bin/env python3
"""The simulated microSD card of the picture_viewer test: a 2 MB image laid
out like a real card (an MBR, one FAT12 partition) whose /MEDIA/PICTURES
holds the test pictures, written as sd_image.bin and sd_image.mem (one
byte per line, for $readmemh by sim/rtl_sim/src/sd_card_img.v).

   gen_card.py out_dir

/MEDIA/PICTURES holds, in name order, pictures of four coloured quadrants
(top left, top right, bottom left, bottom right; the bench checks the
middle of each on the screen):
   A1BASE.JPG    64 x 48 baseline JPEG, 4:2:0: red, green, blue, white
   A2PROG.JPG    64 x 48 progressive JPEG, 4:2:0: yellow, cyan, magenta, black
   A3RGB.PNG     48 x 36 RGB PNG (each row a filter in turn): orange, teal,
                 grey, purple
   A4PAL.PNG     48 x 36 palette PNG, Adam7-interlaced, the bottom-right
                 quadrant transparent: red, yellow, green, (black)
   A5ANIM.GIF    32 x 24 GIF, two frames of 40 ms: blue, then red drawn over
                 the top half only (transparent below), again and again
   A6QUAD.BMP    40 x 30 BMP, 24 bits, bottom-up: white, red, green, blue
   A7QUAD.QOI    40 x 30 QOI: cyan, magenta, yellow, white
   A8BAD.JPG     the first JPEG cut short: an error on the screen
and larger than the screen, shrunk to fit:
   B1BASE.JPG    1024 x 576 baseline JPEG, 4:2:0 (decoded at 1/2, then
                 averaged): brick, green, blue, gold
   B2PROG.JPG    640 x 360 progressive JPEG, 4:2:0 (its coefficients fit
                 the memory at 1/4, grown back smoothly): cyan, plum, orange, grey
   B3RGB.PNG     520 x 296 RGB PNG: navy, lime, salmon, olive
   B4BIG.GIF     512 x 290 GIF, one frame: pink, teal, ochre, violet
   B5BIG.BMP     496 x 280 BMP, 24 bits, bottom-up (each row a seek outside
                 the reader's buffer): sky, rust, mint, slate
The JPEGs were made once with Pillow (libjpeg, quality 90, 4:2:0) and are
kept below; the others are written here. Pure Python, no host tool."""
import base64, os, struct, sys, zlib

BLOCKS   = 4096                    # the card model's size
PART_LBA = 63
SECTORS  = BLOCKS - PART_LBA
RSV, NFATS, ROOT_ENTS, SPC = 1, 2, 64, 1
FAT_SECS = 12
ROOT_SECS = ROOT_ENTS * 32 // 512
DATA_LBA = RSV + NFATS * FAT_SECS + ROOT_SECS          # partition-relative
CLUSTERS = (SECTORS - DATA_LBA) // SPC
assert CLUSTERS < 4085 and (CLUSTERS + 2) * 3 // 2 <= FAT_SECS * 512   # FAT12


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


# Pillow: quads(64, 48, red, green, blue, white).save(f, "JPEG", quality=90, subsampling=2)
JPEG_BASE = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoM"
    "DAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsN"
    "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAAR"
    "CAAwAEADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAA"
    "AgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkK"
    "FhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWG"
    "h4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl"
    "5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREA"
    "AgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYk"
    "NOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOE"
    "hYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk"
    "5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDyyiiivyo/v0KKKKAPaKKKK8E/xKCiiigD"
    "xeiuVor+sP8AiDf/AFMP/KX/AN0P9Gf+I1f9S7/yr/8AczqqK5Wij/iDf/Uw/wDKX/3QP+I1"
    "f9S7/wAq/wD3M+kqK/Raivwn/Vj/AKff+S//AGx/IX+oH/UV/wCSf/bH500V+i1FH+rH/T7/"
    "AMl/+2D/AFA/6iv/ACT/AO2PxXooor/Qk+hCiiigD9qKKKK/i8+7CiiigD//2Q==")
# Pillow: quads(64, 48, yellow, cyan, magenta, black).save(f, "JPEG", quality=90, subsampling=2, progressive=True)
JPEG_PROG = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoM"
    "DAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsN"
    "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wgAR"
    "CAAwAEADASIAAhEBAxEB/8QAFwABAQEBAAAAAAAAAAAAAAAAAAcGCP/EABgBAQEBAQEAAAAA"
    "AAAAAAAAAAAHCAYF/9oADAMBAAIQAxAAAAGxCCe0BhxqjOQG4QtNNW3RCxvHIrtIX105FF+H"
    "VaUAgIn8lA//xAAUEAEAAAAAAAAAAAAAAAAAAABQ/9oACAEBAAEFAkP/xAAcEQAABgMAAAAA"
    "AAAAAAAAAAAAFRYgYuFjoaL/2gAIAQMBAT8BYUz1YKZ6sJjNzYTGbm2//8QAGxEAAAcBAAAA"
    "AAAAAAAAAAAAABQWIGJkouL/2gAIAQIBAT8BYgLWOggLWOgfiD8W/wD/xAAUEAEAAAAAAAAA"
    "AAAAAAAAAABQ/9oACAEBAAY/AkP/xAAUEAEAAAAAAAAAAAAAAAAAAABQ/9oACAEBAAE/IUP/"
    "2gAMAwEAAgADAAAAEP8A/wD/AP8A/PP30AD/xAAUEQEAAAAAAAAAAAAAAAAAAAAw/9oACAED"
    "AQE/EERBH//EABgRAAMBAQAAAAAAAAAAAAAAABEg4fAA/9oACAECAQE/EGmdmdsxf//EABQQ"
    "AQAAAAAAAAAAAAAAAAAAAFD/2gAIAQEAAT8QQ//Z")
# Pillow: quads(1024, 576, brick, green, blue, gold).save(f, "JPEG", quality=90, subsampling=2)
JPEG_BIG = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsK"
    "CwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQU"
    "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAJABAADASIA"
    "AhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQA"
    "AAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3"
    "ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWm"
    "p6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEA"
    "AwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSEx"
    "BhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElK"
    "U1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3"
    "uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDzSiii"
    "vyQ/sAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKAPQ6KKK+mP8sgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA88ooor5k/wBTQooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA9Dooor"
    "6Y/yyCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigDzyiiivmT/AFNCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD0Oiiivpj/LIKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAPPKKKK+"
    "ZP8AU0KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKAPQ6KKK+mP8sgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA88ooor5k/wBTQooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA9Dooor"
    "6Y/yyCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigDzyiiivmT/AFNCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD0Oiiivpj/LIKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAPPKKKK+"
    "ZP8AU0KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKAPQ6KKK+mP8sgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA88ooor5k/wBTQooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA9Dooor"
    "6Y/yyCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigDzyiiivmT/AFNCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD0Oiiivpj/LIKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAPPKKKK+"
    "ZP8AU0KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKAPQ6KKK+mP8sgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA88ooor5k/wBTQooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA9Dooor"
    "6Y/yyCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigDzyiiivmT/AFNCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD0Oiiivpj/LIKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAPPKKKK+"
    "ZP8AU0KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKAPQ6KKK+mP8sgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA88ooor5k/wBTQooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA9Dooor"
    "6Y/yyCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigDzyiiivmT/AFNCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD0Oiiivpj/LIKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAPPKKKK+"
    "ZP8AU0KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKAPQ6KKK+mP8sgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA88ooor5k/wBTQooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA9Dooor"
    "6Y/yyCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigDzyiiivmT/AFNCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD0Oiiivpj/LIKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAPHKKKK/"
    "t0/WAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooA/Siiiiv8hz9wCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKK"
    "KACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoooo"
    "AKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD816KKK/14Pw8KKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAP0oooor"
    "/Ic/cAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAK"
    "KKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoo"
    "ooAKKKKACiiigAooooA/Neiiiv8AXg/DwooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/Siiiiv8hz9wCiiigAooooAKKKKACi"
    "iigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKK"
    "KACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD816K"
    "KK/14Pw8KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKAP0oooor/Ic/cAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKA"
    "CiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAK"
    "KKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/Neiiiv8AXg/DwooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/S"
    "iiiiv8hz9wCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACi"
    "iigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKK"
    "KACiiigAooooAKKKKACiiigD816KKK/14Pw8KKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAP0oooor/Ic/cAooooAKKKKACiii"
    "gAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKA"
    "CiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/"
    "Neiiiv8AXg/DwooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooA/Siiiiv8hz9wCiiigAooooAKKKKACiiigAooooAKKKKACiiigA"
    "ooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACi"
    "iigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD816KKK/14Pw8KKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "AP0oooor/Ic/cAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiii"
    "gAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKA"
    "CiiigAooooAKKKKACiiigAooooA/Neiiiv8AXg/DwooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/Siiiiv8hz9wCiiigAoooo"
    "AKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigA"
    "ooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACi"
    "iigD816KKK/14Pw8KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKAP0oooor/Ic/cAooooAKKKKACiiigAooooAKKKKACiiigAoo"
    "ooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiii"
    "gAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/Neiiiv8AXg/DwooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiig"
    "AooooA/Siiiiv8hz9wCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoooo"
    "AKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigA"
    "ooooAKKKKACiiigAooooAKKKKACiiigD816KKK/14Pw8KKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAP0oooor/Ic/cAooooAK"
    "KKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoo"
    "ooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiii"
    "gAooooA/Neiiiv8AXg/DwooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooo"
    "oAKKKKACiiigAooooAKKKKACiiigAooooA/Siiiiv8hz9wCiiigAooooAKKKKACiiigAooooAKKK"
    "KACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoooo"
    "AKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD816KKK/14Pw8KKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKAP0oooor/Ic/cAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAK"
    "KKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoo"
    "ooAKKKKACiiigAooooAKKKKACiiigAooooA/Neiiiv8AXg/DwooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/Siiiiv8hz9wCi"
    "iigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKK"
    "KACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoooo"
    "AKKKKACiiigD816KKK/14Pw8KKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA"
    "KKKKACiiigAooooAKKKKACiiigAooooAKKKKAP0oooor/Ic/cAooooAKKKKACiiigAooooAKKKKA"
    "CiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAK"
    "KKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooA/Neiiiv8AXg/D"
    "wooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAC"
    "iiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKK"
    "KKACiiigAooooA/Siiiiv8hz9wCiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACi"
    "iigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKK"
    "KACiiigAooooAKKKKACiiigAooooAKKKKACiiigD816KKK/14Pw8KKKKACiiigAooooAKKKKACii"
    "igAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKK"
    "ACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAP0oooor/Ic/"
    "cAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKA"
    "CiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAK"
    "KKKACiiigAooooA//9k=")
# Pillow: quads(640, 360, cyan, plum, orange, grey).save(f, "JPEG", quality=90, subsampling=2, progressive=True)
JPEG_BIGP = base64.b64decode(
    "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"
    "c//EABgQAAMBAQAAAAAAAAAAAAAAAAAxMqGg/9oACAEBAAY/AuuBjGMYxjGMYxjGMYxjGMYxjGMY"
    "xjGMYxjGMnSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0nSdJ0ne"
    "uH//xAAXEAADAQAAAAAAAAAAAAAAAAAAEfCg/9oACAEBAAE/IdcEIhEIhEIhEIhEIhEIhEIhEIhE"
    "IhEIhEIhEIhEIhEIhEIhEIhEIhEIhEIhEIhbCUkkkkkkkkkkkkkkkkkkkv/aAAwDAQACAAMAAAAQ"
    "++++++++++++++++++++88888888888888888888++++++++++++++++++++8888888888888888"
    "8888++++++++++++++++++++88888888888888888888++++++++++++++++++++888888888888"
    "88888888++++++++++++++++++++88888888888888888888++++++++++++++++++++88888888"
    "888888888888++++++++++++++++++++88888888888888888888++++++++++++++++++++8888"
    "8888888888888888++++++++++++++++++++88888888888888888888++++++++++++++++++++"
    "88888888888888888888++++++++++++++++++++88888888888888888888yyyyyyyyyyyyyyyy"
    "yyyy88888888888888888888CCCCCCCCCCCCCCCCCCCC88888888888888888888CCCCCCCCCCCC"
    "CCCCCCCC88888888888888888888CCCCCCCCCCCCCCCCCCCC88888888888888888888CCCCCCCC"
    "CCCCCCCCCCCC88888888888888888888CCCCCCCCCCCCCCCCCCCC88888888888888888888CCCC"
    "CCCCCCCCCCCCCCCC88888888888888888888CCCCCCCCCCCCCCCCCCCC88888888888888888888"
    "CCCCCCCCCCCCCCCCCCCC88888888888888888888CCCCCCCCCCCCCCCCCCCC8888888888888888"
    "8888CCCCCCCCCCCCCCCCCCCC88888888888888888888CCCCCCCCCCCCCCCCCCCC888888888888"
    "88888888/8QAFBEBAAAAAAAAAAAAAAAAAAAAkP/aAAgBAwEBPxBmI4444444444444444444yiii"
    "iiiiiiiiiiiiiiiiv//EABgRAQADAQAAAAAAAAAAAAAAAAEA0fCA/9oACAECAQE/EOzJZZZZZZZZ"
    "ZZZZZZZZZZZdwqbhU3CpuFTcKm4VNwqbhU3CpuFTcKm4VNwqbhU3CpuFTcKm4VNwqbhXXP8A/8QA"
    "FxAAAwEAAAAAAAAAAAAAAAAAAPDxoP/aAAgBAQABPxDXAtC0LQtC0LQtC0LQtC0LQtC0LQtC0LQt"
    "C0LQtC0LQtC0LQtC0LQtC0LQtC0LQtC0LQtDyPI8jyPI8jyPI8jyPI8jyPI8jyPI8jyPI8jyPI8j"
    "yPI8jyPI8jyPI8jyPI8jyPI8jzrh/9k=")


def quads(w, h, cols):
    """w x h pixels (rows), the four quadrants' values"""
    return [[cols[(1 if x >= w // 2 else 0) + (2 if y >= h // 2 else 0)] for x in range(w)] for y in range(h)]


def png(rows, palette=None, trns=None, interlace=0):
    """An 8-bit RGB PNG (rows of (r, g, b)), or a palette one (rows of indices)"""
    h, w = len(rows), len(rows[0])

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    ctype, bpp = (3, 1) if palette else (2, 3)

    def raw(r):
        return bytes(r) if palette else b"".join(bytes(px) for px in r)

    def filtered(r, prev, f):
        out = bytearray([f])
        for i, v in enumerate(r):
            a = r[i - bpp] if i >= bpp else 0
            b = prev[i] if prev else 0
            c = prev[i - bpp] if prev and i >= bpp else 0
            p = a + b - c
            pr = a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else b if abs(p - b) <= abs(p - c) else c
            out.append((v - (0, a, b, (a + b) // 2, pr)[f]) & 255)
        return bytes(out)

    data = bytearray()
    passes = ([(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
              if interlace else [(0, 0, 1, 1)])
    for x0, y0, dx, dy in passes:
        prev = None
        for k, y in enumerate(range(y0, h, dy)):
            sub = [rows[y][x] for x in range(x0, w, dx)]
            if not sub:
                continue
            r = raw(sub)
            data += filtered(r, prev, (k + y) % 5)          # every filter in turn
            prev = r
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, interlace))
    if palette:
        out += chunk(b"PLTE", b"".join(bytes(c) for c in palette))
        if trns is not None:
            out += chunk(b"tRNS", bytes(trns))
    return out + chunk(b"IDAT", zlib.compress(bytes(data), 9)) + chunk(b"IEND", b"")


def lzw(pix, min_size):
    """GIF LZW: codes of growing size, a clear code first and when the table
    is full"""
    clear, eoi = 1 << min_size, (1 << min_size) + 1
    codes, size, nxt = [(clear, min_size + 1)], min_size + 1, eoi + 1
    table, cur = {}, None
    for v in pix:
        if cur is None:
            cur = v
            continue
        if (cur, v) in table:
            cur = table[(cur, v)]
            continue
        codes.append((cur, size))
        if nxt == 4096:
            codes.append((clear, size))
            table, size, nxt = {}, min_size + 1, eoi + 1
        else:
            table[(cur, v)] = nxt
            if nxt == 1 << size:
                size += 1
            nxt += 1
        cur = v
    codes += [(cur, size), (eoi, size)]
    bits, nb, data = 0, 0, bytearray()
    for c, n in codes:
        bits |= c << nb
        nb += n
        while nb >= 8:
            data.append(bits & 255)
            bits >>= 8
            nb -= 8
    if nb:
        data.append(bits & 255)
    return bytes(data)


def gif(w, h, palette, frames):
    """A GIF, a 16-colour global palette: frames of (x, y, rows of indices,
    delay in 1/100 s, transparent index or None)"""
    out = bytearray(b"GIF89a" + struct.pack("<HHBBB", w, h, 0xF3, 0, 0))
    for c in palette + [(0, 0, 0)] * (16 - len(palette)):
        out += bytes(c)
    out += b"\x21\xFF\x0bNETSCAPE2.0\x03\x01\x00\x00\x00"
    for fx, fy, rows, delay, tr in frames:
        fh, fw = len(rows), len(rows[0])
        out += b"\x21\xF9\x04" + bytes([1 if tr is not None else 0]) + struct.pack("<H", delay) + bytes([tr or 0, 0])
        out += b"\x2C" + struct.pack("<HHHHB", fx, fy, fw, fh, 0)
        data = lzw([v for r in rows for v in r], 4)
        out += bytes([4])
        for i in range(0, len(data), 255):
            out += bytes([len(data[i:i + 255])]) + data[i:i + 255]
        out += b"\x00"
    return bytes(out + b"\x3B")


def bmp(rows):
    """A 24-bit bottom-up BMP"""
    h, w = len(rows), len(rows[0])
    stride = (w * 3 + 3) & ~3
    pix = b"".join(b"".join(bytes((px[2], px[1], px[0])) for px in rows[y]) + bytes(stride - w * 3)
                   for y in range(h - 1, -1, -1))
    return (b"BM" + struct.pack("<IHHI", 54 + len(pix), 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(pix), 2835, 2835, 0, 0) + pix)


def qoi(rows):
    """A QOI (RGB): runs, the index, full colours"""
    h, w = len(rows), len(rows[0])
    out = bytearray(b"qoif" + struct.pack(">IIBB", w, h, 3, 0))
    idx = [None] * 64
    prev, run = (0, 0, 0), 0
    px = [p for r in rows for p in r]
    for i, p in enumerate(px):
        if p == prev:
            run += 1
            if run == 62 or i == len(px) - 1:
                out.append(0xC0 | (run - 1))
                run = 0
            continue
        if run:
            out.append(0xC0 | (run - 1))
            run = 0
        hs = (p[0] * 3 + p[1] * 5 + p[2] * 7 + 255 * 11) % 64
        if idx[hs] == p:
            out.append(hs)
        else:
            idx[hs] = p
            out += bytes([0xFE, p[0], p[1], p[2]])
        prev = p
    return bytes(out + bytes(7) + b"\x01")


def files():
    pal = [(220, 40, 40), (230, 220, 40), (40, 190, 70), (0, 0, 0)]
    gpal = [(40, 60, 230), (230, 40, 40)]
    return [
        ("A1BASE.JPG", JPEG_BASE),
        ("A2PROG.JPG", JPEG_PROG),
        ("A3RGB.PNG", png(quads(48, 36, [(240, 140, 30), (30, 160, 150), (128, 128, 128), (140, 50, 200)]))),
        ("A4PAL.PNG", png(quads(48, 36, [0, 1, 2, 3]), palette=pal, trns=[255, 255, 255, 0], interlace=1)),
        ("A5ANIM.GIF", gif(32, 24, gpal, [(0, 0, [[0] * 32 for _ in range(24)], 4, None),
                                          (0, 0, [[1] * 32 for _ in range(12)] + [[2] * 32 for _ in range(12)], 4, 2)])),
        ("A6QUAD.BMP", bmp(quads(40, 30, [(255, 255, 255), (230, 30, 30), (30, 200, 60), (40, 60, 230)]))),
        ("A7QUAD.QOI", qoi(quads(40, 30, [(30, 220, 220), (220, 40, 210), (230, 220, 30), (250, 250, 250)]))),
        ("A8BAD.JPG", JPEG_BASE[:300]),
        ("B1BASE.JPG", JPEG_BIG),
        ("B2PROG.JPG", JPEG_BIGP),
        ("B3RGB.PNG", png(quads(520, 296, [(30, 40, 120), (150, 220, 40), (240, 130, 110), (120, 120, 30)]))),
        ("B4BIG.GIF", gif(512, 290, [(240, 120, 180), (20, 140, 140), (200, 150, 40), (130, 60, 200)],
                          [(0, 0, quads(512, 290, [0, 1, 2, 3]), 0, None)])),
        ("B5BIG.BMP", bmp(quads(496, 280, [(120, 190, 240), (170, 70, 30), (150, 230, 190), (70, 80, 100)]))),
    ]


def main():
    out = sys.argv[1]
    tree = {"APPS": {}, "MEDIA": {"PICTURES": dict(files()), "MUSIC": {}}}
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
    print("sd_image: %d pictures in /MEDIA/PICTURES, %d bytes" % (len(files()), len(img)))


if __name__ == "__main__":
    main()

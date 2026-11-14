# dump.py out.png : the displayed 320x200 8-bpp frame (VRAM 0x82000000 + FRAME_A) with LUT 0, as PNG
import sys, struct, zlib, pylink
VRAM, LUT, W, H = 0x82000000, 0x10050800, 320, 200
j = pylink.JLink(); j.open(); j.set_tif(pylink.enums.JLinkInterfaces.JTAG); j.set_speed(4000); j.connect("RISC-V")
j._dll.JLINKARM_Halt()
words = j.memory_read32(VRAM, W * H // 4)
lut = j.memory_read32(LUT, 256)
j.restart(); j.close()
pix = b"".join(struct.pack("<I", w) for w in words)
rows = b"".join(b"\x00" + b"".join(struct.pack(">I", lut[p])[1:] for p in pix[y * W:(y + 1) * W]) for y in range(H))
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
open(sys.argv[1] + ".raw", "wb").write(pix)
print("saved", sys.argv[1])

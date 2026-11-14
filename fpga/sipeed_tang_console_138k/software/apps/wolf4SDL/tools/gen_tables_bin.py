#!/usr/bin/env python3
"""TABLES.BIN, the tables of port/wolf_tables.c for a WOLF_SD_TABLES build
(arv/wolf_tables_sd.c reads it from the program's folder on the card):

    gen_tables_bin.py port/wolf_tables.c TABLES.BIN

"WTB1", the entry counts of finetangent, sintable and pixelangle_bp (32-bit
little-endian), then the three tables (int32, int32, uint16), little-endian."""
import re, struct, sys

src, out = sys.argv[1], sys.argv[2]
text = open(src).read()

def table(name):
    m = re.search(r"\b%s\[[^\]]*\]\s*=\s*\{(.*?)\};" % name, text, re.S)
    if not m:
        sys.exit("%s: no table %s" % (src, name))
    return [int(v, 0) for v in re.findall(r"-?(?:0x[0-9a-fA-F]+|\d+)", m.group(1))]

tan, sin, bp = table("finetangent"), table("sintable"), table("pixelangle_bp")
if (len(tan), len(sin), len(bp)) != (900, 450, 363):
    sys.exit("unexpected table sizes %d %d %d" % (len(tan), len(sin), len(bp)))
data = b"WTB1" + struct.pack("<3I", len(tan), len(sin), len(bp))
data += struct.pack("<%di" % len(tan), *tan) + struct.pack("<%di" % len(sin), *sin)
data += struct.pack("<%dH" % len(bp), *bp)
open(out, "wb").write(data)
print("%s: %d bytes" % (out, len(data)))

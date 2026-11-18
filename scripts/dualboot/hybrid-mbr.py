#!/usr/bin/env python3
# Rewrites the partition entries of sector 0 of a GPT disk (here: the nested
# table in userdata) as a hybrid MBR: entry 0 points at GPT partition 1 (the
# boot partition) so lk2nd, which only reads MBR, can find extlinux.conf on it;
# entry 1 is the 0xEE GPT protective entry so Linux keeps using the GPT.
# usage: hybrid-mbr.py <device> [--write]
import struct, sys
dev = sys.argv[1]
with open(dev, "rb") as f:
    s0 = bytearray(f.read(512))
    hdr = f.read(512)
    assert hdr[:8] == b"EFI PART", "no GPT header at LBA 1"
    entries_lba, n, esz = struct.unpack_from("<QII", hdr, 72)
    f.seek(entries_lba * 512)
    e = f.read(esz)
first, last = struct.unpack_from("<QQ", e, 32)
assert first and last >= first and last < 2**32
def ent(status, typ, start, length):
    return struct.pack("<B3sB3sII", status, b"\xfe\xff\xff", typ, b"\xfe\xff\xff", start, length)
tbl = ent(0, 0x83, first, last - first + 1) + ent(0, 0xEE, 1, first - 1) + bytes(32)
new = s0[:446] + tbl + b"\x55\xaa"
print("old", s0[446:512].hex())
print("new", new[446:512].hex())
if "--write" in sys.argv:
    with open(dev, "r+b") as f:
        f.write(new)
    print("written")

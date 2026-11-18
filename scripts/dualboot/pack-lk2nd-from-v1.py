#!/usr/bin/env python3
# Pack a new lk.bin the way lk2nd-v1 is packed for S-Boot: take lk2nd-v1's image
# (stock device trees with the lk2nd node, header, cmdline, empty ramdisk,
# SEANDROIDENFORCE) and swap only the gzip'd lk.bin. Repacking v1's own lk.bin
# this way gives v1's kernel section byte for byte.
# usage: pack-lk2nd-from-v1.py <lk2nd-v1.img> <lk.bin> <mkbootimg> <out.img>
import hashlib, os, struct, subprocess, sys, tempfile, zlib
v1p, lkp, mkbootimg, out = sys.argv[1:]
v1 = open(v1p, "rb").read()
ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack_from("<8I", v1, 8)
d = zlib.decompressobj(31); d.decompress(v1[ps:ps + ks]); dtbs = d.unused_data
roff = ps + ((ks + ps - 1) // ps) * ps
w = tempfile.mkdtemp()
gz = zlib.compressobj(9, zlib.DEFLATED, 31)
open(f"{w}/kernel", "wb").write(gz.compress(open(lkp, "rb").read()) + gz.flush() + dtbs)
open(f"{w}/ramdisk", "wb").write(v1[roff:roff + rs])
base = ka - 0x8000
subprocess.run([sys.executable, mkbootimg, "--kernel", f"{w}/kernel", "--ramdisk", f"{w}/ramdisk",
    "--cmdline", v1[64:576].split(b"\0")[0].decode(), "--base", hex(base),
    "--kernel_offset", hex(ka - base), "--ramdisk_offset", hex(ra - base), "--tags_offset", hex(ta - base),
    "--second_offset", "0xf00000", "--pagesize", str(ps), "--output", out], check=True)
img = bytearray(open(out, "rb").read())
struct.pack_into("<I", img, 28, struct.unpack_from("<I", v1, 28)[0])
struct.pack_into("<I", img, 44, struct.unpack_from("<I", v1, 44)[0])
open(out, "wb").write(img + b"SEANDROIDENFORCE")
print(out, len(img) + 16, hashlib.sha256(img + b"SEANDROIDENFORCE").hexdigest())

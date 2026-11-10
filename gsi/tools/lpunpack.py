#!/usr/bin/env python3
"""Unpack an Android super image (raw or sparse) into partition images.

usage: lpunpack.py super.img outdir [partition...]

Handles Android sparse images (converted on the fly) and the LP metadata
format (liblp, metadata v10.x).  Only the primary metadata slot 0 is read.
"""
import os
import struct
import sys

SPARSE_MAGIC = 0xED26FF3A
GEOMETRY_MAGIC = 0x616C4467
HEADER_MAGIC = 0x414C5030
SECTOR = 512


def unsparse(path, out):
    """Write the raw image of sparse image path to out."""
    with open(path, 'rb') as f, open(out, 'wb') as o:
        hdr = f.read(28)
        (magic, major, minor, fhdr_sz, chdr_sz, blk_sz, total_blks,
         total_chunks, _) = struct.unpack('<IHHHHIIII', hdr)
        assert magic == SPARSE_MAGIC
        f.seek(fhdr_sz)
        for _ in range(total_chunks):
            ctype, _, blocks, total = struct.unpack('<HHII', f.read(chdr_sz)[:12])
            size = blocks * blk_sz
            if ctype == 0xCAC1:		# raw
                left = size
                while left:
                    n = min(left, 1 << 24)
                    o.write(f.read(n))
                    left -= n
            elif ctype == 0xCAC2:	# fill
                fill = f.read(4) * (blk_sz // 4)
                for _ in range(blocks):
                    o.write(fill)
            elif ctype == 0xCAC3:	# don't care
                o.seek(size, 1)
            elif ctype == 0xCAC4:	# crc32
                f.read(4)
            else:
                sys.exit('bad chunk type %x' % ctype)
        o.truncate(total_blks * blk_sz)


def main():
    src, outdir = sys.argv[1], sys.argv[2]
    want = set(sys.argv[3:])
    with open(src, 'rb') as f:
        if struct.unpack('<I', f.read(4))[0] == SPARSE_MAGIC:
            raw = src + '.raw'
            print('unsparsing to', raw)
            unsparse(src, raw)
            src = raw
    os.makedirs(outdir, exist_ok=True)
    with open(src, 'rb') as f:
        f.seek(4096)
        geo = f.read(52)
        magic, _, _ = struct.unpack('<II32s', geo[:40])
        assert magic == GEOMETRY_MAGIC, 'no LP geometry'
        max_size, slots, lbs = struct.unpack('<III', geo[40:52])
        f.seek(4096 + 4096 * 2)		# primary metadata, slot 0
        hdr = f.read(256)
        (magic, major, minor, hdr_size) = struct.unpack('<IHHI', hdr[:12])
        assert magic == HEADER_MAGIC, 'no LP metadata header'
        tables_size = struct.unpack('<I', hdr[44:48])[0]
        descs = struct.unpack('<12I', hdr[80:128])
        (p_off, p_num, p_sz, e_off, e_num, e_sz,
         g_off, g_num, g_sz, b_off, b_num, b_sz) = descs
        f.seek(4096 + 4096 * 2 + hdr_size)
        tables = f.read(tables_size)
        extents = []
        for i in range(e_num):
            e = tables[e_off + i * e_sz:e_off + (i + 1) * e_sz]
            extents.append(struct.unpack('<QIQI', e[:24]))
        for i in range(p_num):
            p = tables[p_off + i * p_sz:p_off + (i + 1) * p_sz]
            name = p[:36].split(b'\0')[0].decode()
            attrs, first, num, group = struct.unpack('<IIII', p[36:52])
            size = sum(extents[first + j][0] for j in range(num)) * SECTOR
            print('%-24s %6d MiB  %d extent(s)' % (name, size >> 20, num))
            if want and name not in want:
                continue
            if not num:
                continue
            with open(os.path.join(outdir, name + '.img'), 'wb') as o:
                for j in range(num):
                    sectors, ttype, tdata, _ = extents[first + j]
                    if ttype == 0:		# linear
                        f.seek(tdata * SECTOR)
                        left = sectors * SECTOR
                        while left:
                            n = min(left, 1 << 24)
                            o.write(f.read(n))
                            left -= n
                    else:			# zero
                        o.seek(sectors * SECTOR, 1)
                o.truncate(size)


if __name__ == '__main__':
    main()

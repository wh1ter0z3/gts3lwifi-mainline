// SPDX-License-Identifier: MIT
/*
 * gsi-unzip: extracts one image from a zip, zip64 included (a GSI's 6 GB
 * system.img), stored or deflated, with its CRC checked. For the TWRP
 * installer: the Android 9 unzip in recovery reads no zip64.
 * usage: gsi-unzip -l <zip>                  list the .img members
 *        gsi-unzip <zip> <out|-> [member]    extract member (default: the
 *                                            first *.img), "-" for stdout
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

static int zfd;

static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

static int rd(void *buf, size_t n, off_t off)
{
	return pread(zfd, buf, n, off) == (ssize_t)n ? 0 : -1;
}

static int die(const char *msg)
{
	fprintf(stderr, "gsi-unzip: %s\n", msg);
	return 1;
}

struct entry {
	char name[256];
	uint16_t method;
	uint32_t crc;
	uint64_t csize, usize, lho;
};

/* the central directory: offset and entry count, from the (zip64) end record */
static int central_dir(off_t size, uint64_t *cd_off, uint64_t *count)
{
	uint8_t tail[65536 + 22], z[56];
	off_t start = size > (off_t)sizeof(tail) ? size - (off_t)sizeof(tail) : 0;
	size_t n = size - start;
	long i;

	if (rd(tail, n, start))
		return -1;
	for (i = (long)n - 22; i >= 0; i--)
		if (le32(tail + i) == 0x06054b50)
			break;
	if (i < 0)
		return -1;
	*count = le16(tail + i + 10);
	*cd_off = le32(tail + i + 16);
	/* zip64: a locator right before the end record points to the zip64 end */
	if (i >= 20 && le32(tail + i - 20) == 0x07064b50) {
		uint64_t z64 = le64(tail + i - 20 + 8);

		if (rd(z, sizeof(z), z64) || le32(z) != 0x06064b50)
			return -1;
		*count = le64(z + 32);
		*cd_off = le64(z + 48);
	}
	return 0;
}

/* calls fn for each entry; stops when it returns non-zero */
static int each_entry(int (*fn)(struct entry *, void *), void *arg)
{
	struct stat st;
	uint64_t off, count, k;
	uint8_t h[46], var[65536 * 3];

	if (fstat(zfd, &st) || central_dir(st.st_size, &off, &count))
		return die("not a zip file");
	for (k = 0; k < count; k++) {
		struct entry e = { 0 };
		uint16_t nl, xl, cl, j;
		const uint8_t *x;

		if (rd(h, sizeof(h), off) || le32(h) != 0x02014b50)
			return die("bad central directory");
		nl = le16(h + 28);
		xl = le16(h + 30);
		cl = le16(h + 32);
		if (rd(var, nl + xl, off + 46))
			return die("bad central directory");
		memcpy(e.name, var, nl < sizeof(e.name) - 1 ? nl : sizeof(e.name) - 1);
		e.method = le16(h + 10);
		e.crc = le32(h + 16);
		e.csize = le32(h + 20);
		e.usize = le32(h + 24);
		e.lho = le32(h + 42);
		/* zip64 extra field: the 0xffffffff fields, in this order */
		for (x = var + nl; x + 4 <= var + nl + xl; x += 4 + le16(x + 2)) {
			const uint8_t *v = x + 4;

			if (le16(x) != 0x0001)
				continue;
			if (e.usize == 0xffffffff)
				e.usize = le64(v), v += 8;
			if (e.csize == 0xffffffff)
				e.csize = le64(v), v += 8;
			if (e.lho == 0xffffffff)
				e.lho = le64(v);
		}
		off += 46 + nl + xl + cl;
		j = fn(&e, arg);
		if (j)
			return j == 2 ? 0 : j;
	}
	return 0;
}

static int is_img(const char *n)
{
	size_t l = strlen(n);

	return l > 4 && !strcmp(n + l - 4, ".img");
}

static int list_fn(struct entry *e, void *arg)
{
	(void)arg;
	if (is_img(e->name))
		printf("%llu %s\n", (unsigned long long)e->usize, e->name);
	return 0;
}

struct pick {
	const char *want;
	struct entry e;
	int found;
};

static int pick_fn(struct entry *e, void *arg)
{
	struct pick *p = arg;

	if (p->want ? strcmp(e->name, p->want) : !is_img(e->name))
		return 0;
	p->e = *e;
	p->found = 1;
	return 2;	/* stop */
}

static int extract(const struct entry *e, int out)
{
	static uint8_t in[1 << 20], buf[1 << 20];
	uint8_t lh[30];
	uint64_t pos, left = e->csize, done = 0, step = 0;
	uLong crc = crc32(0, NULL, 0);
	z_stream z = { 0 };
	int ret = Z_OK;

	if (rd(lh, sizeof(lh), e->lho) || le32(lh) != 0x04034b50)
		return die("bad local header");
	pos = e->lho + 30 + le16(lh + 26) + le16(lh + 28);
	if (e->method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK)
		return die("zlib");
	if (e->method != 0 && e->method != 8)
		return die("unsupported compression");
	while (left || (e->method == 8 && ret != Z_STREAM_END)) {
		size_t n = left < sizeof(in) ? left : sizeof(in);

		if (n && rd(in, n, pos))
			return die("read error");
		pos += n;
		left -= n;
		if (e->method == 0) {
			crc = crc32(crc, in, n);
			if (write(out, in, n) != (ssize_t)n)
				return die("write error");
			done += n;
		} else {
			z.next_in = in;
			z.avail_in = n;
			do {
				size_t got;

				z.next_out = buf;
				z.avail_out = sizeof(buf);
				ret = inflate(&z, Z_NO_FLUSH);
				if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
					return die("corrupt data");
				got = sizeof(buf) - z.avail_out;
				crc = crc32(crc, buf, got);
				if (got && write(out, buf, got) != (ssize_t)got)
					return die("write error");
				done += got;
			} while (z.avail_in || (!z.avail_out && ret != Z_STREAM_END));
			if (!n && ret != Z_STREAM_END)
				return die("truncated data");
		}
		if (done >> 28 != step) {	/* every 256 MiB */
			step = done >> 28;
			fprintf(stderr, "gsi-unzip: %llu of %llu MiB\n", (unsigned long long)(done >> 20),
				(unsigned long long)(e->usize >> 20));
		}
	}
	if (e->method == 8)
		inflateEnd(&z);
	if (done != e->usize || crc != e->crc)
		return die("size or CRC mismatch: damaged zip");
	return 0;
}

int main(int argc, char **argv)
{
	struct pick p = { 0 };
	int out, ret;

	if (argc == 3 && !strcmp(argv[1], "-l")) {
		if ((zfd = open(argv[2], O_RDONLY | O_CLOEXEC)) < 0)
			return die(strerror(errno));
		return each_entry(list_fn, NULL);
	}
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: gsi-unzip -l <zip>\n       gsi-unzip <zip> <out|-> [member]\n");
		return 1;
	}
	if ((zfd = open(argv[1], O_RDONLY | O_CLOEXEC)) < 0)
		return die(strerror(errno));
	p.want = argc == 4 ? argv[3] : NULL;
	if (each_entry(pick_fn, &p))
		return 1;
	if (!p.found)
		return die("no such image in the zip");
	out = strcmp(argv[2], "-") ? open(argv[2], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : 1;
	if (out < 0)
		return die(strerror(errno));
	ret = extract(&p.e, out);
	if (out != 1 && (fsync(out) || close(out)))
		return die("write error");
	if (!ret)
		fprintf(stderr, "gsi-unzip: %s: %llu bytes, CRC ok\n", p.e.name, (unsigned long long)p.e.usize);
	return ret;
}

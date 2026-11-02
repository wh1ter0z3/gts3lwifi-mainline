#!/usr/bin/env python3
"""Build lk2nd for both models and pack it the way S-Boot accepts it: gzip'd lk.bin
followed by the stock device trees of a donor boot image with lk2nd's nodes merged
in, stock load addresses, page size 4096, SEANDROIDENFORCE trailer, empty ramdisk.
See docs/lk2nd.md.

Source: upstream lk2nd at the lock's commit plus lk2nd/preview-*.patch. Donors are
boot images for that model; they aren't in git, the lock has their hashes. A built
image holds the donor's device trees.

--t820-image names an existing image to release instead of the new build, after
checking its hash against the lock and comparing it section by section with the
build. That is how the image on our SM-T820 since 2026-10-28 goes out unchanged.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PARTITION_SIZE = "4*1024*1024"  # lk2nd keeps the first 4 MiB of boot
BASE, KERNEL_OFF, RAMDISK_OFF, TAGS_OFF, SECOND_OFF, PAGE = 0x80000000, 0x8000, 0x2200000, 0x2000000, 0xf00000, 4096
EPOCH = 1793491200


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def dtc(*args, data=None):
    return run(["dtc", *args], input=data, capture_output=True).stdout


def empty_ramdisk():
    """An empty newc archive: "." and the trailer, nothing that changes per build."""
    def entry(name, mode, nlink):
        n = name.encode() + b"\0"
        h = b"070701" + b"".join(b"%08X" % v for v in (0, mode, 0, 0, nlink, 0, 0, 0, 0, 0, 0, len(n), 0))
        return (h + n).ljust((len(h) + len(n) + 3) & ~3, b"\0")
    c = entry(".", 0o40755, 2) + entry("TRAILER!!!", 0, 1)
    c = c.ljust((len(c) + 511) & ~511, b"\0")
    gz = zlib.compressobj(9, zlib.DEFLATED, 31)
    return gz.compress(c) + gz.flush()


def pack(build, donor, dtb_name, out_img):
    with open(donor, "rb") as f:
        b = f.read()
    ks, _, _, _, _, _, _, ps = struct.unpack_from("<8I", b, 8)
    k = b[ps:ps + ks]
    if k[:2] == b"\x1f\x8b":
        d = zlib.decompressobj(31)
        d.decompress(k)
        k = d.unused_data
    lk = dtc("-I", "dtb", "-O", "dts", f"{build}/build-lk2nd-msm8996/lk2nd/device/dts/msm8996/{dtb_name}.dtb").decode()
    nodes = "".join(re.search(r"^\t" + n + r" \{\n.*?^\t\};\n", lk, re.S | re.M).group(0) for n in ("lk2nd", "lk2nd-hw"))
    dtbs = []
    for m in re.finditer(b"\xd0\x0d\xfe\xed", k):
        total = struct.unpack_from(">I", k, m.start() + 4)[0]
        if not 100000 < total < 2000000:
            continue
        src = dtc("-I", "dtb", "-O", "dts", "-", data=k[m.start():m.start() + total]).decode()
        if re.search(r"^\tlk2nd \{", src, re.M):
            sys.exit("donor device trees already contain an lk2nd node")
        dtbs.append(dtc("-q", "-I", "dts", "-O", "dtb", "-p", "4096", "-", data=(src + "\n/ {\n" + nodes + "};\n").encode()))
    if not dtbs:
        sys.exit("no stock device trees in the donor image")
    with open(f"{build}/build-lk2nd-msm8996/lk.bin", "rb") as f:
        gz = zlib.compressobj(9, zlib.DEFLATED, 31)
        kernel = gz.compress(f.read()) + gz.flush() + b"".join(dtbs)
    with open(f"{build}/kernel", "wb") as f:
        f.write(kernel)
    with open(f"{build}/ramdisk", "wb") as f:
        f.write(empty_ramdisk())
    cmdline = b[64:576].split(b"\0")[0].decode()
    os_version = struct.unpack_from("<I", b, 44)[0]
    run([sys.executable, "/build/src/lk2nd/scripts/mkbootimg", "--kernel", f"{build}/kernel",
         "--ramdisk", f"{build}/ramdisk", "--cmdline", cmdline, "--base", hex(BASE),
         "--kernel_offset", hex(KERNEL_OFF), "--ramdisk_offset", hex(RAMDISK_OFF),
         "--tags_offset", hex(TAGS_OFF), "--second_offset", hex(SECOND_OFF),
         "--pagesize", str(PAGE), "--output", out_img])
    with open(out_img, "rb") as f:
        img = bytearray(f.read())
    struct.pack_into("<I", img, 28, 0)  # second_addr and os_version as in stock
    struct.pack_into("<I", img, 44, os_version)
    with open(out_img, "wb") as f:
        f.write(img + b"SEANDROIDENFORCE")
    print(f"{dtb_name}: lk2nd in {len(dtbs)} stock device trees, {len(img) + 16} bytes")


def inside(dtb_name, version):
    """In the container: /src (read-only), /donor.img, /out."""
    os.makedirs("/build")
    run(["cp", "-a", "/src", "/build/src"])
    with open("/out/build.log", "w") as log:
        p = subprocess.run(["make", "-C", "/build/src", "BOOTLOADER_OUT=/build/out", "TOOLCHAIN_PREFIX=arm-none-eabi-",
                            f"LK2ND_PARTITION_SIZE={PARTITION_SIZE}", f"LK2ND_VERSION={version}", "lk2nd-msm8996",
                            f"-j{os.environ.get('JOBS', '16')}"],
                           stdout=log, stderr=subprocess.STDOUT)
    if p.returncode:
        sys.stderr.write("".join(open("/out/build.log").readlines()[-20:]))
        sys.exit("lk2nd build failed")
    pack("/build/out", "/donor.img", dtb_name, "/out/lk2nd.img")


def sections(path):
    with open(path, "rb") as f:
        b = f.read()
    ks, _, rs, _, _, _, _, ps = struct.unpack_from("<8I", b, 8)
    r = (ps + ks + ps - 1) // ps * ps
    hexs = lambda x: hashlib.sha256(x).hexdigest()[:16]
    return {"kernel+dtbs": hexs(b[ps:ps + ks]), "ramdisk": hexs(b[r:r + rs]), "cmdline": hexs(b[64:576]), "os_version": hexs(b[44:48])}


def odin_tar(img, dest):
    """What Odin takes in the AP slot: a tar with boot.img, as the lk2nd-v1 release."""
    ti = tarfile.TarInfo("boot.img")
    ti.size = os.path.getsize(img)
    ti.mode = 0o644
    ti.mtime = EPOCH
    with tarfile.open(dest, "w", format=tarfile.USTAR_FORMAT) as t, open(img, "rb") as f:
        t.addfile(ti, f)


def source_tree(lock, out, name, patches, src):
    tree = os.path.join(out, "src-" + name)
    shutil.rmtree(tree, ignore_errors=True)
    os.makedirs(tree)
    p = subprocess.Popen(["git", "-C", src, "archive", lock["base"]], stdout=subprocess.PIPE)
    run(["tar", "-x", "-C", tree], stdin=p.stdout)
    if p.wait():
        sys.exit("git archive failed: is the lock's base commit in " + src + "?")
    run(["git", "init", "-q"], cwd=tree)  # or git apply skips paths when run under another repository
    for patch in patches:
        run(["git", "apply", "--whitespace=nowarn", os.path.join(REPO, patch)], cwd=tree)
    return tree


def build_one(a, lock, out, model):
    m = lock[model]
    donor = getattr(a, model + "_donor")
    if not donor:
        print(f"{model}: no donor given, not built")
        return None
    if sha256(donor) != m["donor_sha256"]:
        sys.exit(f"{model}: donor hash differs from the lock")
    src = os.path.join(out, "lk2nd.git")
    if not os.path.isdir(src):
        run(["git", "init", "-q", "--bare", src])
        run(["git", "-C", src, "fetch", "-q", "--depth=1", lock["url"], lock["base"]])
    tree = source_tree(lock, out, model, m["patches"], src)
    if not os.path.exists(os.path.join(tree, "lk2nd/device/dts/msm8996", m["dtb"] + ".dts")):
        sys.exit(f"{m['dtb']}.dts isn't in the tree: a patch didn't apply")
    bout = os.path.join(out, "build-" + model)
    shutil.rmtree(bout, ignore_errors=True)
    os.makedirs(bout)
    run(["podman", "run", "--rm", "--pull=never", "--network=none", "-e", f"JOBS={a.jobs}",
         "-v", f"{tree}:/src:ro", "-v", f"{os.path.abspath(donor)}:/donor.img:ro",
         "-v", f"{HERE}:/tools:ro", "-v", f"{bout}:/out", a.image,
         "python3", "-I", "/tools/lk2nd-images.py", "--inside", m["dtb"], "--version", m["version"]])
    shutil.rmtree(tree)
    return os.path.join(bout, "lk2nd.img")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lock")
    ap.add_argument("--out")
    ap.add_argument("--t820-donor")
    ap.add_argument("--t825-donor")
    ap.add_argument("--t820-image")
    ap.add_argument("--image", default="localhost/gts3-preview-lk2nd:ubuntu22.04")
    ap.add_argument("--jobs", default="16")
    ap.add_argument("--inside", help=argparse.SUPPRESS)
    ap.add_argument("--version", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.inside:
        return inside(a.inside, a.version)
    lock = json.load(open(a.lock))["lk2nd"]
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    run(["podman", "build", "-q", "-t", a.image, "-f", os.path.join(HERE, "lk2nd.Containerfile"), HERE])
    for model, base in (("t820", "lk2nd-gts3lwifi"), ("t825", "lk2nd-gts3llte")):
        built = build_one(a, lock, out, model)
        chosen = built
        if built:
            print(model, "built", sha256(built), sections(built))
            if lock[model].get("build_sha256") not in (None, sha256(built)):
                print(f"{model}: DRIFT, the build differs from the lock's {lock[model]['build_sha256']}")
        if model == "t820" and a.t820_image:
            if sha256(a.t820_image) != lock[model]["image_sha256"]:
                sys.exit("--t820-image differs from the lock's hash")
            print(model, "given", sha256(a.t820_image), sections(a.t820_image))
            chosen = a.t820_image
        if not chosen:
            continue
        dest = os.path.join(out, base + ".img")
        shutil.copyfile(chosen, dest)
        odin_tar(dest, os.path.join(out, base + "-odin.tar"))
        print(model, "release", sha256(dest), os.path.getsize(dest))
    print(time.strftime("done %H:%M:%S"))


if __name__ == "__main__":
    main()

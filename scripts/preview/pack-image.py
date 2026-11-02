#!/usr/bin/env python3
"""Turn pmbootstrap's rootfs into the images. Runs as root in the builder container.

This is the end of pmbootstrap's install_system_image(), without loop devices:
same clean-up, same sizes, same GPT, same mkfs options, then mke2fs -d instead of a
mount and cp -a. It also keeps the apks and writes the manifest and file list.
"""
import base64
import glob
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys

SECTOR = 512
MIB = 1024 * 1024
TYPE_BOOT = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
TYPE_ROOT = "B921B045-1DF0-41C3-AF44-4C6F280D3FAE"


def sh(*cmd, **kw):
    cmd = [str(c) for c in cmd]
    print("+", " ".join(cmd)[:240], flush=True)
    return subprocess.run(cmd, check=True, **kw)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def stage_rootfs(job):
    """The chroot as it would land on the partitions, split into / and /boot."""
    stage = job["stage"]
    root = os.path.join(stage, "root")
    boot = os.path.join(stage, "boot")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    sh("cp", "-a", "--reflink=auto", job["chroot"], root)

    for f in ("in-pmbootstrap", "usr/bin/qemu-aarch64-static", "tmp/apk_progress_fifo"):
        p = os.path.join(root, f)
        if os.path.lexists(p):
            os.unlink(p)
    shutil.rmtree(os.path.join(root, "mnt/pmbootstrap"), ignore_errors=True)

    # pmbootstrap copies the build host's resolv.conf into every chroot and leaves it there.
    # That's the host's router and network, nothing the tablet needs. Empty is what
    # pmbootstrap leaves on a host without one; resolv-conf-stub-link.service replaces it at boot.
    with open(os.path.join(root, "etc/resolv.conf"), "w"):
        pass

    # /home has empty mount points in it: pmbootstrap starts it again from /etc/skel
    home = os.path.join(root, "home")
    shutil.rmtree(home)
    os.mkdir(home, 0o755)
    home_user = os.path.join(home, job["user"])
    sh("cp", "-a", os.path.join(root, "etc/skel"), home_user)
    sh("chown", "-R", "10000:10000", home_user)

    # Local packages are signed with this build's key, so apk needs it to trust them
    for k in glob.glob(os.path.join(job["work"], "config_apk_keys", "*.pub")):
        sh("cp", k, os.path.join(root, "etc/apk/keys/"))
    os.makedirs(os.path.join(root, "var/cache/apk"), exist_ok=True)
    for f in glob.glob(os.path.join(job["work"], "cache_apk_aarch64", "APKINDEX.*.tar.gz")):
        sh("cp", f, os.path.join(root, "var/cache/apk/"))
    repos = os.path.join(root, "etc/apk/repositories")
    lines = [x for x in open(repos) if "/mnt/pmbootstrap/packages" not in x]
    with open(repos, "w") as f:
        f.writelines(lines)

    # Same thing cp -a into a mounted /boot gets
    b = os.path.join(root, "boot")
    os.mkdir(boot)
    shutil.copystat(b, boot)
    for n in os.listdir(b):
        os.rename(os.path.join(b, n), os.path.join(boot, n))
    return root, boot


def installed(root):
    """Package records from apk's database."""
    pkgs = []
    cur = {}
    for line in open(os.path.join(root, "lib/apk/db/installed"), errors="replace"):
        line = line.rstrip("\n")
        if not line:
            if cur:
                pkgs.append(cur)
            cur = {}
        elif len(line) > 2 and line[1] == ":" and line[0] in "PVCAoSI":
            cur[line[0]] = line[2:]
    if cur:
        pkgs.append(cur)
    return pkgs


def keep_apks(job, root):
    """Copy every installed apk next to the image and write the manifest.

    Columns: name version apk-checksum arch origin where file sha256. The checksum
    is apk's own (C: in the database); that is what compare looks at."""
    out = os.path.join(job["out"], "apks")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    rows = []
    missing = []
    work = job["work"]
    for p in sorted(installed(root), key=lambda x: x["P"]):
        base = f"{p['P']}-{p['V']}"
        local = glob.glob(os.path.join(work, "packages", "*", "aarch64", glob.escape(base) + ".apk"))
        mirror = glob.glob(os.path.join(work, "cache_apk_aarch64", glob.escape(base) + ".*.apk"))
        found = local or mirror
        if not found:
            # apk doesn't download a metapackage with no files
            meta = p.get("I") == "0"
            if not meta:
                missing.append(base)
            rows.append(f"{p['P']} {p['V']} {p.get('C', '-')} {p.get('A', '-')} {p.get('o', '-')} {'meta' if meta else 'missing'} - -")
            continue
        src = found[0]
        dst = os.path.join(out, os.path.basename(src))
        subprocess.run(["cp", "--reflink=auto", src, dst], check=True)
        rows.append(f"{p['P']} {p['V']} {p.get('C', '-')} {p.get('A', '-')} {p.get('o', '-')} "
                    f"{'local' if local else 'mirror'} {os.path.basename(src)} {sha256(dst)}")
    with open(os.path.join(job["out"], "manifest-apks.txt"), "w") as f:
        f.write("\n".join(rows) + "\n")
    print(f"apks: {len(rows)} installed, {len(missing)} not found in the caches", flush=True)
    for m in missing:
        print("not kept:", m, flush=True)


def xattrs(path):
    try:
        names = os.listxattr(path, follow_symlinks=False)
    except OSError:
        return ""
    out = []
    for n in sorted(names):
        try:
            v = os.getxattr(path, n, follow_symlinks=False)
            out.append(n + "=" + base64.b64encode(v).decode())
        except OSError:
            out.append(n + "=?")
    return ",".join(out)


def file_list(job, root, boot):
    """Every path with mode, owner, size and content hash, for compare."""
    rows = []
    for prefix, top in (("", root), ("/boot", boot)):
        for d, dirs, files in os.walk(top):
            dirs.sort()
            for n in [*dirs, *sorted(files)]:
                p = os.path.join(d, n)
                rel = prefix + "/" + os.path.relpath(p, top)
                st = os.lstat(p)
                if stat.S_ISREG(st.st_mode):
                    what = sha256(p)
                elif stat.S_ISLNK(st.st_mode):
                    what = "->" + os.readlink(p)
                elif stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode):
                    what = f"dev:{os.major(st.st_rdev)}:{os.minor(st.st_rdev)}"
                else:
                    what = "-"
                size = st.st_size if stat.S_ISREG(st.st_mode) else 0
                rows.append(f"{rel}\t{st.st_mode:o}\t{st.st_uid}\t{st.st_gid}\t{size}\t{what}\t{xattrs(p)}")
    path = os.path.join(job["out"], "rootfs-files.tsv")
    with open(path, "w", errors="surrogateescape") as f:
        f.write("\n".join(rows) + "\n")
    sh("zstd", "-q", "-f", "--rm", "-T4", path)
    print(f"files: {len(rows)}", flush=True)


def build_image(job, ids, root, boot, dest, root_mib):
    """GPT plus two filesystems in a regular file. Sector numbers are what parted
    gives pmbootstrap: "512M" is decimal and the next start aligns down to 1 MiB."""
    env = dict(os.environ, E2FSPROGS_FAKE_TIME=str(job["epoch"]))
    root_start = (job["boot_size_mb"] * 1000000 // SECTOR) // 2048 * 2048
    boot_start = 2048
    total_mib = job["boot_size_mb"] + root_mib
    total = total_mib * MIB // SECTOR
    root_end = ((total - 33) // 2048) * 2048
    table = (
        "label: gpt\n"
        f"label-id: {ids['uuid_disk']}\n"
        "unit: sectors\n"
        "first-lba: 34\n"
        "sector-size: 512\n"
        f'start={boot_start}, size={root_start - boot_start}, type={TYPE_BOOT}, uuid={ids["uuid_part_boot"]}, name="primary"\n'
        f'start={root_start}, size={root_end - root_start}, type={TYPE_ROOT}, uuid={ids["uuid_part_root"]}, name="primary"\n'
    )
    if os.path.exists(dest):
        os.unlink(dest)
    with open(dest, "wb") as f:
        f.truncate(total * SECTOR)
    sh("sfdisk", "-q", "--no-reread", "--no-tell-kernel", dest, input=table.encode())

    tmp = dest + ".fs"
    sh("mke2fs", "-t", "ext2", "-F", "-q", "-L", "pmOS_boot", "-U", ids["uuid_boot"],
       "-E", f"nodiscard,hash_seed={ids['uuid_boot']},root_owner=0:0", "-d", boot,
       tmp, f"{(root_start - boot_start) // 2}k", env=env)
    sh("e2fsck", "-fn", tmp, stdout=subprocess.DEVNULL)
    sh("dd", f"if={tmp}", f"of={dest}", "bs=1M", f"seek={boot_start * SECTOR // MIB}", "conv=notrunc,sparse", "status=none")

    os.unlink(tmp)
    sh("mke2fs", "-t", "ext4", "-F", "-q", "-L", "pmOS_root", "-i", "8192", "-U", ids["uuid_root"],
       "-E", f"nodiscard,lazy_itable_init=1,hash_seed={ids['uuid_root']},root_owner=0:0", "-d", root,
       tmp, f"{(root_end - root_start) // 2}k", env=env)
    sh("e2fsck", "-fn", tmp, stdout=subprocess.DEVNULL)
    sh("dd", f"if={tmp}", f"of={dest}", "bs=1M", f"seek={root_start * SECTOR // MIB}", "conv=notrunc,sparse", "status=none")
    os.unlink(tmp)
    sh("sfdisk", "-d", dest)


def replace_in_file(path, pairs):
    with open(path, "rb") as f:
        data = f.read()
    n = 0
    for a, b in pairs:
        if len(a) != len(b):
            sys.exit("UUIDs must have the same length")
        n += data.count(a.encode())
        data = data.replace(a.encode(), b.encode())
    with open(path, "wb") as f:
        f.write(data)
    return n


def main():
    job = json.load(open(sys.argv[1]))
    out = job["out"]
    ids = {k: job[k] for k in job if k.startswith("uuid_")}
    # pmbootstrap's get_subpartitions_size(), measured on the whole chroot
    du = subprocess.run(["du", "-ks", job["chroot"]], check=True, capture_output=True, text=True).stdout.split()[0]
    root_mib = round(int(du) / 1024 * 1.2 + 50)
    print(f"chroot {du} KiB, root partition {root_mib} MiB", flush=True)

    root, boot = stage_rootfs(job)
    keep_apks(job, root)
    file_list(job, root, boot)

    boot_img = os.path.join(boot, "boot.img")
    with open(boot_img, "rb") as f:
        head = f.read(8192)
    for k in ("uuid_boot", "uuid_root"):
        if ids[k].encode() not in head and ids[k].encode() not in open(boot_img, "rb").read():
            sys.exit(f"boot.img has no {k}: mkinitfs didn't take the UUIDs from fstab")
    shutil.copyfile(boot_img, os.path.join(out, "boot.img"))

    img = os.path.join(out, "userdata.img")
    build_image(job, ids, root, boot, img, root_mib)
    sh("img2simg", img, os.path.join(out, "userdata.simg"))
    sh("zstd", "-q", "-f", f"-{job['zstd_level']}", f"-T{job['jobs']}", os.path.join(out, "userdata.simg"))

    if job.get("sdtest"):
        sd = job["sdtest"]
        pairs = [(ids[k], sd[k]) for k in ("uuid_boot", "uuid_root")]
        print("sdtest: replaced", replace_in_file(boot_img, pairs), "UUIDs in boot.img", flush=True)
        replace_in_file(os.path.join(root, "etc/fstab"), pairs)
        d = os.path.join(out, "sdtest")
        os.makedirs(d, exist_ok=True)
        shutil.copyfile(boot_img, os.path.join(d, "boot-sd.img"))
        sd_img = os.path.join(d, "microsd.img")
        build_image(job, sd, root, boot, sd_img, root_mib)
        sh("zstd", "-q", "-f", "-3", f"-T{job['jobs']}", sd_img)

    shutil.rmtree(job["stage"])
    for d, dirs, files in os.walk(out):
        for n in [*dirs, *files]:
            p = os.path.join(d, n)
            os.chown(p, job["host_uid"], job["host_uid"], follow_symlinks=False)


if __name__ == "__main__":
    main()

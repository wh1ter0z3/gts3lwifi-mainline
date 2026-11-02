#!/usr/bin/env python3
"""Build the postmarketOS + Plasma preview image for the Galaxy Tab S3.

    scripts/build-preview.py all --run run1            everything, from the lock file
    scripts/build-preview.py STEP [STEP...] --run run1 one step at a time
    scripts/build-preview.py compare RUN_A RUN_B       what differs between two runs
    scripts/build-preview.py pin [--fork REV] [--pmaports SHA] [--pmbootstrap SHA]

Steps, in order: prepare builder packages rootfs initfs pack lk2nd audit release

Everything comes from scripts/preview/lock.json. Output goes to $PREVIEW_OUT
(default ./out/preview), which must not be in git:

    <out>/<run>/work      pmbootstrap's work dir (separate per run, nothing shared)
    <out>/<run>/src       pmbootstrap and pmaports at the pinned commits, overlaid
    <out>/<run>/image     images, the kept apks, per-file hashes
    <out>/<run>/logs      one log per step
    <out>/release/<name>-<run>   what testers get

Why not `pmbootstrap install` all the way: it needs losetup and mount as root.
This runs in a rootless podman container, where those don't work, so pmbootstrap
stops after filling the rootfs (--no-image) and scripts/preview/pack-image.py does
the rest of install_system_image() with mke2fs -d: same GPT, same partition sizes,
same mkfs options, checked against an image pmbootstrap made on a real root.

What "reproducible" means here: pmbootstrap, pmaports, our packages and the kernel
tarball are pinned. Alpine and postmarketOS edge are not: their mirrors only hold
the latest packages, so a later run gets whatever is current. That can't be pinned.
A run therefore keeps every apk it installed (image/apks), writes a manifest of
name, version and apk checksum, and compares it with the manifest committed next
to the lock file: any difference is printed and recorded in BUILDINFO.json as
drift. It is not an error unless --no-drift is given.

To take in later work, such as the touch keys or the headphone jack once they pass
on hardware: merge it onto this branch, then `pin --fork <that commit>` (it reads the
kernel's version and hash back from the APKBUILD), rebuild with `all`, and commit the
lock file and the new manifest. Only a new package directory needs a hand edit: add it
to fork.build and fork.overlay in the lock file.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PREVIEW = os.path.join(HERE, "preview")
LOCK = os.path.join(PREVIEW, "lock.json")
STEPS = "prepare builder packages rootfs initfs pack lk2nd audit release".split()
CONTAINER_WORK = "/home/pmos/.local/var/pmbootstrap"


def die(msg):
    sys.exit("build-preview: " + msg)


def run(cmd, log=None, check=True, **kw):
    print("+", " ".join(cmd)[:300], flush=True)
    if log:
        with open(log, "ab") as f:
            f.write(("\n$ " + " ".join(cmd) + "\n").encode())
            f.flush()
            r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, **kw)
    else:
        r = subprocess.run(cmd, **kw)
    if check and r.returncode:
        die(f"exit {r.returncode}: {' '.join(cmd)[:200]}" + (f" (log: {log})" if log else ""))
    return r


def git(*a, cwd=REPO):
    return subprocess.run(["git", *a], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


class Ctx:
    def __init__(self, a):
        self.a = a
        self.lock = json.load(open(a.lock))
        self.out = os.path.abspath(a.out or os.environ.get("PREVIEW_OUT") or os.path.join(REPO, "out", "preview"))
        self.run = a.run
        self.root = os.path.join(self.out, a.run)
        self.work = os.path.join(self.root, "work")
        self.config = os.path.join(self.root, "config")
        self.src = os.path.join(self.root, "src")
        self.image = os.path.join(self.root, "image")
        self.logs = os.path.join(self.root, "logs")
        self.aports = os.path.join(self.work, "cache_git", "pmaports")
        self.pmb = os.path.join(self.src, "pmbootstrap")
        self.tag = "localhost/gts3-preview-builder:" + hashlib.sha256(
            open(os.path.join(PREVIEW, "Containerfile"), "rb").read()).hexdigest()[:12]
        self.name = self.lock["release"]["name"]
        self.device = self.lock["image"]["device"]
        for d in (self.work, self.config, self.src, self.image, self.logs):
            os.makedirs(d, exist_ok=True)

    def log(self, step):
        return os.path.join(self.logs, step + ".log")

    def pod(self, *cmd, user=None, log=None, check=True, input=None, extra=()):
        c = ["podman", "run", "--rm", "-i", "--privileged", "--userns=keep-id", "--pull=never", "--hostname", "gts3-preview-build",
             "-e", "PYTHONDONTWRITEBYTECODE=1", "-e", "SOURCE_DATE_EPOCH=" + str(self.lock["image"]["epoch"]),
             "-v", f"{self.work}:{CONTAINER_WORK}:Z",
             "-v", f"{self.config}:/home/pmos/.config:Z",
             "-v", f"{self.pmb}:/opt/pmbootstrap:ro,Z",
             "-v", f"{PREVIEW}:/scripts:ro,Z",
             "-v", f"{self.image}:/image:Z", *extra]
        if user:
            c += ["--user", user]
        c += [self.tag, *cmd]
        return run(c, log=log, check=check, input=input)

    def pmbootstrap(self, *args, log=None, check=True):
        return self.pod("python3", "/opt/pmbootstrap/pmbootstrap.py", "--details-to-stdout", "-y", *args,
                        log=log, check=check)


def clone_at(url, commit, dest, branch):
    """A shallow clone at exactly one commit, with origin/<branch> pointing at it,
    which pmbootstrap needs to read channels.cfg."""
    if os.path.isdir(os.path.join(dest, ".git")):
        if git("rev-parse", "HEAD", cwd=dest) == commit:
            return
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)
    run(["git", "init", "-q", "-b", branch, dest])
    run(["git", "-C", dest, "remote", "add", "origin", url])
    run(["git", "-C", dest, "fetch", "-q", "--depth=1", "origin", commit])
    run(["git", "-C", dest, "update-ref", f"refs/remotes/origin/{branch}", commit])
    run(["git", "-C", dest, "checkout", "-q", "-B", branch, commit])


def export_fork(ctx):
    """Our package directories, from git at the pinned commit (never the working
    tree, unless --unpinned)."""
    dest = os.path.join(ctx.src, "fork")
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    if ctx.a.unpinned:
        shutil.copytree(os.path.join(REPO, "pmaports"), os.path.join(dest, "pmaports"))
        return dest, "unpinned working tree", None
    commit = ctx.lock["fork"]["commit"]
    tree = git("rev-parse", f"{commit}:pmaports")
    if tree != ctx.lock["fork"]["pmaports_tree"]:
        die(f"{commit}:pmaports is {tree}, the lock says {ctx.lock['fork']['pmaports_tree']}")
    p = subprocess.Popen(["git", "archive", commit, "pmaports"], cwd=REPO, stdout=subprocess.PIPE)
    run(["tar", "-x", "-C", dest], stdin=p.stdout)
    if p.wait():
        die("git archive failed")
    return dest, commit, tree


def overlay(ctx, fork):
    """Put our packages over the pinned pmaports, where tools/overlay.py put them
    for every build so far."""
    pm = os.path.join(fork, "pmaports")
    for name, dest in ctx.lock["fork"]["overlay"].items():
        d = os.path.join(ctx.aports, dest)
        shutil.rmtree(d, ignore_errors=True)
        shutil.copytree(os.path.join(pm, name), d, symlinks=True)
        pmaport = os.path.join(d, "APKBUILD.pmaports")
        if os.path.exists(pmaport):
            os.replace(pmaport, os.path.join(d, "APKBUILD"))
            diff = os.path.join(d, "APKBUILD.diff")
            if os.path.exists(diff):
                os.unlink(diff)
    env = dict(os.environ, GIT_AUTHOR_NAME="preview", GIT_AUTHOR_EMAIL="preview@localhost",
               GIT_COMMITTER_NAME="preview", GIT_COMMITTER_EMAIL="preview@localhost",
               GIT_AUTHOR_DATE="@%d +0000" % ctx.lock["image"]["epoch"],
               GIT_COMMITTER_DATE="@%d +0000" % ctx.lock["image"]["epoch"])
    run(["git", "-C", ctx.aports, "add", "-A"])
    subprocess.run(["git", "-C", ctx.aports, "commit", "-q", "-m", "preview: overlay"], env=env,
                   check=False, stdout=subprocess.DEVNULL)


def step_prepare(ctx):
    a = ctx.a
    lock = ctx.lock
    clone_at(lock["pmbootstrap"]["url"], lock["pmbootstrap"]["commit"], ctx.pmb, "main")
    os.makedirs(os.path.dirname(ctx.aports), exist_ok=True)
    clone_at(lock["pmaports"]["url"], lock["pmaports"]["commit"], ctx.aports, "main")
    fork, commit, tree = export_fork(ctx)
    overlay(ctx, fork)
    kver = re.search(r"^pkgver=(\S+)", open(os.path.join(ctx.aports, "device/testing/linux-postmarketos-qcom-msm8996/APKBUILD")).read(), re.M).group(1)
    k = lock["kernel"]
    if kver != k["version"]:
        die(f"kernel package is {kver}, the lock says {k['version']}")
    apkbuild = open(os.path.join(ctx.aports, "device/testing/linux-postmarketos-qcom-msm8996/APKBUILD")).read()
    m = re.search(r"^([0-9a-f]{128})  linux-" + re.escape(kver) + r"\.tar\.xz$", apkbuild, re.M)
    if not m or m.group(1) != k["sha512"]:
        die("the kernel tarball's sha512 in APKBUILD differs from the lock")
    # Config: what pmbootstrap init would ask. ssh_keys stays off so nothing from
    # this host lands in the image.
    i = lock["image"]
    cfg = f"""[pmbootstrap]
device = {i['device']}
is_default_channel = False
ui = {i['ui']}
jobs = {lock.get('jobs', 16)}
ssh_keys = False
user = {i['user']}
hostname = {i['hostname']}
timezone = {i['timezone']}
locale = {i['locale']}
boot_size = {i['boot_size_mb']}

[providers]
"""
    for k2, v in i["providers"].items():
        cfg += f"{k2} = {v}\n"
    cfg += "\n[mirrors]\n"
    open(os.path.join(ctx.config, "pmbootstrap_v3.cfg"), "w").write(cfg)
    # pmbootstrap init writes this; without it, any existing work dir is refused
    pmbconfig = open(os.path.join(ctx.pmb, "pmb/config/__init__.py")).read()
    open(os.path.join(ctx.work, "version"), "w").write(re.search(r"^work_version = (\d+)", pmbconfig, re.M).group(1) + "\n")
    if a.seed_distfiles:
        d = os.path.join(ctx.work, "cache_distfiles")
        os.makedirs(d, exist_ok=True)
        for f in os.listdir(a.seed_distfiles):
            s = os.path.join(a.seed_distfiles, f)
            if os.path.isfile(s) and not os.path.exists(os.path.join(d, f)):
                shutil.copyfile(s, os.path.join(d, f))


def step_builder(ctx):
    run(["podman", "build", "-q", "-t", ctx.tag, PREVIEW], log=ctx.log("builder"))


def step_packages(ctx):
    pkgs = ctx.lock["fork"]["build"]
    ctx.pmbootstrap("build", "--force", "--arch", "aarch64", *pkgs, log=ctx.log("packages"))
    ctx.pmbootstrap("shutdown", log=ctx.log("packages"))


def step_rootfs(ctx):
    i = ctx.lock["image"]
    ctx.pmbootstrap("install", "--zap", "--no-image", "--no-sshd", "--password", i["default_password"],
                    "--add", ",".join(i["add"]), "--filesystem", "ext4",
                    log=ctx.log("rootfs"))
    ctx.pmbootstrap("shutdown", log=ctx.log("rootfs"))


def step_initfs(ctx):
    """pmbootstrap's next steps after the rootfs: fstab, then mkinitfs, which reads
    the UUIDs from fstab into the boot image's command line. Also records the hash
    of the published default password for gts3-preview-password."""
    i = ctx.lock["image"]
    script = (
        f"printf '%s\\n' '# <file system> <mount point> <type> <options> <dump> <pass>' "
        f"'UUID={i['uuid_root']} / ext4 defaults 0 0' "
        f"'UUID={i['uuid_boot']} /boot ext2 nodev,nosuid,noexec 0 0' > /etc/fstab && "
        "mkdir -p /etc/gts3-preview && "
        "awk -F: '$1 == \"" + i["user"] + "\" { print $2 }' /etc/shadow > /etc/gts3-preview/default-password-hash && "
        "chmod 700 /etc/gts3-preview && chmod 600 /etc/gts3-preview/default-password-hash && "
        "mkinitfs"
    )
    ctx.pmbootstrap("chroot", "-r", "--output", "stdout", "--", "sh", "-c", script, log=ctx.log("initfs"))
    ctx.pmbootstrap("shutdown", log=ctx.log("initfs"))


def step_pack(ctx):
    i = ctx.lock["image"]
    job = {
        "chroot": f"{CONTAINER_WORK}/chroot_rootfs_{ctx.device}",
        "work": CONTAINER_WORK,
        "out": "/image",
        "stage": "/image/stage",
        "name": ctx.name,
        "user": i["user"],
        "epoch": i["epoch"],
        "boot_size_mb": i["boot_size_mb"],
        "uuid_boot": i["uuid_boot"],
        "uuid_root": i["uuid_root"],
        "uuid_disk": i["uuid_disk"],
        "uuid_part_boot": i["uuid_part_boot"],
        "uuid_part_root": i["uuid_part_root"],
        "sdtest": i.get("sdtest"),
        "zstd_level": i.get("zstd_level", 15),
        "jobs": ctx.lock.get("jobs", 16),
        "host_uid": 1000,
    }
    with open(os.path.join(ctx.image, "pack-job.json"), "w") as f:
        json.dump(job, f, indent=1)
    ctx.pod("python3", "-I", "/scripts/pack-image.py", "/image/pack-job.json", user="root", log=ctx.log("pack"))


def step_lk2nd(ctx):
    """Build or copy the lk2nd images named in the lock. Donor boot images aren't
    ours to publish, so they're arguments; without them the step checks what's there."""
    run([sys.executable, "-I", os.path.join(PREVIEW, "lk2nd-images.py"),
         "--lock", ctx.a.lock, "--out", os.path.join(ctx.out, "lk2nd"),
         *(["--t820-donor", ctx.a.t820_donor] if ctx.a.t820_donor else []),
         *(["--t825-donor", ctx.a.t825_donor] if ctx.a.t825_donor else []),
         *(["--t820-image", ctx.a.t820_image] if ctx.a.t820_image else [])],
        log=ctx.log("lk2nd"))


def step_audit(ctx):
    """Read the finished image. Secret and key files go in read-only; the script says
    found or not found for what's in them, and nothing else."""
    vols, args = [], []
    a = ctx.a
    for flag, paths in (("secret", a.secret), ("key", a.key), ("terms", [a.terms_file] if a.terms_file else [])):
        for i, f in enumerate(paths or []):
            inside = f"/audit/{flag}{i}-{os.path.basename(f)}"
            vols += ["-v", f"{os.path.abspath(f)}:{inside}:ro"]
            args += [f"--{flag}-file", inside]
    with open(os.path.join(ctx.image, "lock.json"), "w") as f:
        json.dump(ctx.lock, f, indent=1)
    r = ctx.pod("python3", "-I", "/scripts/audit-image.py", "/image", "--lock", "/image/lock.json", *args,
                user="root", log=ctx.log("audit"), check=False, extra=vols)
    report = os.path.join(ctx.image, "AUDIT.txt")
    if os.path.exists(report):
        print("".join(open(report).readlines()[:80]))
    if r.returncode:
        print(f"audit: exit {r.returncode}; see {report} and {ctx.log('audit')}", flush=True)


def read_manifest(path):
    m = {}
    for line in open(path):
        p = line.split()
        if len(p) >= 3:
            m[p[0]] = (p[1], p[2])
    return m


def diff_manifests(a, b):
    out = []
    for k in sorted(set(a) | set(b)):
        if k not in a:
            out.append(f"+ {k} {b[k][0]}")
        elif k not in b:
            out.append(f"- {k} {a[k][0]}")
        elif a[k][0] != b[k][0]:
            out.append(f"~ {k} {a[k][0]} -> {b[k][0]}")
        elif a[k][1] != b[k][1]:
            out.append(f"= {k} {a[k][0]} checksum differs")
    return out


def step_release(ctx):
    rel = os.path.join(ctx.out, "release", f"{ctx.name}-{ctx.run}")
    shutil.rmtree(rel, ignore_errors=True)
    os.makedirs(rel)
    n = ctx.name
    copies = {
        f"{n}-userdata.simg.zst": os.path.join(ctx.image, "userdata.simg.zst"),
        f"{n}-boot.img": os.path.join(ctx.image, "boot.img"),
        "manifest-apks.txt": os.path.join(ctx.image, "manifest-apks.txt"),
    }
    lk = os.path.join(ctx.out, "lk2nd")
    for f in ("lk2nd-gts3lwifi.img", "lk2nd-gts3lwifi-odin.tar", "lk2nd-gts3llte.img", "lk2nd-gts3llte-odin.tar"):
        if os.path.exists(os.path.join(lk, f)):
            copies[f] = os.path.join(lk, f)
        else:
            print(f"release: {f} is missing", flush=True)
    for k, v in copies.items():
        if not os.path.exists(v):
            die(f"missing {v}")
        run(["cp", "--reflink=auto", v, os.path.join(rel, k)])
    drift = []
    committed = os.path.join(PREVIEW, "manifest-apks.txt")
    if os.path.exists(committed):
        drift = diff_manifests(read_manifest(committed), read_manifest(os.path.join(rel, "manifest-apks.txt")))
        if drift:
            print("DRIFT against the committed manifest:", *drift, sep="\n", flush=True)
    info = {
        "name": n,
        "run": ctx.run,
        "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "unpinned": bool(ctx.a.unpinned),
        "lock": ctx.lock,
        "source_tree_head": git("rev-parse", "HEAD"),
        "drift_against_committed_manifest": drift,
        "builder_image": subprocess.run(["podman", "image", "inspect", ctx.tag, "--format", "{{.Id}}"],
                                        capture_output=True, text=True).stdout.strip(),
    }
    with open(os.path.join(rel, "BUILDINFO.json"), "w") as f:
        json.dump(info, f, indent=1)
    names = sorted(os.listdir(rel))
    with open(os.path.join(rel, "SHA256SUMS"), "w") as f:
        for x in names:
            f.write(f"{sha256(os.path.join(rel, x))}  {x}\n")
    print(open(os.path.join(rel, "SHA256SUMS")).read())
    run(["ls", "-la", rel])
    if drift and ctx.a.no_drift:
        die("drift against the committed manifest, and --no-drift is set")


def cmd_compare(a):
    """What differs between two runs: apks by checksum, files by hash."""
    out = os.path.abspath(a.out or os.environ.get("PREVIEW_OUT") or os.path.join(REPO, "out", "preview"))
    x, y = (os.path.join(out, r, "image") for r in (a.run_a, a.run_b))
    print(f"=== apks: {a.run_a} -> {a.run_b}")
    d = diff_manifests(read_manifest(os.path.join(x, "manifest-apks.txt")), read_manifest(os.path.join(y, "manifest-apks.txt")))
    print("\n".join(d) or "identical")
    def files(p):
        o = subprocess.run(["zstd", "-dc", os.path.join(p, "rootfs-files.tsv.zst")], check=True, capture_output=True, text=True).stdout
        r = {}
        for line in o.splitlines():
            f = line.split("\t")
            r[f[0]] = f[1:]
        return r
    fx, fy = files(x), files(y)
    only_x = sorted(set(fx) - set(fy))
    only_y = sorted(set(fy) - set(fx))
    changed = sorted(k for k in set(fx) & set(fy) if fx[k] != fy[k])
    print(f"=== files: {len(fx)} and {len(fy)}; only in {a.run_a}: {len(only_x)}; only in {a.run_b}: {len(only_y)}; different: {len(changed)}")
    for k in only_x[:50]:
        print("-", k)
    for k in only_y[:50]:
        print("+", k)
    groups = {}
    for k in changed:
        key = k.split("/")[1] + "/" + (k.split("/")[2] if k.count("/") > 1 else "")
        groups.setdefault(key, []).append(k)
    for g, v in sorted(groups.items(), key=lambda t: -len(t[1])):
        print(f"~ {g}: {len(v)} files, e.g. {v[0]}")
    for k in changed[:40]:
        print("~", k, "fields:", [i for i, (p, q) in enumerate(zip(fx[k], fy[k])) if p != q])
    print("=== images")
    for f in ("boot.img", "userdata.img", "userdata.simg.zst"):
        px, py = os.path.join(x, f), os.path.join(y, f)
        if os.path.exists(px) and os.path.exists(py):
            print(f, "same" if sha256(px) == sha256(py) else "differ", os.path.getsize(px), os.path.getsize(py))


def cmd_pin(a):
    """Point the lock file at other commits. For the fork: any commit in this repo;
    the kernel's version and hash are read back from its APKBUILD."""
    lock = json.load(open(a.lock))
    if a.fork:
        c = git("rev-parse", a.fork + "^{commit}")
        lock["fork"]["commit"] = c
        lock["fork"]["pmaports_tree"] = git("rev-parse", f"{c}:pmaports")
        ab = git("show", f"{c}:pmaports/linux-postmarketos-qcom-msm8996/APKBUILD.pmaports")
        v = re.search(r"^pkgver=(\S+)", ab, re.M).group(1)
        h = re.search(r"^([0-9a-f]{128})  linux-" + re.escape(v) + r"\.tar\.xz$", ab, re.M).group(1)
        lock["kernel"].update({"version": v, "sha512": h,
                               "url": f"https://cdn.kernel.org/pub/linux/kernel/v{v.split('.')[0]}.x/linux-{v}.tar.xz"})
    if a.pmaports:
        lock["pmaports"]["commit"] = a.pmaports
    if a.pmbootstrap:
        lock["pmbootstrap"]["commit"] = a.pmbootstrap
    with open(a.lock, "w") as f:
        json.dump(lock, f, indent=1)
        f.write("\n")
    print(json.dumps({k: lock[k] for k in ("fork", "kernel", "pmaports", "pmbootstrap")}, indent=1))
    print("Rebuild, then copy image/manifest-apks.txt over scripts/preview/manifest-apks.txt if the changes are wanted.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="+", help="all, STEP..., compare RUN_A RUN_B, or pin")
    ap.add_argument("--run", default="run1")
    ap.add_argument("--lock", default=LOCK)
    ap.add_argument("--out")
    ap.add_argument("--unpinned", action="store_true", help="use pmaports/ from the working tree; recorded in BUILDINFO.json")
    ap.add_argument("--no-drift", action="store_true", help="fail if the apks differ from the committed manifest")
    ap.add_argument("--seed-distfiles", help="copy source tarballs from this directory first (abuild still checks hashes)")
    ap.add_argument("--t820-donor", help="boot image with the SM-T820's stock device trees")
    ap.add_argument("--t825-donor", help="boot image with the SM-T825's stock device trees")
    ap.add_argument("--t820-image", help="an existing lk2nd image for the SM-T820, checked against the lock's hash")
    ap.add_argument("--secret-file", dest="secret", action="append", help="audit: values in this file must not be in the image; never printed")
    ap.add_argument("--key-file", dest="key", action="append", help="audit: SSH public key that must not be in the image")
    ap.add_argument("--terms-file", help="audit: local list of words that must not be in the image, kept out of git")
    ap.add_argument("--fork", help="pin: fork commit or ref")
    ap.add_argument("--pmaports", help="pin: pmaports commit")
    ap.add_argument("--pmbootstrap", help="pin: pmbootstrap commit")
    a = ap.parse_args()
    if a.steps[0] == "compare":
        if len(a.steps) != 3:
            die("compare RUN_A RUN_B")
        a.run_a, a.run_b = a.steps[1:]
        return cmd_compare(a)
    if a.steps[0] == "pin":
        return cmd_pin(a)
    steps = STEPS if a.steps == ["all"] else a.steps
    for s in steps:
        if s not in STEPS:
            die(f"unknown step {s}; steps: {' '.join(STEPS)}")
    ctx = Ctx(a)
    for s in steps:
        t = time.time()
        print(f"=== {s}", flush=True)
        globals()["step_" + s](ctx)
        print(f"=== {s}: {time.time() - t:.0f} s", flush=True)


if __name__ == "__main__":
    main()

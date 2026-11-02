#!/usr/bin/env python3
"""Read the finished userdata image, not the build tree, and say what is in it.

Runs as root in the builder container (no loop devices, no mount): the two
filesystems are cut out of the image and unpacked with debugfs rdump. Writes
AUDIT.txt next to the image; the first lines are the checks that decide pass or
fail, the rest is facts for a person to read.

Values from --secret-file are compared and only ever reported as found or not
found. --terms-file is a local list of words that must not be in the image for
reasons of our own: it stays out of git. Each term is looked for in path names and
in the contents of files; hits in files that belong to a package are counted apart,
because they are somebody's stock data and not our state.
"""
import argparse
import gzip
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import zlib

OUT = []
FAILS = []


def say(*a):
    OUT.append(" ".join(str(x) for x in a))


def check(name, ok, detail=""):
    OUT.append(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail else ""))
    if not ok:
        FAILS.append(name)


def sh(*cmd, **kw):
    return subprocess.run([str(c) for c in cmd], check=True, capture_output=True, **kw).stdout


def read(root, rel):
    try:
        with open(os.path.join(root, rel), errors="replace") as f:
            return f.read()
    except OSError:
        return None


def unpack(img, work):
    table = json.loads(sh("sfdisk", "-J", img))["partitiontable"]
    parts = table["partitions"]
    if len(parts) != 2:
        sys.exit("expected two partitions")
    trees = []
    for p, name in zip(parts, ("boot", "root")):
        fs = os.path.join(work, name + ".fs")
        sh("dd", f"if={img}", f"of={fs}", "bs=1M", f"skip={p['start'] // 2048}", f"count={p['size'] // 2048}", "conv=sparse", "status=none")
        tree = os.path.join(work, name)
        os.makedirs(tree)
        subprocess.run(["debugfs", "-R", f"rdump / {tree}", fs], check=True, capture_output=True)
        trees.append((tree, fs, p))
    return trees, table


def fdt_props(blob):
    """Properties of a device tree's root node."""
    magic, total, off_struct, off_str = struct.unpack_from(">IIII", blob)
    if magic != 0xd00dfeed:
        return {}
    pos = off_struct
    props = {}
    depth = 0
    while True:
        tok = struct.unpack_from(">I", blob, pos)[0]
        pos += 4
        if tok == 1:
            depth += 1
            if depth > 1:
                return props
            while blob[pos]:
                pos += 1
            pos = (pos + 4) & ~3
        elif tok == 2:
            return props
        elif tok == 3:
            n, name = struct.unpack_from(">II", blob, pos)
            pos += 8
            key = blob[off_str + name:blob.index(b"\0", off_str + name)].decode()
            props[key] = blob[pos:pos + n]
            pos = (pos + n + 3) & ~3
        elif tok == 9:
            return props


def boot_image(path):
    with open(path, "rb") as f:
        b = f.read()
    if b[:8] != b"ANDROID!":
        return None
    ks, _, rs, _, _, _, _, ps = struct.unpack_from("<8I", b, 8)
    cmdline = (b[64:576].split(b"\0")[0] + b[608:1632].split(b"\0")[0]).decode()
    kernel = b[ps:ps + ks]
    info = {"cmdline": cmdline, "kernel": ks, "ramdisk": rs, "page": ps, "dtbs": [], "version": None}
    tail = kernel
    linux = b""
    if kernel[:2] == b"\x1f\x8b":
        d = zlib.decompressobj(31)
        linux = d.decompress(kernel)
        tail = d.unused_data
    m = re.search(rb"Linux version [ -~]+", linux)
    if m:
        info["version"] = m.group(0).decode()
    for m in re.finditer(b"\xd0\x0d\xfe\xed", tail):
        total = struct.unpack_from(">I", tail, m.start() + 4)[0]
        p = fdt_props(tail[m.start():m.start() + total])
        cells = lambda k: list(struct.unpack(f">{len(p.get(k, b'')) // 4}I", p.get(k, b"")))
        msm, board = cells("qcom,msm-id"), cells("qcom,board-id")
        info["dtbs"].append({
            "model": p.get("model", b"").rstrip(b"\0").decode(errors="replace"),
            "msm": [(msm[i], msm[i + 1]) for i in range(0, len(msm) - 1, 2)],
            "board": [(board[i], board[i + 1]) for i in range(0, len(board) - 1, 2)],
        })
    return info


def lk2nd_pick(dtbs, msm_id, soc_rev, hw, subtype):
    """What lk2nd's platform_dt_absolute_match keeps: same msm-id, same hardware id and
    subtype, soc revision not newer than the board's."""
    return [d["model"] for d in dtbs
            if any(m == msm_id and r <= soc_rev for m, r in d["msm"]) and any(h == hw and s == subtype for h, s in d["board"])]


def owned_paths(root):
    """Path to package name, for every file that apk's database lists."""
    owned = {}
    cur = pkg = ""
    for line in (read(root, "lib/apk/db/installed") or "").splitlines():
        if line.startswith("P:"):
            pkg = line[2:]
        elif line.startswith("F:"):
            cur = line[2:]
        elif line.startswith("R:"):
            # The database has the paths packages ship; /lib, /bin and /sbin are links here
            rel = (cur + "/" + line[2:]).lstrip("/")
            rel = re.sub(r"^(lib|lib64)/", "usr/lib/", rel)
            rel = re.sub(r"^(bin|sbin|usr/sbin)/", "usr/bin/", rel)
            owned[rel] = pkg
    return owned


def walk(tree):
    for d, dirs, files in os.walk(tree):
        for n in [*files, *(x for x in dirs if os.path.islink(os.path.join(d, x)))]:
            yield os.path.join(d, n), os.path.relpath(os.path.join(d, n), tree)


def wants(root):
    out = {}
    for base in ("etc/systemd/system", "usr/lib/systemd/system"):
        top = os.path.join(root, base)
        if not os.path.isdir(top):
            continue
        for n in sorted(os.listdir(top)):
            if n.endswith((".wants", ".requires")) and os.path.isdir(os.path.join(top, n)):
                for u in sorted(os.listdir(os.path.join(top, n))):
                    out.setdefault(u, []).append(f"{base}/{n}")
    return out


def presets(root):
    rows = []
    d = os.path.join(root, "usr/lib/systemd/system-preset")
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        for line in open(os.path.join(d, n), errors="replace"):
            line = line.strip()
            if line and not line.startswith("#"):
                rows.append((n, line))
    return rows


def preset_state(rows, unit):
    """systemd's rule: the first matching line, in file-name order, wins."""
    for n, line in rows:
        verb, _, pat = line.partition(" ")
        pat = pat.split()[0] if pat else ""
        if re.fullmatch(re.escape(pat).replace(r"\*", ".*"), unit):
            return verb, n
    return None, None


def check_ssh(root, lock):
    w = wants(root)
    rows = presets(root)
    verb, fn = preset_state(rows, "sshd.service")
    check("sshd not enabled", "sshd.service" not in w, ", ".join(w.get("sshd.service", [])))
    check("sshd preset is disable", verb == "disable", f"{verb} from {fn}")
    keys = [p for _, p in walk(os.path.join(root, "etc/ssh")) if p.startswith("ssh_host")] if os.path.isdir(os.path.join(root, "etc/ssh")) else []
    check("no ssh host keys in the image", not keys, " ".join(keys))
    ak = [p for _, p in walk(root) if os.path.basename(p) in ("authorized_keys", "authorized_keys2")]
    check("no authorized_keys anywhere", not ak, " ".join(ak))


def check_login(root, lock):
    user = lock["image"]["user"]
    texts = {}
    for rel in ("etc/plasmalogin.conf", "usr/lib/plasmalogin.conf", "etc/sddm.conf"):
        t = read(root, rel)
        if t is not None:
            texts[rel] = t
    for base in ("etc/plasmalogin.conf.d", "usr/lib/plasmalogin.conf.d", "etc/sddm.conf.d", "usr/lib/sddm/sddm.conf.d"):
        d = os.path.join(root, base)
        for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            texts[f"{base}/{n}"] = read(root, f"{base}/{n}") or ""
    auto = []
    for rel, t in texts.items():
        sect = ""
        for line in t.splitlines():
            line = line.strip()
            if line.startswith("["):
                sect = line
            elif sect.lower() == "[autologin]" and re.match(r"(User|Session)\s*=\s*\S", line):
                auto.append(f"{rel}: {line}")
    check("no autologin configured", not auto, "; ".join(auto))
    say("login manager configuration files:", ", ".join(texts) or "none")
    for rel, t in texts.items():
        say(f"--- {rel}")
        say(t.strip())

    shadow = read(root, "etc/shadow") or ""
    rows = {x.split(":")[0]: x.split(":") for x in shadow.splitlines() if x}
    u = rows.get(user)
    check(f"user {user} exists with a password", bool(u and u[1] and u[1][0] == "$"))
    if u:
        say(f"{user}: hash type {u[1].split('$')[1] if '$' in u[1] else '?'}, last change field {u[2]!r}, max {u[4]!r}")
    root_row = rows.get("root")
    check("root is locked", bool(root_row and root_row[1][:1] in ("!", "*")))
    others = [k for k, v in rows.items() if k not in (user, "root") and v[1][:1] == "$"]
    check("no other account has a password", not others, " ".join(others))
    recorded = (read(root, "etc/gts3-preview/default-password-hash") or "").strip()
    check("default-password hash recorded and matches shadow", bool(u) and recorded == u[1])
    if recorded:
        st = os.lstat(os.path.join(root, "etc/gts3-preview/default-password-hash"))
        check("recorded hash is root-only", stat.S_IMODE(st.st_mode) == 0o600 and st.st_uid == 0, f"{stat.S_IMODE(st.st_mode):o} uid {st.st_uid}")
    group = read(root, "etc/group") or ""
    groups = [x.split(":")[0] for x in group.splitlines() if user in x.split(":")[-1].split(",")]
    say(f"{user} groups:", " ".join(groups))
    check(f"{user} is not in the autologin group", "autologin" not in groups)
    return u


def check_listeners(root):
    w = wants(root)
    say("")
    say("== enabled units (symlinks in the image)")
    for u, where in sorted(w.items()):
        say(f"{u}  <- {', '.join(where)}")
    say("")
    rows = presets(root)
    say("== preset files that apply on first boot (systemctl preset-all)")
    seen = sorted({n for n, _ in rows})
    say(", ".join(seen))
    say("")
    say("== units with sockets, by preset: enabled ones can listen")
    d = os.path.join(root, "usr/lib/systemd/system")
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if n.endswith(".socket"):
            verb, fn = preset_state(rows, n)
            if verb == "enable" or n in w:
                t = read(root, f"usr/lib/systemd/system/{n}") or ""
                say(f"{n}: preset {verb}, {'linked' if n in w else 'not linked yet'}, {' '.join(re.findall(r'^Listen\w+=\S.*', t, re.M))}")
    say("")
    say("== services known to open network ports")
    for u in ("sshd.service", "avahi-daemon.service", "avahi-daemon.socket", "cups.service", "cups.socket", "cups.path",
              "cups-browsed.service", "unudhcpd@usb0.service", "kdeconnectd.service", "telnet.socket", "NetworkManager.service",
              "dnsmasq.service", "systemd-resolved.service", "bluetooth.service", "wsdd.service", "rpcbind.service"):
        verb, fn = preset_state(rows, u)
        say(f"{u}: preset {verb} ({fn}), {'linked' if u in w else 'not linked'}")
    say("")
    say("== nftables rules that accept something")
    for base in ("etc/nftables.nft", "etc/nftables.conf"):
        t = read(root, base)
        if t:
            say(f"--- {base}")
            say(t.strip()[:2000])
    d = os.path.join(root, "etc/nftables.d")
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        t = read(root, f"etc/nftables.d/{n}") or ""
        keep = [x.strip() for x in t.splitlines() if "accept" in x or "dport" in x]
        say(f"--- etc/nftables.d/{n}")
        say("\n".join(keep) or "(nothing accepted)")


def check_network(root):
    d = os.path.join(root, "etc/NetworkManager/system-connections")
    conns = os.listdir(d) if os.path.isdir(d) else []
    check("no NetworkManager connection profiles", not conns, f"{len(conns)} found")
    for rel in ("var/lib/iwd", "etc/wpa_supplicant", "var/lib/NetworkManager"):
        d = os.path.join(root, rel)
        names = [p for _, p in walk(d)] if os.path.isdir(d) else []
        say(f"{rel}: {len(names)} files", " ".join(names)[:300])
    wifi = [p for _, p in walk(root) if p.endswith((".psk", ".nmconnection")) or re.match(r"etc/wpa_supplicant/.*\.conf$", p)]
    check("no Wi-Fi profiles anywhere", not wifi, " ".join(wifi[:10]))
    prox = []
    for top in ("etc", "home", "root", "var/lib", "usr/local"):
        t = os.path.join(root, top)
        for full, rel in walk(t) if os.path.isdir(t) else []:
            if os.path.islink(full) or os.path.getsize(full) > 1 << 20:
                continue
            try:
                data = open(full, "rb").read()
            except OSError:
                continue
            if re.search(rb"(?im)^\s*(export\s+)?(https?_proxy|all_proxy)\s*=", data):
                prox.append(f"{top}/{rel}")
    check("no proxy settings", not prox, " ".join(prox[:10]))
    resolv = read(root, "etc/resolv.conf") or ""
    check("no build host's DNS in resolv.conf", "nameserver" not in resolv and "search" not in resolv, "no such file" if not resolv else "")
    say("hostname:", (read(root, "etc/hostname") or "").strip())
    say("/etc/hosts:", (read(root, "etc/hosts") or "").strip())
    say("machine-id:", repr((read(root, "etc/machine-id") or "(no file)").strip()))
    check("no machine-id baked in", (read(root, "etc/machine-id") or "").strip() in ("", "uninitialized"))
    say("apk repositories:")
    say((read(root, "etc/apk/repositories") or "").strip())
    say("apk keys:", " ".join(sorted(os.listdir(os.path.join(root, "etc/apk/keys")))))
    check("pmbootstrap's local repository removed", "/mnt/pmbootstrap" not in (read(root, "etc/apk/repositories") or ""))


def check_tree(root, boot, owned):
    left = [x for x in ("in-pmbootstrap", "mnt/pmbootstrap", "usr/bin/qemu-aarch64-static", "tmp/apk_progress_fifo") if os.path.lexists(os.path.join(root, x))]
    check("no pmbootstrap leftovers", not left, " ".join(left))
    home = sorted(os.listdir(os.path.join(root, "home")))
    say("/home:", " ".join(home))
    homefiles = [p for _, p in walk(os.path.join(root, "home"))]
    skel = {os.path.join("user", p) for _, p in walk(os.path.join(root, "etc/skel"))}
    extra = sorted(set(homefiles) - skel)
    check("/home has only what /etc/skel has", not extra, " ".join(extra[:10]))
    for top in ("root", "tmp", "var/tmp", "var/log", "usr/local"):
        t = os.path.join(root, top)
        names = [p for _, p in walk(t)] if os.path.isdir(t) else []
        say(f"/{top}: {len(names)} files", " ".join(names[:20]))
    hist = [p for _, p in walk(root) if os.path.basename(p) in (".bash_history", ".ash_history", ".zsh_history", ".python_history", "core")]
    check("no shell histories or core files", not hist, " ".join(hist[:10]))
    priv = [p for _, p in walk(root) if re.search(r"(^|/)id_(rsa|ed25519|ecdsa)(\.pub)?$|\.rsa$", p) and not p.startswith("usr/share/")]
    check("no ssh or signing private keys", not priv, " ".join(priv[:10]))

    unowned = []
    for full, rel in walk(root):
        if rel not in owned:
            unowned.append(rel)
    say("")
    say(f"== files no package owns: {len(unowned)}, first directories below; full list in AUDIT-unowned.txt")
    counts = {}
    for rel in unowned:
        k = "/".join(rel.split("/")[:3])
        counts[k] = counts.get(k, 0) + 1
    for k, v in sorted(counts.items(), key=lambda t: -t[1])[:50]:
        say(f"{v:6} {k}")
    say("")
    fw = [p for p in unowned if p.startswith(("lib/firmware", "usr/lib/firmware"))]
    check("no firmware that a package doesn't own", not fw, " ".join(fw[:10]))
    samsung = [rel for _, rel in walk(root) if re.search(
        r"(^|/)(bdwlan30\.bin|sns\.reg|apnhlos|modem\.(mdt|b\d+)|adsp\.(mdt|b\d+|mbn)|mba\.(mbn|mdt)|venus\.(mdt|mbn)|a530_zap\.(mbn|mdt|b\d+))$|/samsung/gts3", rel)
        and rel not in owned]
    check("no Samsung firmware files", not samsung, " ".join(samsung[:10]))
    for sub in ("usr/lib/firmware/updates", "lib/firmware/updates"):
        d = os.path.join(root, sub)
        if os.path.isdir(d) and not os.path.islink(d):
            names = [p for _, p in walk(d)]
            check(f"/{sub} empty", not names, " ".join(names[:10]))
    return unowned


def fs_stat(fs, path):
    """Mode and owner from the filesystem itself: rdump drops setuid bits and xattrs."""
    out = sh("debugfs", "-R", f"stat {path}", fs).decode(errors="replace")
    mode = re.search(r"Mode:\s+(\d+)", out)
    user = re.search(r"User:\s+(\d+)\s+Group:\s+(\d+)", out)
    return (mode.group(1) if mode else "?", user.group(1) if user else "?", user.group(2) if user else "?", "capability" in out.lower())


def setuid_and_caps(rootfs, image_dir):
    # Which files to look at comes from the build's list; what they are comes from the image
    rows = [x.split("\t") for x in sh("zstd", "-dc", os.path.join(image_dir, "rootfs-files.tsv.zst")).decode(errors="replace").splitlines()]
    su = [f for f in rows if len(f) > 1 and stat.S_ISREG(int(f[1], 8)) and int(f[1], 8) & (stat.S_ISUID | stat.S_ISGID) and not f[0].startswith("/boot")]
    say("")
    say("== setuid or setgid files: mode uid gid in the image")
    for f in su:
        m = fs_stat(rootfs, f[0])
        say(f"{m[0]} {m[1]} {m[2]} {f[0]}")
        if f[0] == "/usr/bin/sudo":
            check("sudo is setuid root in the image", int(m[0], 8) == 0o4755 and m[1] == "0", str(m[:3]))
    check("the image has setuid files at all", bool(su), f"{len(su)} files")
    caps = [f[0] for f in rows if len(f) > 6 and "security.capability" in f[6] and not f[0].startswith("/boot")]
    say("== files with file capabilities: from the build's list, then read from the image")
    for c in caps:
        ea = sh("debugfs", "-R", f"ea_list {c}", rootfs).decode(errors="replace")
        say(c, "in image:", "security.capability" in ea or "capability" in ea)
    if not caps:
        say("none")
    for path, uid, mode in (("/home/user", "10000", None), ("/etc/shadow", "0", "640"), ("/etc/gts3-preview/default-password-hash", "0", "600")):
        m = fs_stat(rootfs, path)
        check(f"{path} in the image: owner {uid}", m[1] == uid and (mode is None or m[0].endswith(mode)), str(m[:3]))
    # A security.capability on anything that links libdbus breaks D-Bus on musl
    check("kwin_wayland has no file capabilities", "/usr/bin/kwin_wayland" not in caps)


def texts_to_scan(root, boot):
    """All regular files, plus the initramfs decompressed: it is a copy of rootfs files."""
    for tree, label in ((root, ""), (boot, "boot/")):
        for full, rel in walk(tree):
            if os.path.islink(full):
                yield None, label + rel, os.readlink(full).encode(errors="replace")
                continue
            yield full, label + rel, None
    for name in ("initramfs", "initramfs-extra"):
        full = os.path.join(boot, name)
        if not os.path.exists(full):
            continue
        raw = open(full, "rb").read()
        try:
            if raw[:4] == b"\x28\xb5\x2f\xfd":
                data = subprocess.run(["zstd", "-dc"], input=raw, capture_output=True, check=True).stdout
            elif raw[:2] == b"\x1f\x8b":
                data = gzip.decompress(raw)
            else:
                data = subprocess.run(["unlzma"], input=raw, capture_output=True).stdout or raw
        except Exception as e:
            say(f"couldn't decompress {name}: {e}")
            continue
        yield None, "boot/" + name + " (decompressed)", data


def secret_values(path):
    """Values of KEY=VALUE lines, without quotes, plus whole other lines. Short ones
    can't be told from noise and are counted but not searched."""
    vals = []
    for line in open(path, errors="replace"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        v = line.split("=", 1)[1] if "=" in line else line
        v = v.strip().strip("'\"")
        if v:
            vals.append(v)
    return vals


def scan(root, boot, a, owned):
    secrets = []
    for path in a.secret_file or []:
        for i, v in enumerate(secret_values(path)):
            secrets.append((os.path.basename(path), i + 1, v.encode()))
    keys = []
    for path in a.key_file or []:
        parts = open(path, errors="replace").read().split()
        if len(parts) >= 2:
            keys.append((os.path.basename(path), parts[1].encode()))
    terms = []
    if a.terms_file:
        terms = [x.strip() for x in open(a.terms_file, errors="replace") if x.strip() and not x.startswith("#")]
    found_secret = {}
    found_key = {}
    # These are made from what packages ship (module aliases, the hardware database, lists of
    # package files, compressed archives): a hit there is stock data, counted apart
    derived = re.compile(r"^(etc/udev/hwdb\.bin|usr/lib/apk/db/|var/log/apk\.log|var/cache/|boot/)")
    term_hits = {t: {"path": [], "owned": [], "derived": [], "unowned": []} for t in terms}
    # Short words match by chance inside binaries, so they need word boundaries
    term_re = {t: re.compile(rb"(?<![a-z0-9])" + re.escape(t.lower().encode()) + rb"(?![a-z0-9])") for t in terms}
    n = 0
    for full, rel, data in texts_to_scan(root, boot):
        n += 1
        for t in terms:
            if term_re[t].search(rel.lower().encode(errors="replace")):
                term_hits[t]["path"].append(rel)
        if data is None:
            try:
                st = os.lstat(full)
                if not stat.S_ISREG(st.st_mode):
                    continue
                data = open(full, "rb").read()
            except OSError:
                continue
        for label, i, v in secrets:
            if v in data:
                found_secret.setdefault((label, i), []).append(rel)
        for label, v in keys:
            if v in data:
                found_key[label] = found_key.get(label, 0) + 1
        if terms:
            low = data.lower()
            binary = b"\0" in data[:8192]
            for t in terms:
                # A word of three letters turns up by chance in any binary
                if len(t) < 5 and binary:
                    continue
                if t.lower().encode() in low and term_re[t].search(low):
                    which = "owned" if rel in owned else "derived" if derived.match(rel) else "unowned"
                    term_hits[t][which].append(rel)
    say(f"scanned {n} files")
    for label, i, v in secrets:
        if len(v) < 4:
            say(f"secret {label} value {i}: too short to search")
            continue
        hits = found_secret.get((label, i), [])
        strays = [x for x in hits if x not in owned]
        pkgs = sorted({owned[x] for x in hits if x in owned})
        # Where, never what. A string that also occurs in somebody's package isn't a leak of
        # ours, but say so, so that a person can judge.
        check(f"secret {label} value {i} not in the image, outside packages", not strays,
              "not found" if not hits else f"FOUND in {len(hits)} files: {' '.join(strays[:10]) or 'all in packages'}; packages: {' '.join(pkgs[:12])}")
    for label, v in keys:
        c = found_key.get(label, 0)
        check(f"ssh key {label} not in the image", c == 0, "FOUND" if c else "not found")
    for t in terms:
        h = term_hits[t]
        strays = [x for x in h["path"] if x not in owned] + h["unowned"]
        pkgs = sorted({owned[x] for x in h["path"] + h["owned"] if x in owned})
        check(f"term {t!r}: no state of ours", not strays,
              f"in names {len(h['path'])}, package files {len(h['owned'])}, made from packages {len(h['derived'])}, other {len(h['unowned'])}" +
              (" other: " + " ".join(strays[:8]) if strays else "") +
              (" packages: " + " ".join(pkgs[:12]) if pkgs else "") +
              (" made from packages: " + " ".join(h["derived"][:4]) if h["derived"] else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image_dir")
    ap.add_argument("--lock", required=True)
    ap.add_argument("--image", default="userdata.img")
    ap.add_argument("--report", default="AUDIT.txt")
    ap.add_argument("--secret-file", action="append")
    ap.add_argument("--key-file", action="append")
    ap.add_argument("--terms-file")
    a = ap.parse_args()
    lock = json.load(open(a.lock))
    img = os.path.join(a.image_dir, a.image)
    work = os.path.join(a.image_dir, "audit-tree")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    (boot_t, root_t), table = unpack(img, work)
    boot, root = boot_t[0], root_t[0]
    i = lock["image"]

    say("== image")
    say(sh("sfdisk", "-d", img).decode().strip())
    for tree, fs, p in (boot_t, root_t):
        sh("e2fsck", "-fn", fs)
        say(f"e2fsck clean: {os.path.basename(fs)}")
        info = sh("dumpe2fs", "-h", fs).decode(errors="replace")
        say("\n".join(x for x in info.splitlines() if re.match(r"Filesystem (volume name|UUID|features)|Block size|Block count|Inode count|Free blocks", x)))
    check("boot filesystem UUID", i["uuid_boot"] in sh("dumpe2fs", "-h", boot_t[1]).decode(errors="replace"))
    check("root filesystem UUID", i["uuid_root"] in sh("dumpe2fs", "-h", root_t[1]).decode(errors="replace"))
    fstab = read(root, "etc/fstab") or ""
    say("--- /etc/fstab")
    say(fstab.strip())
    check("fstab has the image's UUIDs", i["uuid_root"] in fstab and i["uuid_boot"] in fstab)

    bi = boot_image(os.path.join(boot, "boot.img"))
    say("")
    say("== boot.img")
    say("cmdline:", bi["cmdline"])
    say("kernel:", bi["version"])
    check("cmdline has the image's UUIDs", i["uuid_root"] in bi["cmdline"] and i["uuid_boot"] in bi["cmdline"])
    check("no serial console or debug shell on the command line", "pmos.debug-shell" not in bi["cmdline"])
    for d in bi["dtbs"]:
        say("dtb:", d)
    check("two appended device trees", len(bi["dtbs"]) == 2, str(len(bi["dtbs"])))
    # msm-id, soc revision, hardware id as lk2nd reports them on an SM-T820: 291 / 0x30001 / 8
    for name, msm in (("SM-T820", 291), ("SM-T825", 246)):
        for sub in (0, 2, 4, 5):
            got = lk2nd_pick(bi["dtbs"], msm, 0x30001, 8, sub)
            check(f"lk2nd's match picks one tree on {name}, board rev {sub}", len(got) == 1, " ".join(got))
    released = os.path.join(a.image_dir, "boot.img")
    check("the release's boot.img is the one in the image", open(released, "rb").read() == open(os.path.join(boot, "boot.img"), "rb").read())

    say("")
    say("== ssh")
    check_ssh(root, lock)
    say("")
    say("== login")
    check_login(root, lock)
    say("")
    say("== network")
    check_network(root)
    check_listeners(root)
    owned = owned_paths(root)
    say("")
    say("== contents")
    unowned = check_tree(root, boot, owned)
    with open(os.path.join(a.image_dir, a.report.replace(".txt", "-unowned.txt")), "w", errors="surrogateescape") as f:
        f.write("\n".join(sorted(unowned)) + "\n")
    setuid_and_caps(root_t[1], a.image_dir)
    say("")
    say("== apk world")
    say((read(root, "etc/apk/world") or "").strip())
    say("")
    say("== on-screen keyboard")
    say("/etc/xdg/kwinrc:", repr(read(root, "etc/xdg/kwinrc")))
    say("session autostart:", os.path.exists(os.path.join(root, "etc/xdg/autostart/gts3-first-login.desktop")))
    for rel in ("usr/share/applications/org.kde.plasma.keyboard.desktop",):
        check(f"{rel} exists", os.path.exists(os.path.join(root, rel)))
    say("")
    say("== leak scan")
    scan(root, boot, a, owned)
    shutil.rmtree(work)

    result = ["PASS" if not FAILS else f"FAIL: {len(FAILS)} checks", ""]
    head = [x for x in OUT if x.startswith(("PASS ", "FAIL "))]
    with open(os.path.join(a.image_dir, a.report), "w") as f:
        f.write("\n".join(result + head + ["", "==== details", ""] + OUT) + "\n")
    print("\n".join(result + head))
    for p in (a.report, a.report.replace(".txt", "-unowned.txt")):
        os.chown(os.path.join(a.image_dir, p), 1000, 1000)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

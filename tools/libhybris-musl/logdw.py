# SPDX-License-Identifier: BSD-3-Clause
# Minimal Android logd writer endpoint: prints what bionic liblog sends to
# /dev/socket/logdw (header: id u8, tid u16, realtime 8 bytes; then prio u8,
# tag\0, message\0).
import os, socket, sys
path = "/dev/socket/logdw"
os.makedirs("/dev/socket", exist_ok=True)
try: os.unlink(path)
except FileNotFoundError: pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(path); os.chmod(path, 0o666)
prios = "??VDIWEF"
out = open(sys.argv[1], "a", buffering=1)
while True:
    d = s.recv(65536)
    if len(d) < 12: continue
    body = d[11:]
    prio = body[0]; rest = body[1:].split(b"\0")
    tag = rest[0].decode(errors="replace"); msg = rest[1].decode(errors="replace") if len(rest) > 1 else ""
    out.write("%s/%s: %s\n" % (prios[prio] if prio < 8 else prio, tag, msg.rstrip()))

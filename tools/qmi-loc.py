#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""QMI LOC test client: one process, one QRTR socket (docs/gnss.md).

  qmi-loc.py watch [SECONDS]  report the GNSS engine turning on and off; runs
                              no session of its own
  qmi-loc.py nmea [SECONDS]   run a standalone session like gnss-share's
                              qmi_loc driver and write its NMEA to stdout

The location service (QRTR service 16, version 2) is found through the
kernel's QRTR name service; if the modem restarts, the client follows it.
The system time is injected only if systemd-timesyncd has synchronized the
clock (or with --time). Status goes to stderr. stdout of 'nmea' carries the
position: don't keep it; gnss-test.sh masks it.

Message and TLV IDs: libqmi data/qmi-service-loc.json; QRTR control
packets: Linux include/uapi/linux/qrtr.h.
"""
import argparse
import os
import select
import signal
import socket
import struct
import sys
import time

AF_QIPCRTR = getattr(socket, 'AF_QIPCRTR', 42)
PORT_CTRL = 0xfffffffe
NEW_SERVER, DEL_SERVER, NEW_LOOKUP = 4, 5, 10
LOC_SERVICE, LOC_VERSION = 16, 2

REG_EVENTS, START, STOP = 0x21, 0x22, 0x23
NMEA_IND, TIME_REQ_IND, ENGINE_IND, FIX_STATE_IND = 0x26, 0x28, 0x2b, 0x2c
INJECT_UTC_TIME, SET_OPERATION_MODE = 0x38, 0x4a
EV_NMEA, EV_TIME_REQ, EV_ENGINE, EV_FIX_STATE = 1 << 2, 1 << 4, 1 << 7, 1 << 8
STANDALONE = 4
SYNCED = '/run/systemd/timesync/synchronized'

t0 = time.monotonic()
stopping = False


def log(msg):
    print(f'{time.monotonic() - t0:7.1f}s {msg}', file=sys.stderr, flush=True)


def tlv(t, fmt, *v):
    return t, struct.pack(fmt, *v)


class Loc:
    def __init__(self):
        self.s = socket.socket(AF_QIPCRTR, socket.SOCK_DGRAM)
        self.node = self.s.getsockname()[0]
        self.svc = None
        self.txn = 0
        self.s.sendto(struct.pack('<5I', NEW_LOOKUP, LOC_SERVICE, 0, 0, 0),
                      (self.node, PORT_CTRL))

    def request(self, msg_id, *tlvs):
        self.txn = self.txn % 0xffff + 1
        body = b''.join(struct.pack('<BH', t, len(v)) + v for t, v in tlvs)
        self.s.sendto(struct.pack('<BHHH', 0, self.txn, msg_id, len(body)) + body,
                      self.svc)
        return self.txn

    def recv(self, timeout):
        """One packet as ('ctrl', cmd, service, version, addr) or
        ('qmi', type, txn, msg_id, tlvs), or None"""
        try:
            ready, _, _ = select.select([self.s], [], [], max(timeout, 0))
        except InterruptedError:
            return None
        if not ready:
            return None
        data, addr = self.s.recvfrom(65536)
        if addr == (self.node, PORT_CTRL):
            if len(data) < 20:
                return None
            cmd, service, inst, node, port = struct.unpack_from('<5I', data)
            return 'ctrl', cmd, service, inst & 0xff, (node, port)
        if addr != self.svc or len(data) < 7:
            return None
        typ, txn, msg_id, length = struct.unpack_from('<BHHH', data)
        tlvs, i, end = {}, 7, min(len(data), 7 + length)
        while i + 3 <= end:
            t, n = struct.unpack_from('<BH', data, i)
            tlvs[t] = data[i + 3:i + 3 + n]
            i += 3 + n
        return 'qmi', typ, txn, msg_id, tlvs


def u32(tlvs, t):
    v = tlvs.get(t, b'')
    return struct.unpack('<I', v[:4])[0] if len(v) >= 4 else None


def inject_time(loc, force):
    if not force and not os.path.exists(SYNCED):
        log(f'clock not synchronized ({SYNCED} missing), not injecting the time')
        return False
    loc.request(INJECT_UTC_TIME, tlv(0x01, '<Q', int(time.time() * 1000)),
                tlv(0x02, '<I', 500))
    log('injected the system time (500 ms)')
    return True


def setup(loc, args):
    if args.mode == 'watch':
        loc.request(REG_EVENTS, tlv(0x01, '<Q', EV_ENGINE | EV_FIX_STATE))
        return
    loc.request(REG_EVENTS, tlv(0x01, '<Q', args.events))
    loc.request(SET_OPERATION_MODE, tlv(0x01, '<I', STANDALONE))
    time.sleep(0.3)
    loc.request(START, tlv(0x01, '<B', 1),       # session ID
                tlv(0x10, '<I', 1),              # periodic fixes
                tlv(0x12, '<I', 1),              # intermediate reports on
                tlv(0x13, '<I', 1000))           # every 1000 ms
    if not args.no_time:
        inject_time(loc, args.time)


def handle_indication(loc, args, msg_id, tlvs, state):
    if msg_id == NMEA_IND:
        text = tlvs.get(0x01, b'').split(b'\0')[0].decode('ascii', 'replace')
        for line in text.splitlines():
            line = line.strip()
            if line.startswith(('$', '!')):
                sys.stdout.write(line + '\r\n')
        sys.stdout.flush()
        state['nmea'] = state.get('nmea', 0) + 1
    elif msg_id == ENGINE_IND:
        log('engine ' + {1: 'on', 2: 'off'}.get(u32(tlvs, 0x01), str(u32(tlvs, 0x01))))
    elif msg_id == FIX_STATE_IND:
        log(f'fix session state {u32(tlvs, 0x01)}')
    elif msg_id == TIME_REQ_IND:
        log('engine asks for the time')
        recent = time.monotonic() - state.get('time', -9) < 5
        if args.mode == 'nmea' and not args.no_time and not recent:
            if inject_time(loc, args.time):
                state['time'] = time.monotonic()
    elif msg_id in (SET_OPERATION_MODE, INJECT_UTC_TIME):
        if u32(tlvs, 0x01):
            log(f'indication 0x{msg_id:02x}: status {u32(tlvs, 0x01)}')


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('mode', choices=('watch', 'nmea'))
    p.add_argument('seconds', nargs='?', type=float, default=120)
    p.add_argument('--events', type=lambda s: int(s, 0),
                   default=EV_NMEA | EV_TIME_REQ | EV_ENGINE | EV_FIX_STATE,
                   help='REG_EVENTS mask for nmea (default 0x194, as gnss-share; '
                        'the first hardware fix used 0x3ff7)')
    p.add_argument('--time', action='store_true', help='inject the time even if not synchronized')
    p.add_argument('--no-time', action='store_true', help='never inject the time')
    args = p.parse_args()

    def on_signal(signum, frame):
        global stopping
        stopping = True
    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    loc = Loc()
    state = {}
    end = time.monotonic() + args.seconds
    while not stopping and time.monotonic() < end:
        pkt = loc.recv(min(1.0, end - time.monotonic()))
        if pkt is None:
            continue
        if pkt[0] == 'ctrl':
            _, cmd, service, version, addr = pkt
            if cmd == NEW_SERVER and service == 0:     # end of the listing
                if loc.svc is None and not state.get('listed'):
                    log('waiting for the location service (is the modem running?)')
                state['listed'] = True
            elif cmd == NEW_SERVER and service == LOC_SERVICE and loc.svc is None:
                if version != LOC_VERSION:
                    log(f'ignoring location service version {version}')
                    continue
                loc.svc = addr
                log('location service on QRTR node %d port %d' % addr)
                setup(loc, args)
            elif cmd == DEL_SERVER and addr == loc.svc:
                loc.svc = None
                log('location service gone (modem stopped?)')
            continue
        _, typ, txn, msg_id, tlvs = pkt
        if typ == 2:
            res = tlvs.get(2, b'')
            if len(res) >= 4 and struct.unpack('<H', res[:2])[0]:
                log(f'request 0x{msg_id:02x} failed: error {struct.unpack("<H", res[2:4])[0]}')
        elif typ == 4:
            handle_indication(loc, args, msg_id, tlvs, state)

    if args.mode == 'nmea' and loc.svc is not None:
        txn = loc.request(STOP, tlv(0x01, '<B', 1))
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            pkt = loc.recv(deadline - time.monotonic())
            if pkt and pkt[0] == 'qmi' and pkt[1] == 2 and pkt[2] == txn:
                log('session stopped')
                break
    if args.mode == 'nmea':
        log(f'{state.get("nmea", 0)} NMEA indications')
    loc.s.close()


if __name__ == '__main__':
    main()

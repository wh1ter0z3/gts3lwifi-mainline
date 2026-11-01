#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A fake QMI LOC service on the local QRTR node, for testing the GNSS tools
on a PC (tests/gnss/run.sh). Never run it on the tablet.

  fake-loc.py [--restart-after SECONDS] [--fix-after N]

Publishes service 16 version 2 through the kernel's name service (and
refuses to if one exists already), answers every request with success, and
while a client's session runs sends it synthetic NMEA at 0 N 0 E once a
second: no fix for the first N seconds (default 2), then a fix. Engine
on/off indications go to every client registered for them. Each request is
logged to stderr as 'request 0xNN from PORT'. --restart-after closes the
service and publishes it again on a new socket, as a modem restart does.
"""
import argparse
import select
import socket
import struct
import sys
import time

AF_QIPCRTR = getattr(socket, 'AF_QIPCRTR', 42)
PORT_CTRL = 0xfffffffe
NEW_SERVER, NEW_LOOKUP = 4, 10
REG_EVENTS, START, STOP, SET_OPERATION_MODE = 0x21, 0x22, 0x23, 0x4a
NMEA_IND, ENGINE_IND = 0x26, 0x2b
EV_NMEA, EV_ENGINE = 1 << 2, 1 << 7


def sentence(body):
    cs = 0
    for c in body.encode():
        cs ^= c
    return f'${body}*{cs:02X}'


def nmea(fix):
    q, used, hdop = ('1', '08', '0.9') if fix else ('0', '00', '')
    status = 'A' if fix else 'V'
    return [
        sentence(f'GPGSV,1,1,04,01,45,090,{38 if fix else 20},02,30,180,35,'
                 '03,60,270,,04,10,000,31'),
        sentence(f'GPGSA,A,{3 if fix else 1},01,02,04,,,,,,,,,,1.5,{hdop},1.2'),
        sentence(f'GPGGA,120000.00,0000.0000,N,00000.0000,E,{q},{used},{hdop},0.0,M,0.0,M,,'),
        sentence(f'GPRMC,120000.00,{status},0000.0000,N,00000.0000,E,0.0,0.0,010170,,,A'),
        sentence('PQXFI,120000.0,0000.0000,N,00000.0000,E,0.0,1.0,1.0,0.1'),
    ]


def log(msg):
    print(f'fake-loc: {msg}', file=sys.stderr, flush=True)


def tlv(t, v):
    return struct.pack('<BH', t, len(v)) + v


def message(typ, txn, msg_id, *tlvs):
    body = b''.join(tlvs)
    return struct.pack('<BHHH', typ, txn, msg_id, len(body)) + body


class Service:
    def __init__(self):
        self.s = socket.socket(AF_QIPCRTR, socket.SOCK_DGRAM)
        node = self.s.getsockname()[0]
        self.s.sendto(struct.pack('<5I', NEW_SERVER, 16, 2, 0, 0), (node, PORT_CTRL))
        log(f'published on node {node} port {self.s.getsockname()[1]}')
        self.masks = {}       # client -> event mask
        self.sessions = {}    # client -> session start time

    def broadcast(self, msg_id, *tlvs):
        for client, mask in self.masks.items():
            if mask & EV_ENGINE:
                self.s.sendto(message(4, 0, msg_id, *tlvs), client)

    def engine(self, before):
        if bool(self.sessions) != before:
            self.broadcast(ENGINE_IND, tlv(0x01, struct.pack('<I', 1 if self.sessions else 2)))

    def handle(self, data, client):
        if len(data) < 7 or data[0] != 0:
            return
        txn, msg_id = struct.unpack_from('<HH', data, 1)
        log(f'request 0x{msg_id:02x} from {client[1]}')
        before = bool(self.sessions)
        self.s.sendto(message(2, txn, msg_id, tlv(0x02, b'\0\0\0\0')), client)
        if msg_id == REG_EVENTS:
            self.masks[client] = struct.unpack_from('<Q', data, 10)[0]
        elif msg_id == SET_OPERATION_MODE:
            self.s.sendto(message(4, 0, msg_id, tlv(0x01, struct.pack('<I', 0))), client)
        elif msg_id == START:
            self.sessions[client] = time.monotonic()
        elif msg_id == STOP:
            self.sessions.pop(client, None)
        self.engine(before)

    def drop(self, client):
        before = bool(self.sessions)
        self.masks.pop(client, None)
        self.sessions.pop(client, None)
        self.engine(before)

    def tick(self, fix_after):
        for client, started in list(self.sessions.items()):
            if self.masks.get(client, 0) & EV_NMEA:
                for line in nmea(time.monotonic() - started >= fix_after):
                    self.s.sendto(message(4, 0, NMEA_IND, tlv(0x01, line.encode() + b'\0')), client)


def already_published():
    s = socket.socket(AF_QIPCRTR, socket.SOCK_DGRAM)
    s.sendto(struct.pack('<5I', NEW_LOOKUP, 16, 0, 0, 0), (s.getsockname()[0], PORT_CTRL))
    found = False
    while select.select([s], [], [], 1)[0]:
        cmd, service = struct.unpack_from('<2I', s.recv(64))
        if service == 0:
            break
        found = True
    s.close()
    return found


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--restart-after', type=float)
    p.add_argument('--fix-after', type=float, default=2)
    args = p.parse_args()
    if already_published():
        sys.exit('fake-loc: a location service exists already, not adding a fake one')

    svc = Service()
    restart = time.monotonic() + args.restart_after if args.restart_after else None
    next_tick = time.monotonic() + 1
    while True:
        now = time.monotonic()
        if restart and now >= restart:
            log('restarting')
            svc.s.close()
            time.sleep(0.2)
            svc, restart = Service(), None
        if now >= next_tick:
            svc.tick(args.fix_after)
            next_tick += 1
        if select.select([svc.s], [], [], 0.1)[0]:
            data, addr = svc.s.recvfrom(65536)
            if addr[1] == PORT_CTRL:
                # DEL_CLIENT from the kernel when a client's socket closes
                if len(data) >= 12 and struct.unpack_from('<I', data)[0] == 6:
                    svc.drop(struct.unpack_from('<2I', data, 4))
                continue
            svc.handle(data, addr)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass

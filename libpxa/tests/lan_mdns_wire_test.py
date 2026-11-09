#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent DNS packet client: no vendor parser, multicast loopback, or daemon."""
import select
import socket
import struct
import subprocess
import sys
import time


def dns_name(name):
    return b"".join(bytes([len(part)]) + part.encode() for part in name.rstrip(".").split(".")) + b"\0"


def read_name(packet, offset):
    labels, end, seen = [], None, set()
    while True:
        assert offset < len(packet) and offset not in seen
        seen.add(offset)
        n = packet[offset]
        offset += 1
        if n & 0xC0 == 0xC0:
            assert offset < len(packet)
            if end is None:
                end = offset + 1
            offset = ((n & 63) << 8) | packet[offset]
        elif n == 0:
            return b".".join(labels).decode() + ".", end or offset
        else:
            assert n <= 63 and offset + n <= len(packet)
            labels.append(packet[offset:offset + n])
            offset += n


def parse(packet):
    ident, flags, questions, answers, authority, extra = struct.unpack_from("!6H", packet)
    assert flags & 0x8400 == 0x8400
    offset, records = 12, []
    for _ in range(questions):
        _, offset = read_name(packet, offset)
        offset += 4
    for _ in range(answers + authority + extra):
        name, offset = read_name(packet, offset)
        kind, klass, ttl, size = struct.unpack_from("!HHIH", packet, offset)
        offset += 10
        assert offset + size <= len(packet)
        value = packet[offset:offset + size]
        if kind == 12:
            value, _ = read_name(packet, offset)
        elif kind == 33:
            priority, weight, port = struct.unpack_from("!3H", packet, offset)
            host, _ = read_name(packet, offset + 6)
            value = (priority, weight, port, host)
        records.append((name, kind, klass, ttl, value))
        offset += size
    assert offset == len(packet)
    return ident, records


def main():
    process = subprocess.Popen([sys.argv[1], "--advertise"], stdout=subprocess.PIPE, text=True)
    try:
        assert select.select([process.stdout], [], [], 5)[0], "provider startup timed out"
        ip, port = process.stdout.readline().split()
        port = int(port)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.3)
        name = "_pxawire._tcp.local."
        packet = struct.pack("!6H", 0x1234, 0, 1, 0, 0, 0) + dns_name(name) + struct.pack("!HH", 12, 1)
        found = {}
        deadline = time.monotonic() + 5
        while len(found) < 2 and time.monotonic() < deadline:
            sock.sendto(packet, (ip, 5353))
            try:
                response, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            ident, records = parse(response)
            assert ident == 0x1234
            ptr = next(r for r in records if r[1] == 12)
            srv = next(r for r in records if r[1] == 33)
            address = next(r for r in records if r[1] == 1)
            txt = next(r for r in records if r[1] == 16)
            assert ptr[0] == name and ptr[4] == srv[0] == txt[0]
            assert srv[4][2] == port and srv[4][3] == address[0]
            assert socket.inet_ntoa(address[4]) == ip
            assert all(r[2] == 1 and 0 < r[3] <= 10 for r in records), "legacy reply rules"
            found[ptr[4]] = txt[4]
        assert len(found) == 2, found
        for instance, txt in found.items():
            expected = b"\x03v=1\x04flag\x05bin=\xff" if instance.startswith("wire-meta-") else b"\0"
            assert txt == expected, (instance, txt)
            # TXT-only queries must answer TXT (rather than returning SRV as the answer).
            request = struct.pack("!6H", 0x4321, 0, 1, 0, 0, 0) + dns_name(instance) + struct.pack("!HH", 16, 1)
            sock.sendto(request, (ip, 5353))
            while True:
                response, _ = sock.recvfrom(4096)
                ident, records = parse(response)
                if ident == 0x4321:
                    assert records[0][1] == 16 and records[0][4] == expected
                    break
        # A response that repeats a question must not trigger another response.
        sock.settimeout(0.15)
        try:
            while True: sock.recvfrom(4096)
        except socket.timeout:
            pass
        repeated = struct.pack("!6H", 0x5678, 0x8400, 1, 0, 0, 0) + dns_name(name) + struct.pack("!HH", 12, 1)
        sock.sendto(repeated, (ip, 5353))
        try:
            unexpected, _ = sock.recvfrom(4096)
        except socket.timeout:
            pass
        else:
            raise AssertionError("responder answered a response's repeated question")
        sock.close()
        print("LAN mDNS wire PASS: independent PTR/SRV/A/TXT decoding, legacy unicast, key-only/binary/empty TXT")
    finally:
        process.terminate()
        process.wait(timeout=3)


if __name__ == "__main__":
    main()

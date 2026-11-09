#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Observe real probes and inject an independent competing mDNS responder."""
import select
import socket
import struct
import subprocess
import sys
import time
from lan_mdns_wire_test import dns_name, read_name


def parse(packet):
    _, flags, qd, an, ns, ar = struct.unpack_from('!6H', packet)
    at, questions, records = 12, [], []
    for _ in range(qd):
        name, at = read_name(packet, at)
        kind, klass = struct.unpack_from('!HH', packet, at)
        at += 4
        questions.append((name, kind, klass))
    for section, count in enumerate((an, ns, ar)):
        for _ in range(count):
            name, at = read_name(packet, at)
            kind, klass, ttl, size = struct.unpack_from('!HHIH', packet, at)
            at += 10
            assert at + size <= len(packet)
            value = packet[at:at + size]
            if kind == 33:
                target, _ = read_name(packet, at + 6)
                value = (struct.unpack_from('!3H', packet, at), target)
            records.append((section, name, kind, klass, ttl, value))
            at += size
    assert at == len(packet)
    return flags, questions, records


def record(owner, kind, data):
    return dns_name(owner) + struct.pack('!HHIH', kind, 1, 120, len(data)) + data


def test(binary, competing_probe):
    route = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    route.connect(('224.0.0.251', 5353))
    ip = route.getsockname()[0]
    route.close()
    observer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    observer.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, 'SO_REUSEPORT'):
        observer.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    observer.bind(('', 5353))
    observer.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                        socket.inet_aton('224.0.0.251') + socket.inet_aton(ip))
    observer.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
    observer.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    observer.settimeout(0.15)
    process = subprocess.Popen([binary, '--probe-advertise'], stdout=subprocess.PIPE, text=True)
    try:
        assert select.select([process.stdout], [], [], 5)[0]
        line = process.stdout.readline().split()
        assert line[0] == 'OPEN', line
        provider_ip, port = line[1], int(line[2])
        first, active, probes, announcements = None, None, [], []
        injected, injection_at = False, None
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            assert process.poll() is None, 'provider exited early'
            try:
                packet, source = observer.recvfrom(4096)
            except socket.timeout:
                continue
            if source[0] != provider_ip:
                continue
            try:
                flags, questions, records = parse(packet)
            except (AssertionError, UnicodeError, struct.error, IndexError):
                continue  # Other hosts may advertise unrelated non-UTF8 names.
            instance_questions = [q for q in questions if q[0].startswith('wire-meta-')]
            if flags == 0 and instance_questions and records:
                # Own injected packets use a competing TXT value or SRV port.
                if competing_probe and any(r[2] == 16 and r[5] == b'\x03v=9' for r in records):
                    continue
                current = instance_questions[0][0]
                now = time.monotonic()
                assert all(q[1] == 255 for q in questions)
                assert {r[2] for r in records if r[0] == 1} == {1, 16, 33}
                if first is None:
                    first = current
                if not injected:
                    host = next(r[5][1] for r in records if r[2] == 33)
                    srv = struct.pack('!3H', 0, 0, port if competing_probe else port + 1) + dns_name(host)
                    if competing_probe:
                        # TXT sorts before SRV regardless of packet record order.
                        competing = (struct.pack('!6H', 0, 0, 1, 0, 2, 0) +
                            dns_name(current) + struct.pack('!HH', 255, 1) +
                            record(current, 33, srv) + record(current, 16, b'\x03v=9'))
                    else:
                        competing = struct.pack('!6H', 0, 0x8400, 0, 1, 0, 0) + record(current, 33, srv)
                    observer.sendto(competing, ('224.0.0.251', 5353))
                    injected, injection_at = True, now
                    continue
                if competing_probe:
                    assert current == first, 'simultaneous loser should retry the same name'
                    if now - injection_at < 0.9:
                        continue
                else:
                    if current == first:
                        continue
                    assert now - injection_at >= 4.9, 'rename retries require the five-second rate limit'
                active = current
                probes.append(now)
            if flags & 0x8000 and active:
                advertised = [r for r in records if r[1] == active and r[2] == 33 and r[5][0][2] == port]
                if advertised:
                    announcements.append(time.monotonic())
                    if len(announcements) == 2:
                        break
        assert injected and active and len(probes) == 3, (injected, active, probes)
        assert all(b - a >= 0.20 for a, b in zip(probes, probes[1:])), probes
        assert len(announcements) == 2, announcements
        assert announcements[0] - probes[-1] >= 0.20, 'claim must wait after the third probe'
        assert announcements[1] - announcements[0] >= 0.90, 'two startup announcements, one second apart'
        assert select.select([process.stdout], [], [], 1)[0]
        assert process.stdout.readline().strip() == 'READY'
        print('PASS', 'simultaneous probe tiebreak -> same-name retry' if competing_probe else
              'existing name -> rename, five-second retry', flush=True)
    finally:
        process.terminate()
        process.wait(timeout=3)
        observer.close()


def main():
    test(sys.argv[1], False)
    test(sys.argv[1], True)
    print('LAN mDNS probe PASS: independent conflicts, sorted RDATA, bounded retries and startup timing')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Check that unsigned version defaults become current, forward-compatible manifest ranges."""

import struct
import sys
from pathlib import Path

from build_package_manifest import parse_services
from protocol_metadata import SERVICE_IDS, SERVICE_VERSIONS


def records(data):
    offset = 0
    while offset < len(data):
        tag, length = struct.unpack_from("<HH", data, offset)
        offset += 4
        yield tag, data[offset:offset + length]
        offset += length


def service_requirements(component):
    result = {}
    for tag, payload in records(component):
        if tag != 5:
            continue
        fields = dict(records(payload))
        service_id = struct.unpack("<H", fields[1])[0]
        result[service_id] = (struct.unpack("<HH", fields[2]),
                              struct.unpack("<HH", fields[3]))
    return result


def assert_default(requirements, name):
    service_id = SERVICE_IDS[name]
    major, minor = SERVICE_VERSIONS[service_id]
    assert requirements[service_id] == ((major, minor), (major, 0xFFFF)), name


for service_name in ("window", "ui", "clock", "fs", "net"):
    for declaration in (service_name, {"name": service_name}):
        service_id = SERVICE_IDS[service_name]
        parsed = parse_services([declaration], "services")
        assert len(parsed) == 1
        assert_default({service_id: (parsed[0][1], parsed[0][2])}, service_name)

manifest = Path(sys.argv[1]).read_bytes()
assert manifest[:4] == b"PXAM"
components = {}
for tag, payload in records(manifest[12:]):
    if tag == 16:
        fields = dict(records(payload))
        components[fields[1].decode("ascii")] = service_requirements(payload)

for name in ("window", "ui", "clock", "fs", "permission"):
    assert_default(components["main"], name)
for name in ("ipc", "permission"):
    assert_default(components["responder"], name)
assert components["main"][SERVICE_IDS["net"]] == ((0, 1), (0, 4))
print("PXA Service default version verification OK")

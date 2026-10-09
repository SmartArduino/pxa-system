#!/usr/bin/env python3
"""Release gates for locks, capabilities and archive installation failures."""
import hashlib
import io
import json
import os
import struct
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import compat
import install
import pxa
sys.path.insert(0, str(pxa.ROOT / "tools/package"))
import build_package_manifest as package_manifest


def tlv(*items):
    return b"".join(struct.pack("<HH", tag, len(value)) + value for tag, value in items)


def manifest(core=(1, 0), service=(3, (0, 6), (0, 65535), 1), artifacts=()):
    sid, minimum, maximum, features = service
    required = tlv((1, struct.pack("<H", sid)), (2, struct.pack("<HH", *minimum)),
                   (3, struct.pack("<HH", *maximum)), (4, struct.pack("<Q", features)))
    component = tlv((1, b"main"), (5, required))
    for kind, name, abi, features in artifacts or [(2, "main.aot", "abi", 0)]:
        component += tlv((4, tlv((1, bytes([kind])), (2, name.encode()), (3, b"linux-x86_64"),
                                (4, b"wamr"), (5, abi.encode()),
                                (6, struct.pack("<Q", features)), (7, b"\1"))))
    body = tlv((2, b"pxa-test"), (7, struct.pack("<HH", *core)), (16, component))
    return b"PXAM" + struct.pack("<HHI", 0, 7, len(body)) + body


def host():
    return {"core": [1, 0], "target": "linux-x86_64", "engine": "wamr", "engine_abi": "abi",
            "memory_model": 1, "artifact_features": 3, "wasm": True,
            "services": [{"id": 3, "version": [0, 6], "features": 1}]}


class CompatibilityTest(unittest.TestCase):
    def test_matching_contract(self):
        self.assertTrue(compat.analyze(manifest(), host())["compatible"])

    def test_core_major_and_minor(self):
        for core in ((0, 1), (1, 1), (2, 0)):
            self.assertFalse(compat.analyze(manifest(core=core), host())["compatible"])

    def test_service_absent_version_and_feature(self):
        for service in ((9, (0, 1), (0, 65535), 0), (3, (0, 7), (0, 65535), 0),
                        (3, (0, 1), (0, 5), 0), (3, (1, 0), (1, 65535), 0),
                        (3, (0, 6), (0, 65535), 2)):
            self.assertFalse(compat.analyze(manifest(service=service), host())["compatible"])

    def test_exact_aot_abi_and_wasm_fallback(self):
        encoded = manifest(artifacts=[(2, "bad.aot", "other", 0), (1, "main.wasm", "", 0)])
        self.assertEqual(compat.analyze(encoded, host())["artifacts"], {"main": "main.wasm"})
        value = host() | {"wasm": False}
        self.assertFalse(compat.analyze(encoded, value)["compatible"])

    def test_native_selector_specificity_and_tie_break(self):
        encoded = manifest(artifacts=[(2, "z.aot", "abi", 1), (2, "a.aot", "abi", 1),
                                      (2, "basic.aot", "abi", 0), (2, "x.aot", "abi", 4)])
        self.assertEqual(compat.analyze(encoded, host())["artifacts"], {"main": "a.aot"})

    def test_bounded_parser(self):
        for encoded in (b"\1", struct.pack("<HH", 1, 50) + b"x"):
            with self.assertRaises(ValueError): list(compat.records(encoded))
        with self.assertRaises(ValueError): compat.singleton(tlv((1, b"a"), (1, b"b")))


class BaselineTest(unittest.TestCase):
    def test_inventory_rejects_changed_inputs_and_escaping_links(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "file").write_bytes(b"original")
            record = {"path": "file", "sha256": hashlib.sha256(b"original").hexdigest()}
            self.assertTrue(pxa.inventory_matches(root, record, {}))
            (root / "file").write_bytes(b"changed")
            self.assertFalse(pxa.inventory_matches(root, record, {}))
            (root / "link").symlink_to("/etc/passwd")
            self.assertFalse(pxa.inventory_matches(root, {"path": "link", "link": "/etc/passwd"}, {}))
            self.assertFalse(pxa.inventory_matches(root, {"path": "../outside"}, {}))

    def test_missing_external_private_key_is_not_generated(self):
        with tempfile.TemporaryDirectory() as temp:
            missing = Path(temp) / "missing.pem"
            with mock.patch.dict(os.environ, {"PXA_USER_HOME": temp, "PXA_SIGNING_KEY": str(missing)}):
                with self.assertRaisesRegex(ValueError, "existing private key"):
                    pxa.keys()
            self.assertFalse(missing.exists())

    def test_requirements_are_resolved_once(self):
        package = {"components": [{"id": "main", "kind": "ui", "services": ["ui"],
                                   "wasi": {"features": ["monotonic-clock"]}}],
                   "permissions": [{"name": "net.client"}],
                   "ipc_endpoints": [{"component": "main"}]}
        resolved = pxa.freeze_services(package)
        self.assertEqual({s["name"] for s in resolved["components"][0]["services"]},
                         {"ui", "window", "clock", "permission", "ipc", "wasi"})
        self.assertEqual(resolved["min_core"], [1, 0])
        self.assertEqual(pxa.freeze_services(resolved), resolved)
        self.assertEqual(package["components"][0]["services"], ["ui"])

    def test_explicit_old_service_baseline_is_preserved(self):
        requirement = {"name": "ui", "min_version": [0, 2], "max_version": [0, 65535], "features": ["canvas"]}
        resolved = pxa.freeze_services({"components": [{"id": "main", "kind": "ui", "services": [requirement]}]})
        self.assertIn(requirement, resolved["components"][0]["services"])

    def test_frozen_wasi_range_requires_wasi_configuration(self):
        requirement = {"name": "wasi", "min_version": [0, 0], "max_version": [0, 65535], "features": []}
        parsed = package_manifest.parse_services([requirement], "component services", allow_wasi=True)
        self.assertEqual(parsed[0][1], (0, 0))
        with self.assertRaises(package_manifest.PackageError):
            package_manifest.parse_services([requirement], "component services")

    def test_lock_rejects_other_distribution(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp)
            pxa.write(app / "pxa.lock", {"schema": "pxa-lock-1", "release": "wrong", "distribution_sha256": "x"})
            with self.assertRaisesRegex(ValueError, "requires DevKit"): pxa.lock_for(app)


class InstallTest(unittest.TestCase):
    def archive(self, directory, escaped=False):
        path = directory / "kit.tar.gz"
        entry = b"#!/bin/sh\n"
        inventory = {"schema": "pxa-distribution-1", "files": [{"path": "bin/pxa", "sha256": hashlib.sha256(entry).hexdigest()}]}
        with tarfile.open(path, "w:gz") as archive:
            for name, value in (("kit/bin/pxa", entry), ("kit/distribution.json", json.dumps(inventory).encode())):
                info = tarfile.TarInfo(name); info.size = len(value); info.mode = 0o755
                archive.addfile(info, io.BytesIO(value))
            if escaped:
                info = tarfile.TarInfo("kit/escape"); info.type = tarfile.SYMTYPE; info.linkname = "../../escaped"
                archive.addfile(info)
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_install_and_existing_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); archive, checksum = self.archive(root)
            installed = install.unpack(archive, checksum, root / "sdk")
            self.assertTrue((installed / "bin/pxa").is_file())
            with self.assertRaisesRegex(ValueError, "already exists"): install.unpack(archive, checksum, installed)

    def test_bad_checksum_leaves_destination_absent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); archive, _ = self.archive(root)
            with self.assertRaisesRegex(ValueError, "checksum"): install.unpack(archive, "0" * 64, root / "sdk")
            self.assertFalse((root / "sdk").exists())

    def test_escaping_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); archive, checksum = self.archive(root, True)
            with self.assertRaises(tarfile.FilterError): install.unpack(archive, checksum, root / "sdk")
            self.assertFalse((root / "sdk").exists())


if __name__ == "__main__":
    unittest.main()

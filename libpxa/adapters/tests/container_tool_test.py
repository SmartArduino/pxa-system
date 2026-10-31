#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import struct
import zlib
import hashlib
import random
import subprocess
import sys
import tempfile
from pathlib import Path


PXA_SYSTEM_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PXA_SYSTEM_ROOT / "tools/wamr"))
from metadata import load as load_wamr_metadata


def run(*args: object, ok: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run([str(arg) for arg in args], check=False,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (result.returncode == 0) != ok:
        raise RuntimeError(
            f"unexpected exit {result.returncode}: {' '.join(map(str, args))}\n"
            f"{result.stdout.decode(errors='replace')}"
            f"{result.stderr.decode(errors='replace')}")
    return result


def metadata(path: Path, sequence: int, lineage: str | None) -> None:
    value = {
        "format": "pxa-package-source-0.1",
        "id": "pxa-rotation-test",
        "version": f"{sequence}.0.0",
        "release_sequence": sequence,
    }
    if lineage is not None:
        value["publisher_lineage"] = lineage
    path.write_text(json.dumps(value), encoding="utf-8")


def signed_package(root: Path, tools: Path, key: Path, meta: Path,
                   output: Path) -> Path:
    package = output.with_suffix("")
    artifacts = package / "artifacts"
    artifacts.mkdir(parents=True)
    (artifacts / "main.wasm").write_bytes(b"wasm" * 18000 + bytes(range(256)) * 128)
    (artifacts / "main.linux-x86_64.aot").write_bytes(b"aot" * 500)
    assets = package / "assets"; assets.mkdir()
    (assets / "empty.bin").write_bytes(b"")
    (assets / "random.bin").write_bytes(random.Random(57).randbytes(33000))
    # Cross multiple cached-index windows even with 32-KiB chunks, including
    # the transition into the following empty and incompressible files.
    (assets / "cache-boundary.bin").write_bytes(b"index-window" * (256 * 1024))
    run(sys.executable, tools / "build_package_manifest.py", meta, package,
        key, "linux-x86_64", load_wamr_metadata()["engine_abi"])
    run(sys.executable, tools / "build_pxa_container.py", package, key, output)
    return output


def main() -> None:
    verifier = Path(sys.argv[1]).resolve()
    tools = Path(sys.argv[2]).resolve()
    legacy_enabled = len(sys.argv) > 3 and sys.argv[3] == "1"
    sys.path.insert(0, str(tools))
    from build_pxa_container import p1363_signature, STREAM_DOMAIN
    with tempfile.TemporaryDirectory(prefix="pxa-lineage-test-") as temporary:
        root = Path(temporary)
        old_key = root / "old.pem"
        new_key = root / "new.pem"
        old_public = root / "old.der"
        run("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
            "ec_paramgen_curve:P-256", "-out", old_key)
        run("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
            "ec_paramgen_curve:P-256", "-out", new_key)
        run("openssl", "pkey", "-in", old_key, "-pubout", "-outform", "DER",
            "-out", old_public)
        lineage = root / "publisher-lineage.pxkl"
        run(sys.executable, tools / "build_publisher_lineage.py",
            "pxa-rotation-test", old_key, new_key, lineage)

        rotated_meta = root / "rotated.json"
        metadata(rotated_meta, 2, lineage.name)
        rotated = signed_package(root, tools, new_key, rotated_meta,
                                 root / "rotated.pxa")
        storage_ok = root / "storage-ok"
        storage_ok.mkdir()
        run(verifier, old_public, storage_ok, rotated)
        # All public builders now default to the low-memory DEFLATE profile.
        header = rotated.read_bytes()[:64]
        assert struct.unpack_from("<HH", header, 4) == (0, 2)
        assert struct.unpack_from("<HH", header, 12) == (2, 12)
        legacy = root / "legacy.pxa"
        run(sys.executable, tools / "build_pxa_container.py", rotated.with_suffix(""),
            new_key, legacy, "--legacy")
        assert struct.unpack_from("<HH", legacy.read_bytes(), 4) == (0, 1)
        assert struct.unpack_from("<HH", legacy.read_bytes(), 12) == (1, 12)
        legacy_storage = root / "storage-legacy"; legacy_storage.mkdir()
        run(verifier, old_public, legacy_storage, legacy, ok=legacy_enabled)
        if not legacy_enabled:
            assert not any(legacy_storage.rglob("current")), "rejected legacy package installed files"
        for args in (("--legacy", "--codec", "deflate"), ("--legacy", "--chunk-size", "32768")):
            invalid = root / "invalid-options.pxa"
            run(sys.executable, tools / "build_pxa_container.py", rotated.with_suffix(""),
                new_key, invalid, *args, ok=False)
            assert not invalid.exists()

        tampered = root / "tampered.pxa"
        tampered.write_bytes(rotated.read_bytes()[:-1] +
                             bytes([rotated.read_bytes()[-1] ^ 1]))
        storage_tampered = root / "storage-tampered"
        storage_tampered.mkdir()
        run(verifier, old_public, storage_tampered, tampered, ok=False)

        # Both full-file and fragmented streaming installs must produce the
        # original decoded inventory with all three bounded block sizes.
        for chunk, codec in ((4096, "lz4"), (16384, "lz4"), (32768, "lz4"), (32768, "store"),
                             (4096, "deflate"), (16384, "deflate"), (32768, "deflate")):
            case = f"{chunk}-{codec}"
            streamable = root / f"stream-{case}.pxa"
            run(sys.executable, tools / "build_pxa_container.py", rotated.with_suffix(""),
                new_key, streamable, "--streamable", "--chunk-size", chunk, "--codec", codec)
            storage = root / f"full-{case}"; storage.mkdir()
            run(verifier, old_public, storage, streamable)
            if sys.platform.startswith("linux") and codec == "deflate":
                os.environ["PXA_TEST_STREAM_FRAGMENT"] = "2048"
                # 4 KiB blocks must remain installable when the old extra
                # input/output/state allocations cannot be satisfied.
                # Larger blocks still fail cleanly if their spill cannot fit.
                denied = (4100, 4096, "inflate") if chunk == 4096 else (chunk + 4, chunk)
                for size in denied:
                    storage = root / f"low-memory-{case}-{size}"; storage.mkdir()
                    # A failed receive must also preserve an installed generation.
                    before = None
                    if chunk != 4096:
                        del os.environ["PXA_TEST_STREAM_FRAGMENT"]
                        run(verifier, old_public, storage, rotated)
                        before = {p.relative_to(storage): hashlib.sha256(p.read_bytes()).digest()
                                  for p in storage.rglob("*") if p.is_file()}
                        os.environ["PXA_TEST_STREAM_FRAGMENT"] = "2048"
                    os.environ["PXA_TEST_STREAM_DENY_ALLOC"] = str(size)
                    # 16 KiB decoded output is also borrowed from the workspace.
                    succeeds = chunk == 4096 or (chunk == 16384 and size == chunk)
                    result = run(verifier, old_public, storage, streamable, ok=succeeds)
                    if not succeeds:
                        assert b"status=-9" in result.stderr, result.stderr  # RESOURCE_LIMIT
                        assert not (storage / "stream-3").exists()
                        after = {p.relative_to(storage): hashlib.sha256(p.read_bytes()).digest()
                                 for p in storage.rglob("*") if p.is_file()}
                        assert before == after
                del os.environ["PXA_TEST_STREAM_DENY_ALLOC"]
                del os.environ["PXA_TEST_STREAM_FRAGMENT"]
            for fragment in (1, 63, 108, 2048, 32773):
                os.environ["PXA_TEST_STREAM_FRAGMENT"] = str(fragment)
                storage = root / f"fragment-{case}-{fragment}"; storage.mkdir()
                run(verifier, old_public, storage, streamable)
                for path in rotated.with_suffix("").rglob("*"):
                    if path.is_file():
                        assert (storage / "stream-3" / path.relative_to(rotated.with_suffix(""))).read_bytes() == path.read_bytes()
            storage = root / f"adopt-{case}"; storage.mkdir()
            os.environ["PXA_TEST_STREAM_ADOPT"] = "1"
            run(verifier, old_public, storage, streamable)
            del os.environ["PXA_TEST_STREAM_ADOPT"]
            assert not (storage / "stream-3").exists()
            storage = root / f"collision-{case}"; storage.mkdir()
            (storage / "stream-3").mkdir(); (storage / "stream-3" / "owned").write_text("keep")
            run(verifier, old_public, storage, streamable, ok=False)
            assert (storage / "stream-3" / "owned").read_text() == "keep"
            raw = streamable.read_bytes()
            prefix = 64 + struct.unpack_from("<I", raw, 16)[0] + 216
            payload = prefix + struct.unpack_from("<I", raw, 56)[0]
            malformed = {
                "index": raw[:prefix + 16] + bytes([raw[prefix + 16] ^ 1]) + raw[prefix + 17:],
                "block": raw[:-1] + bytes([raw[-1] ^ 1]),
                "record": raw[:payload] + b"\xff" + raw[payload + 1:],
                "truncated": raw[:-1], "extra": raw + b"x", "prefix-only": raw[:payload],
            }
            # Correct signatures do not authorize noncanonical index offsets
            # or sizes, nor invalid compressed blocks with a valid encoded hash.
            for name, mutate in (("offset", lambda data: struct.pack_into("<Q", data, prefix, 0xffffffffffffffff)),
                                 ("index-length", lambda data: struct.pack_into("<I", data, prefix + 8, 0xffffffff)),
                                 ("index-reserved", lambda data: struct.pack_into("<I", data, prefix + 12, 1))):
                changed = bytearray(raw); mutate(changed)
                message = hashlib.sha256(changed[:prefix-108] + changed[prefix:payload]).digest()
                changed[prefix-64:prefix] = p1363_signature(str(new_key), STREAM_DOMAIN + message)
                malformed[name] = bytes(changed)
            changed = bytearray(raw); changed[-1] ^= 1
            entry = payload - 48
            relative, length = struct.unpack_from("<QI", changed, entry)
            changed[entry+16:entry+48] = hashlib.sha256(changed[payload+relative:payload+relative+length]).digest()
            changed[prefix-64:prefix] = p1363_signature(str(new_key), STREAM_DOMAIN +
                hashlib.sha256(changed[:prefix-108] + changed[prefix:payload]).digest())
            malformed["signed-bad-decoded-file"] = bytes(changed)
            if codec == "deflate":
                # Authenticate a malformed compressed body. This must reach
                # the bounded decoder (not just fail the encoded-block hash).
                changed = bytearray(raw)
                relative, length = struct.unpack_from("<QI", changed, prefix)
                block_at = payload + relative
                stored, decoded = struct.unpack_from("<HH", changed, block_at)
                assert stored < decoded
                changed[block_at + 4:block_at + length] = b"\x07" * stored  # reserved BTYPE
                changed[prefix + 16:prefix + 48] = hashlib.sha256(changed[block_at:block_at + length]).digest()
                changed[prefix - 64:prefix] = p1363_signature(str(new_key), STREAM_DOMAIN +
                    hashlib.sha256(changed[:prefix - 108] + changed[prefix:payload]).digest())
                malformed["signed-invalid-deflate"] = bytes(changed)
            for name, value in malformed.items():
                broken = root / f"{name}-{case}.pxa"; broken.write_bytes(value)
                storage = root / f"bad-{name}-{case}"; storage.mkdir()
                run(verifier, old_public, storage, broken, ok=False)
                assert not (storage / "stream-3").exists(), name
            os.environ["PXA_TEST_STREAM_WRITE_LIMIT"] = "4096"
            storage = root / f"disk-write-error-{case}"; storage.mkdir()
            run(verifier, old_public, storage, streamable, ok=False)
            assert not (storage / "stream-3").exists()
            del os.environ["PXA_TEST_STREAM_WRITE_LIMIT"]
            del os.environ["PXA_TEST_STREAM_FRAGMENT"]

        old_meta = root / "old.json"
        metadata(old_meta, 1, None)
        old = signed_package(root, tools, old_key, old_meta, root / "old.pxa")
        storage_rollback = root / "storage-rollback"
        storage_rollback.mkdir()
        run(verifier, old_public, storage_rollback, rotated, old, ok=False)

        rollback_lineage = root / "publisher-lineage-rollback.pxkl"
        run(sys.executable, tools / "build_publisher_lineage.py",
            "pxa-rotation-test", old_key, new_key, rollback_lineage,
            "--keep-old-signer")
        rollback_meta = root / "rollback-allowed.json"
        metadata(rollback_meta, 2, rollback_lineage.name)
        rollback_allowed = signed_package(
            root, tools, new_key, rollback_meta, root / "rollback-allowed.pxa")
        storage_allowed = root / "storage-rollback-allowed"
        storage_allowed.mkdir()
        run(verifier, old_public, storage_allowed, rollback_allowed, old)


if __name__ == "__main__":
    main()

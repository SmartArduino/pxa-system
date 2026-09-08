#!/usr/bin/env python3
"""Create or extend a PXA publisher-key lineage (PXKL)."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import subprocess
from pathlib import Path

from build_package_manifest import (
    P256_ORDER,
    PackageError,
    openssl,
    parse_ecdsa_signature,
    require,
    validate_p256_public_key,
)


SAFE_ID = re.compile(r"[a-z][a-z0-9._-]{0,63}")
DOMAIN = b"PXA-PUBLISHER-KEY-ROTATION\0"


def sign(private_key: str, message: bytes) -> bytes:
    der = openssl(["dgst", "-sha256", "-sign", private_key], message)
    r, s = parse_ecdsa_signature(der)
    require(0 < r < P256_ORDER and 0 < s < P256_ORDER, "invalid signature")
    if s > P256_ORDER // 2:
        s = P256_ORDER - s
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def key(private_key: str) -> tuple[bytes, bytes]:
    spki = openssl(["pkey", "-in", private_key, "-pubout", "-outform", "DER"])
    validate_p256_public_key(spki)
    return spki, hashlib.sha256(spki).digest()


def parse_existing(path: str | None) -> tuple[bytes, int, bytes | None]:
    if path is None:
        return b"", 0, None
    data = Path(path).read_bytes()
    require(len(data) >= 16 and data[:4] == b"PXKL", "invalid existing PXKL")
    major, minor, header_size, count, body_size = struct.unpack_from("<HHHHI", data, 4)
    require((major, minor, header_size) == (0, 1, 16), "unsupported existing PXKL")
    require(0 < count < 8 and body_size == len(data) - 16, "invalid existing PXKL size")
    offset = 16
    final_key_id = None
    for generation in range(1, count + 1):
        require(len(data) - offset >= 80, "truncated existing PXKL")
        actual_generation, flags = struct.unpack_from("<II", data, offset)
        spki_size, signature_size = struct.unpack_from("<HH", data, offset + 72)
        require(actual_generation == generation and flags & ~1 == 0 and
                0 < spki_size <= 160 and signature_size == 64 and
                struct.unpack_from("<I", data, offset + 76)[0] == 0,
                "invalid existing PXKL link")
        link_size = 80 + spki_size + 64
        require(link_size <= len(data) - offset, "truncated existing PXKL link")
        final_key_id = data[offset + 40:offset + 72]
        offset += link_size
    require(offset == len(data), "trailing existing PXKL data")
    return data[16:], count, final_key_id


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("app_id")
    parser.add_argument("old_private_key")
    parser.add_argument("new_private_key")
    parser.add_argument("output")
    parser.add_argument("--extend", metavar="PXKL")
    parser.add_argument("--keep-old-signer", action="store_true")
    args = parser.parse_args()
    require(SAFE_ID.fullmatch(args.app_id) is not None, "invalid App ID")
    old_spki, old_key_id = key(args.old_private_key)
    new_spki, new_key_id = key(args.new_private_key)
    require(old_key_id != new_key_id, "old and new public keys are identical")
    body, previous_count, previous_final = parse_existing(args.extend)
    require(previous_final is None or previous_final == old_key_id,
            "old key does not continue the existing lineage")
    generation = previous_count + 1
    flags = 0 if args.keep_old_signer else 1
    root_key_id = old_key_id if previous_count == 0 else body[8:40]
    app_id = args.app_id.encode("ascii")
    message = (root_key_id + struct.pack("<H", len(app_id)) + app_id +
               struct.pack("<II", generation, flags) + old_key_id + new_key_id +
               struct.pack("<H", len(new_spki)) + new_spki)
    signature = sign(args.old_private_key, DOMAIN + message)
    link = (struct.pack("<II", generation, flags) + old_key_id + new_key_id +
            struct.pack("<HHI", len(new_spki), 64, 0) + new_spki + signature)
    body += link
    output = b"PXKL" + struct.pack("<HHHHI", 0, 1, 16, generation, len(body)) + body
    destination = Path(args.output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.tmp-{os.getpid()}")
    try:
        temporary.write_bytes(output)
        os.replace(temporary, destination)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    print(f"{destination} generation={generation} root={root_key_id.hex()} signer={new_key_id.hex()}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, PackageError) as error:
        raise SystemExit(f"error: {error}") from error

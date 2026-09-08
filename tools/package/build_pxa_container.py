#!/usr/bin/env python3
"""Build the canonical single-file PXA container from a signed Package."""

from __future__ import annotations

import argparse
import binascii
import ctypes
import ctypes.util
import hashlib
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from build_package_manifest import (
    P256_ORDER,
    PackageError,
    openssl,
    parse_ecdsa_signature,
    require,
)


HEADER_SIZE = 64
SIGNATURE_SIZE = 108
CHUNK_SIZE = 4096
CONTAINER_DOMAIN = b"PXA-PACKAGE-CONTAINER-DIGEST\0"


def records(data: bytes):
    offset = 0
    while offset < len(data):
        require(len(data) - offset >= 4, "truncated manifest record")
        tag, size = struct.unpack_from("<HH", data, offset)
        offset += 4
        require(size <= len(data) - offset, "truncated manifest payload")
        yield tag & 0x7FFF, data[offset:offset + size]
        offset += size


def manifest_files(manifest: bytes) -> list[tuple[str, int, bytes]]:
    require(len(manifest) >= 12 and manifest[:4] == b"PXAM", "invalid manifest")
    require(struct.unpack_from("<I", manifest, 8)[0] == len(manifest) - 12,
            "invalid manifest size")
    files = []
    for tag, payload in records(manifest[12:]):
        if tag != 17:
            continue
        fields = {nested_tag: value for nested_tag, value in records(payload)}
        require(set(fields) == {1, 2, 3}, "invalid manifest file record")
        require(len(fields[2]) == 8 and len(fields[3]) == 32,
                "invalid manifest file metadata")
        try:
            path = fields[1].decode("ascii")
        except UnicodeDecodeError as error:
            raise PackageError("non-ASCII package path") from error
        files.append((path, struct.unpack("<Q", fields[2])[0], fields[3]))
    return files


class Lz4:
    def __init__(self) -> None:
        library = ctypes.util.find_library("lz4")
        require(library is not None, "liblz4 is required to build codec 1")
        self.library = ctypes.CDLL(library)
        self.library.LZ4_compressBound.argtypes = [ctypes.c_int]
        self.library.LZ4_compressBound.restype = ctypes.c_int
        self.library.LZ4_compress_default.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.library.LZ4_compress_default.restype = ctypes.c_int

    def compress(self, source: bytes) -> bytes:
        bound = self.library.LZ4_compressBound(len(source))
        target = ctypes.create_string_buffer(bound)
        source_buffer = ctypes.create_string_buffer(source, len(source))
        size = self.library.LZ4_compress_default(
            source_buffer, target, len(source), bound)
        require(size > 0, "LZ4 compression failed")
        return target.raw[:size]


def p1363_signature(private_key: str, message: bytes) -> bytes:
    signature_der = openssl(["dgst", "-sha256", "-sign", private_key], message)
    r, s = parse_ecdsa_signature(signature_der)
    require(0 < r < P256_ORDER and 0 < s < P256_ORDER,
            "invalid ECDSA signature")
    if s > P256_ORDER // 2:
        s = P256_ORDER - s
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def encode_payload(package_dir: Path, files, payload_file, codec: int) -> tuple[int, int]:
    lz4 = Lz4() if codec == 1 else None
    payload_size = 0
    unpacked_size = 0
    for index, (relative, expected_size, expected_digest) in enumerate(files):
        source_path = package_dir / relative
        require(source_path.is_file(), f"missing package file: {relative}")
        content = source_path.read_bytes()
        require(len(content) == expected_size, f"size mismatch: {relative}")
        require(hashlib.sha256(content).digest() == expected_digest,
                f"digest mismatch: {relative}")
        chunks = [content[offset:offset + CHUNK_SIZE]
                  for offset in range(0, len(content), CHUNK_SIZE)]
        encoded_chunks = []
        encoded_size = 0
        for chunk in chunks:
            compressed = lz4.compress(chunk) if lz4 is not None else chunk
            stored = compressed if len(compressed) < len(chunk) else chunk
            encoded_chunks.append((stored, len(chunk)))
            encoded_size += 4 + len(stored)
        payload_file.write(struct.pack("<HHIQ", index, 0, len(chunks), encoded_size))
        payload_size += 16
        for stored, decoded_size in encoded_chunks:
            payload_file.write(struct.pack("<HH", len(stored), decoded_size))
            payload_file.write(stored)
            payload_size += 4 + len(stored)
        unpacked_size += len(content)
    return payload_size, unpacked_size


def build(package_dir: Path, private_key: str, output_path: Path, codec: int) -> None:
    manifest = (package_dir / "manifest.pxm").read_bytes()
    package_signature = (package_dir / "signature.pxs").read_bytes()
    require(len(package_signature) == SIGNATURE_SIZE and
            package_signature[:4] == b"PXAS", "invalid Package signature")
    files = manifest_files(manifest)
    publisher_key_id = package_signature[8:40]

    with tempfile.TemporaryFile() as payload:
        payload_size, unpacked_size = encode_payload(
            package_dir, files, payload, codec)
        container_size = (HEADER_SIZE + len(manifest) + len(package_signature) +
                          SIGNATURE_SIZE + payload_size)
        header = bytearray(HEADER_SIZE)
        struct.pack_into("<4sHHHHHHIII IQQQI", header, 0,
                         b"PXAC", 0, 1, HEADER_SIZE, 0, codec, 12,
                         len(manifest), len(package_signature), SIGNATURE_SIZE,
                         len(files), payload_size, unpacked_size, container_size, 0)
        struct.pack_into("<I", header, 60, binascii.crc32(header[:60]) & 0xFFFFFFFF)

        material_hash = hashlib.sha256()
        material_hash.update(header)
        material_hash.update(manifest)
        material_hash.update(package_signature)
        payload.seek(0)
        while chunk := payload.read(64 * 1024):
            material_hash.update(chunk)
        signature = p1363_signature(
            private_key, CONTAINER_DOMAIN + material_hash.digest())
        envelope = (b"PXCS" + struct.pack("<HH", 0x0001, 1) + publisher_key_id +
                    struct.pack("<HH", 64, 0) + signature)

        output_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = output_path.with_name(f".{output_path.name}.tmp-{os.getpid()}")
        try:
            with temporary.open("wb") as output:
                output.write(header)
                output.write(manifest)
                output.write(package_signature)
                output.write(envelope)
                payload.seek(0)
                while chunk := payload.read(64 * 1024):
                    output.write(chunk)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, output_path)
        finally:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
    print(f"{output_path} files={len(files)} packed={container_size} unpacked={unpacked_size}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package_dir")
    parser.add_argument("private_key")
    parser.add_argument("output")
    parser.add_argument("--codec", choices=("store", "lz4"), default="lz4")
    arguments = parser.parse_args()
    build(Path(arguments.package_dir).resolve(), arguments.private_key,
          Path(arguments.output).resolve(), 1 if arguments.codec == "lz4" else 0)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, PackageError) as error:
        raise SystemExit(f"error: {error}") from error

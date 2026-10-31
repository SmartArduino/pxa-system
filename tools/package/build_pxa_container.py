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
import zlib
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
STREAM_DOMAIN = b"PXA-PACKAGE-STREAM-INDEX\0"


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
    def __init__(self, level: int = 9) -> None:
        private = Path(__file__).resolve().parents[2] / "runtime/lib/liblz4.so.1"
        library = str(private) if private.is_file() else ctypes.util.find_library("lz4")
        require(library is not None, "liblz4 is required to build codec 1")
        self.library = ctypes.CDLL(library)
        self.library.LZ4_compressBound.argtypes = [ctypes.c_int]
        self.library.LZ4_compressBound.restype = ctypes.c_int
        self.level = level
        self.library.LZ4_compress_HC.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.library.LZ4_compress_HC.restype = ctypes.c_int
        self.library.LZ4_compress_default.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.library.LZ4_compress_default.restype = ctypes.c_int

    def compress(self, source: bytes) -> bytes:
        bound = self.library.LZ4_compressBound(len(source))
        target = ctypes.create_string_buffer(bound)
        source_buffer = ctypes.create_string_buffer(source, len(source))
        size = (self.library.LZ4_compress_HC(source_buffer, target, len(source), bound, self.level)
                if self.level else self.library.LZ4_compress_default(
                    source_buffer, target, len(source), bound))
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


def encode_payload(package_dir: Path, files, payload_file, codec: int,
                   chunk_size: int = CHUNK_SIZE, index_file=None) -> tuple[int, int]:
    lz4 = Lz4() if codec == 1 else None
    payload_size = 0
    unpacked_size = 0
    for index, (relative, expected_size, expected_digest) in enumerate(files):
        source_path = package_dir / relative
        require(source_path.is_file(), f"missing package file: {relative}")
        digest = hashlib.sha256()
        encoded_size = 0
        decoded_size = 0
        chunk_count = 0
        record_offset = payload_file.tell()
        payload_file.write(bytes(16))
        with source_path.open("rb") as source:
            while chunk := source.read(chunk_size):
                digest.update(chunk)
                if codec == 2:
                    compressor = zlib.compressobj(level=9, wbits=-15)
                    compressed = compressor.compress(chunk) + compressor.flush()
                else:
                    compressed = lz4.compress(chunk) if lz4 is not None else chunk
                stored = compressed if len(compressed) < len(chunk) else chunk
                encoded = struct.pack("<HH", len(stored), len(chunk)) + stored
                if index_file is not None:
                    index_file.write(struct.pack("<QI4x32s", payload_file.tell(),
                                                len(encoded), hashlib.sha256(encoded).digest()))
                payload_file.write(encoded)
                encoded_size += 4 + len(stored)
                decoded_size += len(chunk)
                chunk_count += 1
        require(decoded_size == expected_size, f"size mismatch: {relative}")
        require(digest.digest() == expected_digest, f"digest mismatch: {relative}")
        end_offset = payload_file.tell()
        payload_file.seek(record_offset)
        payload_file.write(struct.pack("<HHIQ", index, 0, chunk_count, encoded_size))
        payload_file.seek(end_offset)
        payload_size += 16 + encoded_size
        unpacked_size += decoded_size
    return payload_size, unpacked_size


def build(package_dir: Path, private_key: str, output_path: Path, codec: int,
          streamable: bool = True, chunk_size: int | None = None) -> None:
    chunk_size = chunk_size if chunk_size is not None else CHUNK_SIZE
    require(chunk_size in (4096, 16384, 32768), "unsupported block size")
    require(streamable or chunk_size == CHUNK_SIZE, "large blocks require stream format")
    require(codec in (0, 1, 2), "unsupported codec")
    require(codec != 2 or streamable, "DEFLATE requires --streamable (Container 0.2)")
    manifest = (package_dir / "manifest.pxm").read_bytes()
    package_signature = (package_dir / "signature.pxs").read_bytes()
    require(len(package_signature) == SIGNATURE_SIZE and
            package_signature[:4] == b"PXAS", "invalid Package signature")
    files = manifest_files(manifest)
    publisher_key_id = package_signature[8:40]

    with tempfile.TemporaryFile() as payload, tempfile.TemporaryFile() as index:
        payload_size, unpacked_size = encode_payload(
            package_dir, files, payload, codec, chunk_size, index if streamable else None)
        index_size = index.tell()
        container_size = (HEADER_SIZE + len(manifest) + len(package_signature) +
                          SIGNATURE_SIZE + index_size + payload_size)
        header = bytearray(HEADER_SIZE)
        struct.pack_into("<4sHHHHHHIII IQQQI", header, 0,
                         b"PXAC", 0, 2 if streamable else 1, HEADER_SIZE, 0, codec, chunk_size.bit_length() - 1,
                         len(manifest), len(package_signature), SIGNATURE_SIZE,
                         len(files), payload_size, unpacked_size, container_size, index_size)
        struct.pack_into("<I", header, 60, binascii.crc32(header[:60]) & 0xFFFFFFFF)

        material_hash = hashlib.sha256()
        material_hash.update(header)
        material_hash.update(manifest)
        material_hash.update(package_signature)
        signed_body = index if streamable else payload
        signed_body.seek(0)
        while chunk := signed_body.read(64 * 1024):
            material_hash.update(chunk)
        signature = p1363_signature(
            private_key, (STREAM_DOMAIN if streamable else CONTAINER_DOMAIN) + material_hash.digest())
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
                index.seek(0)
                while chunk := index.read(64 * 1024):
                    output.write(chunk)
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


def add_container_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--codec", choices=("store", "lz4", "deflate"),
                        help="defaults to deflate, or lz4 with --legacy")
    formats = parser.add_mutually_exclusive_group()
    formats.add_argument("--streamable", dest="streamable", action="store_true", default=True,
                         help="Container 0.2 (default): authenticate blocks before decoding")
    formats.add_argument("--legacy", dest="streamable", action="store_false",
                         help="Container 0.1 for older firmware; defaults to LZ4")
    parser.add_argument("--chunk-size", type=int, choices=(4096, 16384, 32768),
                        default=CHUNK_SIZE, help="decoded block bytes (default: 4096)")


def container_codec(arguments: argparse.Namespace) -> int:
    codec = arguments.codec or ("deflate" if arguments.streamable else "lz4")
    require(arguments.streamable or codec != "deflate", "DEFLATE requires Container 0.2; omit --legacy")
    require(arguments.streamable or arguments.chunk_size == CHUNK_SIZE,
            "Container 0.1 requires --chunk-size 4096")
    return {"store": 0, "lz4": 1, "deflate": 2}[codec]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package_dir")
    parser.add_argument("private_key")
    parser.add_argument("output")
    add_container_arguments(parser)
    arguments = parser.parse_args()
    build(Path(arguments.package_dir).resolve(), arguments.private_key,
          Path(arguments.output).resolve(), container_codec(arguments),
          arguments.streamable, arguments.chunk_size)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, PackageError) as error:
        raise SystemExit(f"error: {error}") from error

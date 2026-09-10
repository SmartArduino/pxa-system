#!/usr/bin/env python3
"""Run a deterministic simulator case and validate its RGBA PNG output."""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


def decode_rgba(path: Path, expected_width: int, expected_height: int) -> bytes:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    offset = 8
    compressed = bytearray()
    width = height = 0
    while offset < len(data):
        size = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + size]
        offset += 12 + size
        if kind == b"IHDR":
            width, height, depth, color_type = struct.unpack(">IIBB", payload[:10])
            if depth != 8 or color_type != 6:
                raise ValueError("expected 8-bit RGBA PNG")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if (width, height) != (expected_width, expected_height):
        raise ValueError(f"unexpected size {width}x{height}")
    raw = zlib.decompress(compressed)
    stride = width * 4
    previous = bytearray(stride)
    pixels = bytearray()
    cursor = 0
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor : cursor + stride])
        cursor += stride
        for index in range(stride):
            left = row[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                row[index] = (row[index] + left) & 0xFF
            elif filter_type == 2:
                row[index] = (row[index] + above) & 0xFF
            elif filter_type == 3:
                row[index] = (row[index] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above),
                             abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
                row[index] = (row[index] + predictor) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        pixels.extend(row)
        previous = row
    return bytes(pixels)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("args", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    args = options.args[1:] if options.args[:1] == ["--"] else options.args
    with tempfile.TemporaryDirectory(prefix="pxsys-visual-") as directory:
        screenshot = Path(directory) / "screen.png"
        environment = os.environ.copy()
        environment.update({"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"})
        command = [options.simulator, *args, "--duration-ms", "250",
                   "--screenshot", str(screenshot)]
        subprocess.run(command, env=environment, check=True)
        pixels = decode_rgba(screenshot, options.width, options.height)
        sampled = {pixels[index : index + 4] for index in range(0, len(pixels), 64)}
        if len(sampled) < 8 or not any(pixel[3] for pixel in sampled):
            raise ValueError("screenshot is blank or visually degenerate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

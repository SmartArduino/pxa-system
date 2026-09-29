#!/usr/bin/env python3
"""Compile optional resources.json, then index package assets before signing.

PXR1 files contain native INDEX8/RGB565 raster resources or RGB565/BGRA8888
UI pixels. The index describes independently readable files. Integrity is
verified during installation; runtime does not hash resources or block tables.
"""
import argparse
import json
import re
import struct
from pathlib import Path

INDEX_PATH = "assets/resources.pxi"
FILE_HEADER = struct.Struct("<4sHHHHHHIIQ")
RECORD = struct.Struct("<HBBHHIIHHI")
SAFE_PATH = re.compile(r"[A-Za-z0-9._-]+(?:/[A-Za-z0-9._-]+)*")


class ResourceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ResourceError(message)


def checked_path(root, value, *, asset=False):
    require(isinstance(value, str) and len(value) <= 255 and
            SAFE_PATH.fullmatch(value) and
            all(p not in (".", "..") for p in value.split("/")),
            f"invalid resource path: {value!r}")
    require(not asset or value.startswith("assets/"), "resource must be under assets/")
    root = root.resolve()
    current = root
    for part in value.split("/"):
        current /= part
        require(not current.is_symlink(), f"resource symlink rejected: {value}")
    require(current.resolve().is_relative_to(root), "resource escapes package")
    return current


def palette_words(path):
    values = json.loads(path.read_text())
    require(isinstance(values, list) and 256 <= len(values) <= 65536 and
            len(values) % 256 == 0 and
            all(type(v) is int and 0 <= v <= 65535 for v in values),
            "palette JSON must contain 256..65536 RGB565 integers in complete rows")
    return values


def make_pxr(kind, width, height, payload):
    encoding = 1 if kind == 1 else 2
    return FILE_HEADER.pack(b"PXR1", 1, 0, kind, encoding, width, height,
                            len(payload), FILE_HEADER.size, 0) + payload


def compile_declared(app, package):
    config_path = app / "resources.json"
    if not config_path.exists():
        return
    require(not config_path.is_symlink(), "resources.json cannot be a symlink")
    config = json.loads(config_path.read_text())
    require(isinstance(config, dict) and set(config) == {"assets"} and
            isinstance(config["assets"], list), "expected resources.json assets list")
    outputs = set()
    for item in config["assets"]:
        require(isinstance(item, dict) and
                set(item) <= {"path", "kind", "source", "palette", "width", "height", "encoding"},
                "invalid resource declaration")
        output = checked_path(package, item.get("path"), asset=True)
        require(output.suffix == ".pxr" and str(output) not in outputs and
                not output.exists(), "duplicate or colliding compiled resource")
        outputs.add(str(output))
        source = checked_path(app, item.get("source"))
        require(source.is_file(), f"missing resource source: {source}")
        kind = item.get("kind")
        if kind == "palette":
            require(set(item) == {"path", "kind", "source"}, "invalid palette fields")
            words = palette_words(source)
            data = make_pxr(2, 256, len(words) // 256,
                            struct.pack(f"<{len(words)}H", *words))
        elif kind == "index8":
            require(set(item) == {"path", "kind", "source", "width", "height"},
                    "INDEX8 requires width and height")
            width, height = item["width"], item["height"]
            require(type(width) is int and type(height) is int and
                    0 < width <= 256 and 0 < height <= 256, "invalid texture dimensions")
            require(source.stat().st_size == width * height, "INDEX8 length mismatch")
            data = make_pxr(1, width, height, source.read_bytes())
        elif kind == "image":
            require(set(item) <= {"path", "kind", "source", "encoding", "width", "height"},
                    "invalid image fields")
            encoding = item.get("encoding", "bgra8888")
            require(encoding in ("rgb565", "bgra8888", "bgra8888-premultiplied"),
                    "unsupported UI image encoding")
            if encoding == "bgra8888-premultiplied":
                require(set(item) == {"path", "kind", "source", "encoding", "width", "height"},
                        "premultiplied UI image requires explicit dimensions")
                width, height = item["width"], item["height"]
                require(type(width) is int and type(height) is int and
                        0 < width <= 4096 and 0 < height <= 4096 and
                        source.suffix == ".bgra", "invalid premultiplied UI source")
                payload = source.read_bytes()
                require(len(payload) == width * height * 4,
                        "premultiplied UI image length mismatch")
                require(all(b <= a and g <= a and r <= a
                            for b, g, r, a in struct.iter_unpack("4B", payload)),
                        "premultiplied UI channels exceed alpha")
                encoded = 8
            else:
                require(set(item) <= {"path", "kind", "source", "encoding"},
                        "RGB565/BGRA8888 image dimensions come from source PNG")
                from PIL import Image
                with Image.open(source) as image:
                    require(0 < image.width <= 4096 and 0 < image.height <= 4096,
                            "UI image dimensions exceed 4096x4096")
                    rgba = image.convert("RGBA")
                    width, height = image.width, image.height
                    if encoding == "rgb565":
                        require(rgba.getchannel("A").getextrema() == (255, 255),
                                "RGB565 UI images must be opaque; use bgra8888 for alpha")
                        payload = bytearray()
                        for r, g, b, _ in struct.iter_unpack("4B", rgba.tobytes()):
                            payload += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                        encoded = 2
                    else:
                        payload = rgba.tobytes("raw", "BGRA")
                        encoded = 7
            data = FILE_HEADER.pack(b"PXR1", 1, 0, 4, encoded,
                width, height, len(payload), FILE_HEADER.size, 0) + payload
        elif kind == "texture":
            require(set(item) == {"path", "kind", "source", "palette"},
                    "image texture requires an explicit shared RGB565 palette")
            from PIL import Image
            words = palette_words(checked_path(app, item["palette"]))[:256]
            colors = [((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63,
                       (v & 31) * 255 // 31) for v in words]
            with Image.open(source) as image:
                require(0 < image.width <= 256 and 0 < image.height <= 256,
                        "texture dimensions exceed 256x256")
                rgba = image.convert("RGBA")
                # Exact deterministic mapping. No dithering; transparent pixels
                # use index 0, partial alpha requires an explicit coverage atlas.
                cache = {}
                indices = bytearray()
                for r, g, b, a in struct.iter_unpack("4B", rgba.tobytes()):
                    require(a in (0, 255), "partial alpha requires a coverage atlas")
                    if a == 0:
                        indices.append(0)
                    else:
                        rgb = (r, g, b)
                        if rgb not in cache:
                            cache[rgb] = min(range(1, 256), key=lambda i:
                                sum((x-y)**2 for x, y in zip(rgb, colors[i])))
                        indices.append(cache[rgb])
                data = make_pxr(1, image.width, image.height, indices)
        else:
            raise ResourceError(f"unsupported compiled resource kind: {kind}")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)


def describe(path):
    size = path.stat().st_size
    require(size <= 0xffffffff, "resource file exceeds format limit")
    with path.open("rb") as stream:
        header = stream.read(512)
    # kind, encoding, width, height, stored, decoded, version, payload_offset
    if path.suffix == ".pxr":
        require(len(header) >= FILE_HEADER.size, "truncated PXR1 header")
        magic, major, minor, kind, enc, width, height, decoded, offset, reserved = (
            FILE_HEADER.unpack_from(header))
        require(magic == b"PXR1" and (major, minor) == (1, 0) and reserved == 0,
                "unsupported or malformed PXR1 header")
        require(offset == 32 and kind in (1, 2, 4), "invalid PXR1 kind/offset")
        require((kind == 1 and enc == 1 and 0 < width <= 256 and 0 < height <= 256) or
                (kind == 2 and enc == 2 and width == 256 and 0 < height <= 256) or
                (kind == 4 and enc in (2, 7, 8) and 0 < width <= 4096 and 0 < height <= 4096),
                "invalid PXR1 encoding or dimensions")
        pixel_bytes = 1 if enc == 1 else 2 if enc == 2 else 4
        require(decoded == width * height * pixel_bytes and
                size == decoded + offset, "PXR1 size mismatch")
        return kind, enc, width, height, size, decoded, 1, offset
    if path.suffix.lower() == ".png":
        require(len(header) >= 33 and header[:8] == b"\x89PNG\r\n\x1a\n" and
                header[8:16] == b"\x00\x00\x00\rIHDR", "invalid PNG header")
        width, height = struct.unpack_from(">II", header, 16)
        require(0 < width <= 65535 and 0 < height <= 65535 and
                width * height * 4 <= 0xffffffff, "PNG dimensions exceed format limit")
        return 4, 6, width, height, size, width * height * 4, 0, 0
    if path.suffix.lower() == ".ogg":
        require(header[:4] == b"OggS", "invalid Ogg header")
        encoding = 3 if b"OpusHead" in header else 4 if b"\x01vorbis" in header else 0
        require(encoding != 0, "unsupported Ogg codec")
        return 3, encoding, 0, 0, size, 0, 0, 0
    if path.suffix.lower() == ".pcm" and 0 < size <= 16000:
        return 3, 5, 0, 0, size, size, 0, 0
    return 5, 0, 0, 0, size, size, 0, 0


def build_index(package):
    directory = checked_path(package, "assets")
    if not directory.exists():
        return None
    entries = []
    for path in sorted(directory.rglob("*")):
        relative = path.relative_to(package).as_posix()
        checked_path(package, relative, asset=True)
        if not path.is_file() or relative == INDEX_PATH:
            continue
        name = relative.encode("ascii")
        kind, encoding, width, height, stored, decoded, version, offset = describe(path)
        entries.append(RECORD.pack(len(name), kind, encoding, width, height,
                                   stored, decoded, version, 0, offset) +
                       name + bytes((-len(name)) % 4))
    content = b"".join(entries)
    result = struct.pack("<4sHHII", b"PXRI", 1, 3, len(entries), len(content) + 16) + content
    checked_path(package, INDEX_PATH, asset=True).write_bytes(result)
    return result


def compile_resources(app, package):
    app, package = app.resolve(), package.resolve()
    compile_declared(app, package)
    return build_index(package)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    try:
        compile_resources(args.app, args.package)
    except (ResourceError, OSError, ValueError) as error:
        parser.exit(1, f"resource build failed: {error}\n")


if __name__ == "__main__":
    main()

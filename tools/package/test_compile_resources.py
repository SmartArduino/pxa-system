#!/usr/bin/env python3
import json
import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from compile_resources import (FILE_HEADER, RECORD, ResourceError,
                               build_index, compile_resources, make_pxr)


class ResourcesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app, self.package = self.root / "app", self.root / "package"
        self.app.mkdir()
        (self.package / "assets").mkdir(parents=True)

    def declare(self, entries):
        (self.app / "resources.json").write_text(json.dumps({"assets": entries}))

    def test_raw_and_palette_compile_and_index(self):
        (self.app / "pixels.bin").write_bytes(bytes([0, 1, 3, 3]))
        (self.app / "colors.json").write_text(json.dumps([i * 251 for i in range(256)]))
        self.declare([
            {"path": "assets/t.pxr", "kind": "index8", "source": "pixels.bin",
             "width": 2, "height": 2},
            {"path": "assets/p.pxr", "kind": "palette", "source": "colors.json"},
        ])
        result = compile_resources(self.app, self.package)
        self.assertEqual(FILE_HEADER.unpack_from((self.package / "assets/t.pxr").read_bytes()),
                         (b"PXR1", 1, 0, 1, 1, 2, 2, 4, 32, 0))
        self.assertEqual(struct.unpack_from("<4sHHII", result), (b"PXRI", 1, 3, 2, len(result)))
        self.assertEqual(RECORD.unpack_from(result, 16), (12, 2, 2, 256, 1, 544, 512, 1, 0, 32))
        self.assertEqual(result[40:52], b"assets/p.pxr")
        self.assertEqual(build_index(self.package), result)  # deterministic, excludes itself

    def test_image_explicit_shared_palette(self):
        from PIL import Image
        image = Image.new("RGBA", (2, 1))
        image.putdata([(255, 0, 0, 255), (0, 0, 0, 0)])
        image.save(self.app / "image.png")
        (self.app / "p.json").write_text(json.dumps([0, 0xf800] + [0] * 254))
        self.declare([{"path": "assets/t.pxr", "kind": "texture", "source": "image.png",
                       "palette": "p.json"}])
        compile_resources(self.app, self.package)
        self.assertEqual((self.package / "assets/t.pxr").read_bytes()[32:], bytes([1, 0]))

    def test_native_ui_image_pixels_and_alpha(self):
        from PIL import Image
        image = Image.new("RGBA", (2, 1))
        image.putdata([(255, 0, 0, 128), (0, 255, 0, 0)])
        image.save(self.app / "image.png")
        self.declare([{"path": "assets/ui.pxr", "kind": "image", "source": "image.png"}])
        result = compile_resources(self.app, self.package)
        data = (self.package / "assets/ui.pxr").read_bytes()
        self.assertEqual(FILE_HEADER.unpack_from(data), (b"PXR1",1,0,4,7,2,1,8,32,0))
        self.assertEqual(data[32:], bytes([0,0,255,128,0,255,0,0]))
        self.assertEqual(RECORD.unpack_from(result,16), (13,4,7,2,1,40,8,1,0,32))
        (self.package / "assets/ui.pxr").unlink()
        self.declare([{"path": "assets/ui.pxr", "kind": "image", "source": "image.png", "encoding": "rgb565"}])
        with self.assertRaisesRegex(ResourceError,"opaque"):
            compile_resources(self.app,self.package)
        image.putdata([(255,0,0,255),(0,255,0,255)])
        image.save(self.app / "image.png")
        compile_resources(self.app,self.package)
        data = (self.package / "assets/ui.pxr").read_bytes()
        self.assertEqual(FILE_HEADER.unpack_from(data), (b"PXR1",1,0,4,2,2,1,4,32,0))
        self.assertEqual(data[32:], struct.pack("<HH",0xf800,0x07e0))

    def test_premultiplied_ui_pixels_are_kept_exact(self):
        source = self.app / "scaled.bgra"
        pixels = bytes([20, 40, 60, 80, 0, 0, 0, 0])
        source.write_bytes(pixels)
        self.declare([{"path": "assets/ui.pxr", "kind": "image",
                       "source": source.name, "encoding": "bgra8888-premultiplied",
                       "width": 2, "height": 1}])
        index = compile_resources(self.app, self.package)
        data = (self.package / "assets/ui.pxr").read_bytes()
        self.assertEqual(FILE_HEADER.unpack_from(data), (b"PXR1", 1, 0, 4, 8, 2, 1, 8, 32, 0))
        self.assertEqual(data[32:], pixels)
        self.assertEqual(RECORD.unpack_from(index, 16)[1:5], (4, 8, 2, 1))
        (self.package / "assets/ui.pxr").unlink()
        source.write_bytes(bytes([81, 40, 60, 80, 0, 0, 0, 0]))
        with self.assertRaisesRegex(ResourceError, "channels exceed alpha"):
            compile_resources(self.app, self.package)

    def test_sources_and_outputs_cannot_escape(self):
        (self.app / "data").write_bytes(b"x")
        for field, value in [("source", "../data"), ("path", "assets/../bad.pxr"),
                             ("path", "/tmp/bad.pxr"), ("path", "assets//bad.pxr")]:
            entry = {"path": "assets/a.pxr", "kind": "index8", "source": "data",
                     "width": 1, "height": 1}
            entry[field] = value
            self.declare([entry])
            with self.subTest(field=field, value=value), self.assertRaises(ResourceError):
                compile_resources(self.app, self.package)

    def test_symlinks_rejected(self):
        (self.package / "assets/escape").symlink_to(self.app, target_is_directory=True)
        with self.assertRaises(ResourceError):
            build_index(self.package)
        (self.package / "assets/escape").unlink()
        (self.app / "source").symlink_to(self.root / "missing")
        self.declare([{"path": "assets/a.pxr", "kind": "index8", "source": "source",
                       "width": 1, "height": 1}])
        with self.assertRaises(ResourceError):
            compile_resources(self.app, self.package)

    def test_corrupt_header_length_version_and_dimensions(self):
        original = make_pxr(1, 3, 2, b"abcd")
        bad = [original[:-1], original + b"x"]
        for offset, value in [(4, 2), (12, 0), (24, 1)]:
            changed = bytearray(original)
            changed[offset] = value
            bad.append(changed)
        for content in bad:
            (self.package / "assets/a.pxr").write_bytes(content)
            with self.assertRaises(ResourceError):
                build_index(self.package)

    def test_stream_and_blob_metadata(self):
        (self.package / "assets/music.ogg").write_bytes(b"OggS" + bytes(24) + b"OpusHead")
        (self.package / "assets/map.bin").write_bytes(b"1234")
        result = build_index(self.package)
        first = RECORD.unpack_from(result, 16)
        self.assertEqual(first[1:8], (5, 0, 0, 0, 4, 4, 0))
        second = RECORD.unpack_from(result, 16 + 24 + 16)
        self.assertEqual(second[1:8], (3, 3, 0, 0, 36, 0, 0))
        self.assertEqual(len(result), 96)

    def test_music_index_has_no_runtime_digests(self):
        for marker in (b"OpusHead", b"\x01vorbis"):
            content = b"OggS" + bytes(24) + marker + bytes(range(251)) * 34
            (self.package / "assets/music.ogg").write_bytes(content)
            result = build_index(self.package)
            start = 16 + RECORD.size + ((len("assets/music.ogg") + 3) & ~3)
            self.assertEqual(result[start:], b"")
            self.assertEqual(build_index(self.package), result)

    def test_blob_metadata_and_empty_file(self):
        content = bytes(range(251)) * 34
        (self.package / "assets/map.bin").write_bytes(content)
        (self.package / "assets/empty.bin").write_bytes(b"")
        result = build_index(self.package)
        empty_path = b"assets/empty.bin"
        offset = 16 + RECORD.size + (len(empty_path) + 3) // 4 * 4
        record = RECORD.unpack_from(result, offset)
        self.assertEqual(record[1], 5)
        self.assertEqual(record[5:7], (len(content), len(content)))
        offset += RECORD.size + (len(b"assets/map.bin") + 3) // 4 * 4
        self.assertEqual(result[offset:], b"")
        self.assertEqual(build_index(self.package), result)


if __name__ == "__main__":
    unittest.main()

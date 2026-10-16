from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import tracemalloc
import unittest

from .build import resource_inputs
from .common import ToolError, digest, identity
from .distribution import extract, fetch, verify_manifest
from .project import Project, SDK_ROOT, TARGETS, aot_flags


class DevelopmentToolsTest(unittest.TestCase):
    def test_native_stack_checks_are_opt_in_and_part_of_the_aot_recipe(self):
        for target in ('esp32s3', 'esp32s31', 'simulator'):
            self.assertEqual(aot_flags({}, target), TARGETS[target][2])
            for enabled in (False, True):
                flags = aot_flags({'build': {'native_stack_checks': enabled}}, target)
                self.assertEqual([f for f in flags if f.startswith('--stack-bounds-checks=')],
                                 ['--stack-bounds-checks=' + str(int(enabled))])
            self.assertNotEqual(identity(aot_flags({'build': {'native_stack_checks': True}}, target)),
                                identity(aot_flags({'build': {'native_stack_checks': False}}, target)))
        for invalid in (None, 1, 'true', []):
            with self.assertRaises(ToolError):
                aot_flags({'build': {'native_stack_checks': invalid}}, 'esp32s3')

    def test_resource_cache_tracks_sources_outside_resource_directories(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'image.index8').write_bytes(b'\x01')
            (root / 'resources.json').write_text(json.dumps({'assets': [
                {'kind': 'index8', 'path': 'assets/image.pxr', 'source': 'image.index8', 'width': 1, 'height': 1}]}))
            before = identity(resource_inputs(root))
            (root / 'image.index8').write_bytes(b'\x02')
            self.assertNotEqual(before, identity(resource_inputs(root)))

    def test_distribution_rejects_path_and_link_escape(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for name, link in [('../escape', None), ('link', '../../escape')]:
                archive = root / 'bad.tar.gz'
                with tarfile.open(archive, 'w:gz') as tar:
                    member = tarfile.TarInfo(name)
                    if link:
                        member.type = tarfile.SYMTYPE
                        member.linkname = link
                    tar.addfile(member)
                with self.assertRaises((ToolError, tarfile.FilterError)):
                    extract(archive, root / 'install')
            self.assertFalse((root.parent / 'escape').exists())

    def test_offline_rejects_network_before_connecting(self):
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaises(ToolError):
                fetch('https://example.invalid/data', Path(td) / 'result', offline=True)

    def test_distribution_signature_detects_changed_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'config').mkdir()
            private = root / 'private.pem'
            public = root / 'config/release-public.pem'
            manifest = root / 'distribution.json'
            signature = root / 'distribution.json.sig'
            manifest.write_text('{"schema":"pxa-distribution-1","version":"0.3.0"}')
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt',
                            'ec_paramgen_curve:P-256', '-out', str(private)], check=True, capture_output=True)
            subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)],
                           check=True, capture_output=True)
            subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(private),
                            '-out', str(signature), str(manifest)], check=True, capture_output=True)
            self.assertEqual(verify_manifest(manifest, signature, root=root)['version'], '0.3.0')
            manifest.write_text('{"schema":"pxa-distribution-1","version":"0.3.1"}')
            with self.assertRaises(ToolError):
                verify_manifest(manifest, signature, root=root)

    def test_duplicate_components_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'package.json').write_text(json.dumps({'id': 'example', 'components': [{'id': 'main'}, {'id': 'main'}]}))
            with self.assertRaises(ToolError):
                Project.open(root)

    def test_container_encoder_streams_large_files_and_detects_mutation(self):
        sys.path.insert(0, str(SDK_ROOT / 'tools/package'))
        from build_pxa_container import encode_payload, CHUNK_SIZE
        from build_package_manifest import PackageError
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            path = root / 'large.bin'
            with path.open('wb') as stream:
                for _ in range(1024):
                    stream.write(bytes(CHUNK_SIZE))
            record = ('large.bin', path.stat().st_size, bytes.fromhex(digest(path)))
            with tempfile.TemporaryFile() as output:
                tracemalloc.start()
                encode_payload(root, [record], output, 0)
                _, peak = tracemalloc.get_traced_memory()
                tracemalloc.stop()
                self.assertLess(peak, 512 * 1024)
                output.seek(0)
                self.assertEqual(struct.unpack('<HHIQ', output.read(16))[2], 1024)
            path.write_bytes(b'changed')
            with tempfile.TemporaryFile() as output, self.assertRaises(PackageError):
                encode_payload(root, [record], output, 0)


if __name__ == '__main__':
    unittest.main()

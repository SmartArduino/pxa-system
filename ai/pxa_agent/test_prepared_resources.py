"""Resource identity and immutable binding checks before native installation."""
import hashlib
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from .prepared_run import stage_resource_media, verify_resource


class PreparedResourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name) / 'app.pxa'
        self.resource = Path(self.temp.name) / 'resources.pxa'
        self.app.write_bytes(b'code container')
        self.resource.write_bytes(b'resource container')
        self.resource_manifest = b'0' * 12 + b'resource manifest'
        self.app_manifest = b'0' * 12 + b'application manifest'
        self.common = [(1, b'r' * 32), (2, b'example'), (11, b'publisher key')]
        self.app_fields = self.common + [(13, hashlib.sha256(self.resource_manifest).digest())]
        self.resource_fields = self.common + [(12, b'\x02')]
        self.tools = SimpleNamespace(
            read_package_manifest=lambda path: self.app_manifest if path == self.app else self.resource_manifest,
            manifest_records=lambda body: self.app_fields if body == b'application manifest' else self.resource_fields,
            package_publisher_root=lambda path: b'r' * 32,
            package_manifest_identity=lambda path: 'example')
        self.digest = hashlib.sha256(self.resource.read_bytes()).hexdigest()

    def check(self):
        verify_resource(self.tools, self.app, self.resource, self.digest)

    def test_matching_identity_and_bound_generation(self):
        self.check()

    def test_changed_resource_is_rejected(self):
        self.resource.write_bytes(b'replaced container')
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.check()

    def test_different_binding_is_rejected(self):
        self.app_fields[-1] = (13, b'x' * 32)
        with self.assertRaisesRegex(ValueError, 'binding'):
            self.check()

    def test_code_cannot_be_supplied_as_resources(self):
        self.resource_fields[-1] = (12, b'\x01')
        with self.assertRaisesRegex(ValueError, 'not application code'):
            self.check()

    def test_foreign_identity_or_signer_is_rejected(self):
        for tag in (1, 2, 11):
            with self.subTest(tag=tag):
                self.resource_fields = [(kind, b'foreign' if kind == tag else value)
                                        for kind, value in self.common] + [(12, b'\x02')]
                with self.assertRaises(ValueError):
                    self.check()

    def test_duplicate_or_missing_identity_is_rejected(self):
        for fields in (self.common + [(1, b'other'), (12, b'\x02')],
                       [(kind, value) for kind, value in self.resource_fields if kind != 2]):
            with self.subTest(fields=fields):
                self.resource_fields = fields
                with self.assertRaisesRegex(ValueError, 'unique'):
                    self.check()

    def test_different_stable_lineage_is_rejected(self):
        self.tools.package_publisher_root = lambda path: (b'r' if path == self.app else b'x') * 32
        with self.assertRaisesRegex(ValueError, 'lineage'):
            self.check()

    def test_verified_media_survives_code_installation_and_keeps_private_files_separate(self):
        state = Path(self.temp.name) / 'state'
        state.mkdir()
        installed = state / 'resource-install' / 'packages' / 'example'
        (installed / 'maps').mkdir(parents=True)
        payload = b'original signed map bytes'
        (installed / 'maps' / 'first.wad').write_bytes(payload)
        media = stage_resource_media(self.tools, self.app, installed, state)
        expected = hashlib.sha256(b'PXA-APPKEY-1' + b'r' * 32 + b'\x07\x00example').hexdigest()
        self.assertEqual(media, state / 'media' / expected)
        code = state / 'packages' / 'example'
        code.mkdir(parents=True)
        (code / 'main.wasm').write_bytes(b'code')
        self.assertEqual((media / 'maps' / 'first.wad').read_bytes(), payload)
        self.assertFalse((code / 'maps').exists())
        self.assertFalse((state / 'userdata').exists())
        with self.assertRaises(FileExistsError):
            stage_resource_media(self.tools, self.app, installed, state)

    def test_media_never_follows_resource_symlinks(self):
        state = Path(self.temp.name) / 'state'
        state.mkdir()
        installed = Path(self.temp.name) / 'installed'
        installed.mkdir()
        (installed / 'escape').symlink_to(self.app)
        with self.assertRaisesRegex(ValueError, 'unsafe'):
            stage_resource_media(self.tools, self.app, installed, state)
        self.assertFalse((state / 'media').exists())


if __name__ == '__main__':
    unittest.main()

import hashlib
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile
import threading
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

from . import common, environment, sdk_cache


class SdkCacheTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.home = self.root / 'agent'
        self.release = self.root / 'release'
        self.release.mkdir()
        self.version = '9.8.7-test'
        self.key = ec.generate_private_key(ec.SECP256R1())
        kit = self.root / 'kit'
        kit.mkdir()
        (kit / 'release-public.pem').write_bytes(self.key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        patches = [patch.dict(os.environ, PXA_AGENT_HOME=str(self.home)),
                   patch.object(common, 'KIT', kit), patch.object(environment, 'native_supported', return_value=True)]
        for item in patches:
            item.start()
            self.addCleanup(item.stop)
        components = []
        files = {'sdk': {'VERSION': self.version.encode(), 'sdk/header.h': b'header'},
                 'devtools': {'bin/pxa': b'launcher'}, 'runtime': {'runtime/python/keep': b'runtime'},
                 'toolchain': {'toolchains/wasi-sdk/VERSION': b'34'},
                 'simulator': {'simulator/bin/probe': b'simulator'}}
        for name, entries in files.items():
            path = self.release / (name + '.tar.gz')
            with tarfile.open(path, 'w:gz') as archive:
                for filename, data in entries.items():
                    entry = tarfile.TarInfo(filename)
                    entry.size = len(data)
                    archive.addfile(entry, io.BytesIO(data))
            components.append({'name': name, 'file': path.name, 'size': path.stat().st_size,
                               'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        self.document = {'schema': 'pxa-distribution-1', 'version': self.version,
                         'platform': 'linux-x86_64', 'components': components}
        self.manifest = self.release / 'distribution.json'
        self.write_manifest()

    def write_manifest(self):
        raw = json.dumps(self.document).encode()
        self.manifest.write_bytes(raw)
        self.manifest.with_suffix('.json.sig').write_bytes(self.key.sign(raw, ec.ECDSA(hashes.SHA256())))

    def install(self, **arguments):
        return environment.install_native(self.version, str(self.manifest), **arguments)

    def archives(self):
        return [path for path in (self.home / 'downloads').glob('*') if len(path.name) == 64]

    def test_success_releases_archives_but_keeps_signed_receipts(self):
        sdk = self.install()
        self.assertEqual(self.archives(), [])
        self.assertTrue((sdk / 'distribution.json.sig').is_file())
        self.assertEqual((sdk / 'bin/pxa').read_bytes(), b'launcher')

    def test_keep_prune_and_unknown_firmware_cache(self):
        sdk = self.install(keep_archives=True)
        unknown = self.home / 'downloads' / ('f' * 64)
        unknown.write_bytes(b'prepared firmware image, not an SDK archive')
        hashes, skipped = sdk_cache.archive_candidates()
        self.assertFalse(skipped)
        self.assertEqual(len(hashes), 5)
        before = (sdk / 'distribution.json').read_bytes()
        self.assertEqual(sdk_cache.discard_archives(hashes, dry_run=True)['archive_count'], 5)
        self.assertEqual(len(self.archives()), 6)
        sdk_cache.discard_archives(hashes)
        self.assertEqual(self.archives(), [unknown])
        self.assertEqual((sdk / 'distribution.json').read_bytes(), before)

    def test_unverified_receipt_cannot_authorize_pruning(self):
        sdk = self.install(keep_archives=True)
        (sdk / 'distribution.json').write_text('{}')
        hashes, skipped = sdk_cache.archive_candidates()
        self.assertFalse(hashes)
        self.assertEqual(len(skipped), 1)
        self.assertEqual(len(self.archives()), 5)

    def test_explicit_repair_replaces_damage_preserving_project_identity_and_save(self):
        sdk = self.install()
        (sdk / 'bin/pxa').write_bytes(b'damaged')
        retained = {}
        for name in ('workspace/apps/timer/pxa.lock', 'keys/development.pem', 'workspace/save.dat'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
            retained[path] = path.read_bytes()
        self.install(repair=True)
        self.assertEqual((sdk / 'bin/pxa').read_bytes(), b'launcher')
        self.assertTrue(all(path.read_bytes() == content for path, content in retained.items()))
        self.assertFalse(list(sdk.parent.glob('.previous-sdk-*')))

    def test_failed_repair_preserves_existing_sdk_and_retry_archives(self):
        sdk = self.install()
        (sdk / 'bin/pxa').write_bytes(b'original existing SDK')
        (self.release / 'runtime.tar.gz').write_bytes(b'bad download')
        with self.assertRaises(common.ToolError):
            self.install(repair=True)
        self.assertEqual((sdk / 'bin/pxa').read_bytes(), b'original existing SDK')
        self.assertTrue(self.archives())

    def test_offline_repair_uses_retained_archives_without_network_or_original_source(self):
        sdk = self.install(keep_archives=True)
        (sdk / 'bin/pxa').write_bytes(b'damaged')
        for archive in self.release.glob('*.tar.gz'):
            archive.unlink()
        with patch.object(common, 'request', side_effect=AssertionError('network forbidden')):
            environment.install_native(self.version, offline=True, repair=True, keep_archives=True)
        self.assertEqual((sdk / 'bin/pxa').read_bytes(), b'launcher')

    def test_replacement_failure_rolls_back(self):
        sdk = self.install()
        original = os.replace
        def fail_stage(source, destination):
            if Path(source).parent.name.startswith('.sdk-'):
                raise OSError('injected stage replacement failure')
            return original(source, destination)
        with patch.object(environment.os, 'replace', side_effect=fail_stage):
            with self.assertRaises(OSError):
                self.install(repair=True)
        self.assertEqual((sdk / 'bin/pxa').read_bytes(), b'launcher')

    def test_failed_rollback_retains_previous_sdk_outside_auto_cleanup(self):
        sdk = self.install()
        original = os.replace
        def fail_replacement(source, destination):
            if Path(source).parent.name.startswith(('.sdk-', '.previous-sdk-')):
                raise OSError('injected replacement and rollback failure')
            return original(source, destination)
        with patch.object(environment.os, 'replace', side_effect=fail_replacement):
            with self.assertRaisesRegex(common.ToolError, 'previous SDK retained'):
                self.install(repair=True)
        previous = list(sdk.parent.glob('.previous-sdk-*/sdk/bin/pxa'))
        self.assertEqual(len(previous), 1)
        self.assertEqual(previous[0].read_bytes(), b'launcher')

    def test_cache_pruning_waits_for_archive_reader(self):
        item = self.document['components'][0]
        entered, completed = threading.Event(), threading.Event()
        def prune():
            entered.set()
            sdk_cache.discard_archives([item['sha256']])
            completed.set()
        with common.artifact_reader(str(self.release/item['file']), item) as archive:
            thread = threading.Thread(target=prune)
            thread.start()
            self.assertTrue(entered.wait(2))
            self.assertFalse(completed.wait(.05))
            self.assertTrue(archive.is_file())
        thread.join(2)
        self.assertFalse(thread.is_alive())
        self.assertTrue(completed.is_set())
        self.assertFalse(archive.exists())


if __name__ == '__main__':
    unittest.main()

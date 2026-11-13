"""Per-target packaging, preflight and partial store submission behavior."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from . import store, store_packages
from .common import ToolError


class StoreEnvironment:
    def __init__(self, root):
        self.workspace = root
        self.app = root / 'apps/game'
        self.app.mkdir(parents=True)
        (self.app / 'package.json').write_text('{"id":"pxa-game","version":"1.0.0","release_sequence":3}')
        self.calls = []

    def project(self, value):
        result = (self.workspace / value).resolve()
        if not result.is_relative_to(self.workspace):
            raise ToolError('Outside workspace')
        return result

    def ensure_identity(self, project):
        pass

    def call(self, arguments, **kwargs):
        self.calls.append(arguments)
        if arguments[0] == 'build' and '--check-existing' not in arguments:
            target = arguments[arguments.index('--target') + 1]
            path = Path(str(arguments[arguments.index('--output') + 1]) + '.pxa')
            path.write_bytes(('signed-' + target).encode())
        return ''


def identity(environment, path):
    target = next(t for t in store_packages.TARGETS if str(path).endswith('-' + t + '.pxa'))
    return {'app_id': 'pxa-game', 'version': '1.0.0', 'release_sequence': 3,
            'root': 'a' * 64, 'spki': b'public-key', 'contains_wasm': target == 'wasm',
            'aot_targets': [] if target == 'wasm' else [store_packages.TARGETS[target]]}


class StorePackagesTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.env = StoreEnvironment(Path(tmp.name))
        mocked = patch.object(store, 'package_identity', side_effect=identity)
        mocked.start(); self.addCleanup(mocked.stop)

    def matrix(self):
        result = store_packages.build(self.env, self.env.app, ['esp32s3', 'esp32s31', 'wasm'])
        return result, Path(result['manifest'])

    def test_invalid_target_deadline_fails_before_any_build_or_identity_change(self):
        for invalid in (0, 3601, True, 1.5):
            with self.subTest(value=invalid), self.assertRaises(ToolError):
                store_packages.build(self.env, self.env.app, ['esp32s3'],
                                     target_timeout_seconds=invalid)
        self.assertEqual(self.env.calls, [])

    def test_old_sdk_cannot_silently_drop_signed_operation_requirements(self):
        source=self.env.app/'package.json'
        metadata=json.loads(source.read_text())
        metadata['interaction']={'schema':'pxa-app-interaction-1','profiles':[{
            'id':'focus','kind':'focus','complete':True,'requires':{'confirm':'tap'}}]}
        source.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ToolError, 'did not sign the declared interaction'):
            self.matrix()
        self.assertFalse(any(args[0]=='publish' for args in self.env.calls))

    def test_build_retains_separate_outputs_and_never_requests_a_combined_target(self):
        result, manifest = self.matrix()
        self.assertFalse(result['combined_package'])
        self.assertFalse(manifest.is_relative_to(self.env.app))
        calls = [args for args in self.env.calls if args[0] == 'build']
        self.assertEqual([args[args.index('--target') + 1] for args in calls], ['wasm', 'esp32s3', 'esp32s31'])
        self.assertTrue(all('--offline' in args for args in calls))
        self.assertNotIn('--aot-only', calls[0])
        self.assertTrue(all('--aot-only' in args for args in calls[1:]))
        self.assertEqual(len({str(args[args.index('--output') + 1]) for args in calls}), 3)
        self.assertEqual(len(json.loads(manifest.read_text())['packages']), 3)
        for item in result['packages']:
            path = self.env.workspace / item['path']
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), item['sha256'])

    def test_native_only_selection_automatically_adds_one_independent_wasm_package(self):
        for targets in (['esp32s3'], ['wasm'], ['wasm', 'esp32s3', 'wasm']):
            with self.subTest(targets=targets):
                result = store_packages.build(self.env, self.env.app, targets)
                self.assertEqual([p['target'] for p in result['packages']],
                                 ['wasm'] if targets == ['wasm'] else ['wasm', 'esp32s3'])

    def test_missing_or_mislabeled_wasm_cannot_reach_upload(self):
        result, manifest = self.matrix()
        original = json.loads(manifest.read_text())
        manifest.write_text(json.dumps({**original, 'packages': original['packages'][1:]}))
        with patch.object(store, 'publish') as upload, self.assertRaisesRegex(ToolError, 'separate Wasm'):
            store_packages.publish(self.env, self.env.app, manifest)
        upload.assert_not_called()
        manifest.write_text(json.dumps(original))
        wasm = self.env.workspace / result['packages'][0]['path']
        with patch.object(store, 'package_identity', side_effect=lambda env, path:
                {**identity(env, path), 'contains_wasm': False} if path == wasm else identity(env, path)), \
             patch.object(store, 'publish') as upload, self.assertRaisesRegex(ToolError, 'selected target'):
            store_packages.publish(self.env, self.env.app, manifest)
        upload.assert_not_called()

    def test_invalid_targets_fail_before_build_and_failed_build_has_no_ready_matrix(self):
        result, manifest = self.matrix()
        before = len(self.env.calls)
        with self.assertRaises(ToolError):
            store_packages.build(self.env, self.env.app, ['not-a-target'])
        self.assertEqual(len(self.env.calls), before)
        with patch.object(self.env, 'call', side_effect=ToolError('compile failed')), self.assertRaises(ToolError):
            store_packages.build(self.env, self.env.app, ['esp32s3'])
        self.assertTrue(manifest.exists())
        self.assertEqual(len(list((self.env.workspace / 'local/artifacts').rglob('packages.json'))), 1)

    def test_all_files_and_signatures_are_preflighted_before_any_upload(self):
        result, manifest = self.matrix()
        path = self.env.workspace / result['packages'][1]['path']
        path.write_bytes(b'changed')
        with patch.object(store, 'publish') as upload, self.assertRaisesRegex(ToolError, 'changed'):
            store_packages.publish(self.env, self.env.app, manifest)
        upload.assert_not_called()

    def test_target_signer_and_release_mismatch_cannot_reach_upload(self):
        result, manifest = self.matrix()
        original = json.loads(manifest.read_text())
        for change in ({'version': '2'}, {'release_sequence': 99}, {'publisher_key_id': 'b' * 64},
                       {'lineage_root_key_id': 'b' * 64}):
            manifest.write_text(json.dumps({**original, **change}))
            with self.subTest(change=change), patch.object(store, 'publish') as upload, self.assertRaises(ToolError):
                store_packages.publish(self.env, self.env.app, manifest)
            upload.assert_not_called()

    def test_stale_build_inputs_fail_before_upload_without_implicit_rebuild(self):
        _, manifest = self.matrix()
        with patch.object(self.env, 'call', side_effect=ToolError('source changed')) as sdk, \
             patch.object(store, 'publish') as upload, self.assertRaisesRegex(ToolError, 'source changed'):
            store_packages.publish(self.env, self.env.app, manifest)
        self.assertIn('--check-existing', sdk.call_args.args[0])
        upload.assert_not_called()

    def test_media_is_uploaded_once_and_partial_failure_is_not_replayed(self):
        _, manifest = self.matrix()
        with patch.object(store, 'publish', side_effect=[{'status': 'pending_review'}, ToolError('network')]) as upload:
            result = store_packages.publish(self.env, self.env.app, manifest, screenshots=['real.png'])
        self.assertFalse(result['passed'])
        self.assertEqual([item['target'] for item in result['completed']], ['wasm'])
        self.assertEqual(result['remaining_targets'], ['esp32s3', 'esp32s31'])
        self.assertEqual(upload.call_count, 2)
        self.assertEqual(upload.call_args_list[0].kwargs['screenshots'], ['real.png'])
        self.assertEqual(upload.call_args_list[1].kwargs['screenshots'], ())
        self.assertFalse(upload.call_args_list[1].kwargs['update_listing'])

    def test_arbitrary_matrix_order_still_uploads_wasm_first(self):
        _, manifest = self.matrix()
        document = json.loads(manifest.read_text())
        document['packages'].reverse()
        manifest.write_text(json.dumps(document))
        with patch.object(store, 'publish', return_value={'status': 'pending_review'}) as upload:
            result = store_packages.publish(self.env, self.env.app, manifest)
        self.assertTrue(result['passed'])
        self.assertEqual(result['completed'][0]['target'], 'wasm')
        self.assertTrue(str(upload.call_args_list[0].args[2]).endswith('-wasm.pxa'))

    def test_signed_multi_target_metadata_is_rejected_by_default_before_authorization(self):
        with patch.object(store, 'package_identity', return_value={'aot_targets': ['esp32-s3', 'esp32-s31']}), \
             patch('pxa_agent.scope.enforce'), patch.object(store, 'authorize') as authorize, \
             self.assertRaisesRegex(ToolError, 'one target per'):
            store.publish(self.env, self.env.app, self.env.app / 'universal.pxa')
        authorize.assert_not_called()


class PackageArchitectureParserTests(unittest.TestCase):
    def test_architectures_come_from_component_manifests_not_filenames(self):
        def tlv(tag, value):
            return struct.pack('<HH', tag, len(value)) + value
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'claims-to-be-esp32s3.pxa'
            artifacts = b''.join(tlv(4, tlv(1, b'\x02') + tlv(3, target.encode()))
                                 for target in ('esp32-s3', 'esp32-s31'))
            body = (tlv(2, b'pxa-game') + tlv(3, b'1.0.0') + tlv(9, struct.pack('<Q', 3))
                    + tlv(11, b'public-key') + tlv(16, artifacts))
            manifest = b'PXAM' + struct.pack('<HHI', 0, 8, len(body)) + body
            header = bytearray(64); header[:4] = b'PXAC'
            struct.pack_into('<H', header, 8, 64); struct.pack_into('<I', header, 16, len(manifest))
            path.write_bytes(header + manifest)
            value = store.package_identity(None, path)
            self.assertEqual(value['aot_targets'], ['esp32-s3', 'esp32-s31'])
            self.assertEqual(value['release_sequence'], 3)

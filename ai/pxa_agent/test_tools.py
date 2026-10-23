"""Behavioral checks for signed inputs, identity/data preservation and adapters.

Run from the kit environment: python -m unittest pxa_agent.test_tools
"""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

from . import common, environment, firmware, integration, store


class ToolTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.ws = self.root / 'project with spaces'
        self.ws.mkdir()
        self.override = patch.dict(os.environ, PXA_AGENT_HOME=str(self.root / 'home'))
        self.override.start()

    def tearDown(self):
        self.override.stop()
        self.temporary.cleanup()

    def test_signed_manifest_and_changed_bytes(self):
        key = ec.generate_private_key(ec.SECP256R1())
        anchor = self.root / 'trust'
        anchor.mkdir()
        (anchor / 'release-public.pem').write_bytes(key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        document = self.root / 'release.json'
        document.write_text('{"schema":"test","version":"1"}')
        Path(str(document) + '.sig').write_bytes(key.sign(document.read_bytes(), ec.ECDSA(hashes.SHA256())))
        with patch.object(common, 'KIT', anchor):
            self.assertEqual(common.signed_document(document, schema='test')[0]['version'], '1')
            document.write_text('{"schema":"test","version":"2"}')
            with self.assertRaisesRegex(common.ToolError, 'signature'):
                common.signed_document(document)

    def test_artifact_hash_mismatch_and_bad_size(self):
        archive = self.root / 'payload'
        archive.write_bytes(b'hello')
        info = dict(file='sdk.tar.gz', size=5, sha256=hashlib.sha256(b'hello').hexdigest())
        cached = common.artifact(archive, info)
        self.assertEqual(cached.read_bytes(), b'hello')
        cached.write_bytes(b'other')
        archive.write_bytes(b'wrong')
        with self.assertRaisesRegex(common.ToolError, 'hash'):
            common.artifact(archive, info)
        with self.assertRaises(common.ToolError):
            common.artifact(archive, {**info, 'file': '../escape'})

    def test_archive_traversal_and_escaping_symlink(self):
        for name, target in [('../escape', None), ('link', '/outside')]:
            path = self.root / 'bad.tar'
            with tarfile.open(path, 'w') as archive:
                member = tarfile.TarInfo(name)
                if target:
                    member.type, member.linkname = tarfile.SYMTYPE, target
                archive.addfile(member)
            with self.assertRaises((common.ToolError, tarfile.FilterError)):
                common.extract(path, self.ws)

    def test_workspace_boundary_and_original_publisher_guard(self):
        env = environment.Environment(self.ws, {'engine': 'native', 'sdk': '/unused'})
        with self.assertRaises(common.ToolError):
            env.project('../outside')
        app = self.ws / 'apps/existing'
        (app / '.pxa').mkdir(parents=True)
        key = ec.generate_private_key(ec.SECP256R1())
        public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        (app / '.pxa/publisher.der').write_bytes(public)
        with self.assertRaisesRegex(common.ToolError, 'original private key'):
            env.ensure_identity(app)
        path = environment.sdk_data() / 'identities/development.pem'
        path.parent.mkdir(parents=True)
        path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        env.ensure_identity(app)
        (app / '.pxa/publisher.der').write_bytes(b'different')
        with self.assertRaisesRegex(common.ToolError, 'differs'):
            env.ensure_identity(app)

    def test_project_adapters_preserve_settings_and_local_edits(self):
        (self.ws / 'AGENTS.md').write_text('User project instructions\n')
        (self.ws / '.codex').mkdir()
        (self.ws / '.codex/config.toml').write_text('model = "user-model"\n[mcp_servers.other]\ncommand = "other"\n')
        (self.ws / '.mcp.json').write_text(json.dumps({'mcpServers': {'other': {'command': 'other'}}}))
        integration.install(self.ws)
        integration.install(self.ws)
        self.assertTrue((self.ws / '.pxa/pxa.ps1').read_bytes().startswith(b'\xef\xbb\xbf'))
        self.assertIn(b'chcp 65001', (self.ws / '.pxa/pxa.cmd').read_bytes())
        import tomllib
        config = tomllib.loads((self.ws / '.codex/config.toml').read_text())
        self.assertEqual(config['model'], 'user-model')
        self.assertEqual(config['mcp_servers']['other']['command'], 'other')
        self.assertEqual(json.loads((self.ws / '.mcp.json').read_text())['mcpServers']['other']['command'], 'other')
        self.assertTrue((self.ws / '.claude/skills/pxa-debug/SKILL.md').is_file())
        self.assertTrue((self.ws / '.agents/skills/pxa-debug/SKILL.md').is_file())
        rules = self.ws / 'AGENTS.md'
        rules.write_text(rules.read_text().replace('No ESP-IDF', 'Local edited ESP-IDF'))
        with self.assertRaisesRegex(common.ToolError, 'edited locally'):
            integration.install(self.ws)
        self.assertIn('Local edited', rules.read_text())

    def test_existing_server_conflict_has_no_partial_writes(self):
        (self.ws / '.mcp.json').write_text('{"mcpServers":{"pxa":{"command":"user"}}}')
        with self.assertRaisesRegex(common.ToolError, 'retained'):
            integration.install(self.ws)
        self.assertFalse((self.ws / 'AGENTS.md').exists())
        self.assertFalse((self.ws / '.agents').exists())

    def test_partition_layout_mismatch_never_writes(self):
        # A real binary ESP partition table, not a mocked parser result.
        entry = struct.pack('<HBBII16sI', 0x50aa, 0, 0, 0x10000, 0x400000, b'factory', 0)
        raw = entry + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(entry, usedforsecurity=False).digest() + b'\xff' * 32
        self.assertEqual(firmware.partition_table(raw)['factory']['size'], 0x400000)
        env = environment.Environment(self.ws, {'engine': 'native', 'sdk': '/unused'})
        prepared = dict(chip='esp32s3', partitions={'factory': {'offset': 0x10000, 'size': 0x500000}})
        calls = []
        def transport(command, **kwargs):
            calls.append(command)
            self.assertNotIn('write-flash', command)
            Path(command[-1]).write_bytes(raw)
            return type('Status', (), {'returncode': 0})()
        with patch.object(firmware, 'prepare', return_value=prepared), patch.object(env, 'device', return_value='board=pai-touch;target=esp32s3'), patch.object(firmware.subprocess, 'run', side_effect=transport):
            with self.assertRaisesRegex(common.ToolError, 'layouts differ'):
                firmware.flash(env, 'pai-touch', port='test')
        self.assertEqual(len(calls), 1)
        with self.assertRaisesRegex(common.ToolError, 'explicit --erase-data'):
            firmware.flash(env, 'pai-touch', port='test', mode='full')

    def test_private_upload_credentials_never_enter_status_output(self):
        path = store.cache_path('https://app.doit.am', {'app_id': 'test', 'root': 'a' * 64})
        common.atomic_json(path, {'token': 'PRIVATE-SECRET', 'token_expires_at': 9999999999,
            'app_id': 4, 'dashboard_url': 'https://app.doit.am/dashboard/apps/4', 'display_name': 'Alice'}, private=True)
        state = store.status(path.stem)
        self.assertTrue(state['authorized'])
        self.assertNotIn('PRIVATE-SECRET', json.dumps(state))
        if os.name != 'nt':
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_repeat_setup_accepts_windows_engine_executable_path(self):
        engine = str(self.root / 'Docker Desktop/docker.exe')
        config = {'schema': 'pxa-agent-workspace-1', 'sdk_version': '0.5.0', 'engine': engine}
        common.atomic_json(self.ws / '.pxa/agent.json', config)
        with patch.object(environment, 'install_container', return_value={'engine': engine, 'image': 'sha256:test'}) as installer:
            result = environment.configure(self.ws)
            self.assertEqual(result.config['engine'], engine)
            installer.assert_called_once_with('0.5.0', engine, None)


    def test_renewing_browser_grant_drops_old_token_until_new_approval(self):
        env = environment.Environment(self.ws, {'engine': 'native', 'sdk': '/unused'})
        project = self.ws / 'apps/game'
        project.mkdir(parents=True)
        (project / 'package.json').write_text('{"id":"pxa-game","name":"Game"}')
        package = project / 'dist.pxa'
        package.write_bytes(b'test-package')
        private = ec.generate_private_key(ec.SECP256R1())
        public = private.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        key = environment.sdk_data() / 'identities/development.pem'
        key.parent.mkdir(parents=True)
        key.write_bytes(private.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        identity = {'app_id': 'pxa-game', 'root': hashlib.sha256(public).hexdigest(), 'spki': public}
        path = store.cache_path('https://app.doit.am', identity)
        common.atomic_json(path, {'token': 'old-token', 'token_expires_at': 9999999999,
            'current_key': identity['root'], 'app_id': 4, 'dashboard_url': 'url', 'display_name': 'User'}, private=True)
        grant = {'challenge': 'A' * 43, 'device_code': 'local-test-only', 'user_code': 'ABCD-EFGH',
            'expires_in': 600, 'interval': 5, 'verification_uri_complete': 'https://app.doit.am/developer/authorize?code=ABCDEFGH'}
        with patch.object(env, 'call', return_value=''), patch.object(store, 'package_identity', return_value=identity), patch.object(store, 'api', side_effect=[grant, {'verified': True}]) as api:
            self.assertEqual(store.authorize(env, project, package, open_browser=False)['status'], 'authorized')
            api.assert_not_called()
            renewed = store.authorize(env, project, package, open_browser=False, renew=True)
            self.assertEqual(renewed['status'], 'awaiting_browser_approval')
        self.assertNotIn('token', json.loads(path.read_text()))
        self.assertFalse(store.status(path.stem, poll=False)['authorized'])


if __name__ == '__main__':
    unittest.main()

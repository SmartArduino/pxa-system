"""Remembered store accounts, secret redaction, renewal and explicit sign-out."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

from . import account_credentials as credentials, store
from .common import ToolError, atomic_json

SERVER = 'https://store.example.test'
ACCOUNT = {'credential': 'pxa_creator_' + 's' * 43, 'scope': 'creator:publish', 'session_id': 'a' * 32,
           'user_id': 7, 'display_name': 'Creator', 'client_name': 'My computer', 'expires_in': 31536000}
GRANT = {'device_code': 'grant-' + 'd' * 43, 'challenge': 'A' * 43, 'user_code': 'ABCD-EFGH',
         'expires_in': 600, 'interval': 0,
         'verification_uri_complete': SERVER + '/developer/authorize?code=ABCDEFGH'}
UPLOAD = {'token': 'UPLOAD-SECRET', 'app_id': 10, 'scope': 'app:upload', 'expires_in': 1800,
          'display_name': 'Creator', 'dashboard_url': SERVER + '/dashboard/apps/10'}


class AccountCredentialsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, PXA_AGENT_HOME=str(self.root / 'agent')).start()
        patch.object(credentials, 'native_keyring', return_value=None).start()

    def remember(self, **changes):
        credentials.save_account(SERVER, {**ACCOUNT, 'expires_at': time.time() + 31536000, **changes})

    def app(self):
        app = self.root / 'game'
        app.mkdir()
        (app / 'package.json').write_text('{"id":"pxa-game","name":"Game","description":"Dodge obstacles to score points."}')
        package = app / 'game.pxa'
        package.write_bytes(b'signed-test-package')
        key = ec.generate_private_key(ec.SECP256R1())
        key_path = self.root / 'signing.pem'
        key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                               serialization.NoEncryption()))
        public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        identity = {'app_id': 'pxa-game', 'root': hashlib.sha256(public).hexdigest(), 'spki': public}
        env = SimpleNamespace(project=lambda value: Path(value), call=Mock(return_value=''), workspace=self.root)
        return env, app, package, key_path, identity

    def test_first_login_polls_once_and_subsequent_login_renews_without_browser(self):
        safe = {k: v for k, v in ACCOUNT.items() if k != 'credential'}
        with patch.object(store, 'api', side_effect=[GRANT, {'account': ACCOUNT}, safe]) as api, \
             patch.object(store.webbrowser, 'open') as browser:
            first = store.login(server=SERVER)
            pending = store.login(server=SERVER, open_browser=False)
            self.assertEqual(first['credential_ref'], pending['credential_ref'])
            ready = store.status(first['credential_ref'])
            self.assertTrue(ready['authorized'])
            again = store.login(server=SERVER)
            self.assertTrue(again['authorized'])
            self.assertEqual(browser.call_count, 1)
            self.assertEqual(api.call_args_list[-1].args[1], '/api/v2/creator/session/refresh')
        for value in (first, ready, again):
            self.assertNotIn(ACCOUNT['credential'], json.dumps(value))
        saved = credentials.account_path(SERVER)
        if os.name != 'nt':
            self.assertEqual(saved.stat().st_mode & 0o777, 0o600)
            self.assertEqual(saved.parent.stat().st_mode & 0o777, 0o700)

    def test_native_keyring_keeps_long_credential_out_of_metadata(self):
        values = {}
        ring = SimpleNamespace(set_password=lambda service, key, value: values.update({key: value}),
                               get_password=lambda service, key: values.get(key),
                               delete_password=lambda service, key: values.pop(key))
        with patch.object(credentials, 'native_keyring', return_value=ring):
            self.remember()
            self.assertNotIn(ACCOUNT['credential'], credentials.account_path(SERVER).read_text())
            self.assertEqual(credentials.read_account(SERVER)['credential'], ACCOUNT['credential'])
            credentials.forget_account(SERVER)
            self.assertFalse(values)

    def test_unavailable_native_store_does_not_overwrite_existing_login(self):
        self.remember()
        path = credentials.account_path(SERVER)
        saved = json.loads(path.read_text())
        saved.pop('credential'); saved['credential_storage'] = 'system-keyring'
        atomic_json(path, saved, private=True)
        before = path.read_bytes()
        with patch.object(store, 'api') as api, self.assertRaisesRegex(ToolError, 'Unlock or restore'):
            store.login(server=SERVER)
        api.assert_not_called()
        self.assertEqual(path.read_bytes(), before)

    def test_expired_app_authorization_uses_account_and_real_local_key_proof(self):
        self.remember()
        env, app, package, key_path, identity = self.app()
        safe = {k: v for k, v in ACCOUNT.items() if k != 'credential'}
        with patch.object(store, 'api', side_effect=[safe, {**GRANT, 'account_authorized': True},
              {'verified': True}, UPLOAD]) as api, patch.object(store, 'package_identity', return_value=identity), \
             patch.object(store.webbrowser, 'open') as browser:
            result = store.authorize(env, app, package, key=key_path, server=SERVER)
        self.assertTrue(result['authorized']); browser.assert_not_called()
        self.assertEqual(api.call_args_list[1].kwargs['credential']['token'], ACCOUNT['credential'])
        path = store.cache_path(SERVER, identity)
        saved = json.loads(path.read_text())
        self.assertEqual(saved['creator_session_id'], ACCOUNT['session_id'])
        self.assertEqual(saved['token'], UPLOAD['token'])
        self.assertNotIn(UPLOAD['token'], json.dumps(result))

    def test_missing_summary_stops_authorization_before_any_server_mutation(self):
        env, app, package, key_path, value = self.app()
        (app / 'package.json').write_text('{"id":"pxa-game","name":"Game"}')
        with patch.object(store, 'package_identity', return_value=value), patch.object(store, 'api') as api, \
             self.assertRaisesRegex(ToolError, 'factual store.json listing text'):
            store.authorize(env, app, package, key=key_path, server=SERVER)
        api.assert_not_called()

    def test_logout_removes_app_tokens_and_other_origins_survive_network_failure(self):
        self.remember()
        identity = {'app_id': 'game', 'root': 'a' * 64}
        path = store.cache_path(SERVER, identity)
        atomic_json(path, {'server': SERVER, 'token': 'secret', 'grant': GRANT}, private=True)
        other = store.cache_path('https://other.example.test', identity)
        atomic_json(other, {'server': 'https://other.example.test', 'token': 'keep'}, private=True)
        with patch.object(store, 'api', side_effect=ToolError('offline')):
            result = store.logout(server=SERVER)
        self.assertTrue(result['logged_out']); self.assertFalse(result['remote_revoked'])
        self.assertIsNotNone(result['warning'])
        self.assertNotIn('token', json.loads(path.read_text()))
        self.assertNotIn('grant', json.loads(path.read_text()))
        self.assertEqual(json.loads(other.read_text())['token'], 'keep')
        self.assertFalse(credentials.account_path(SERVER).exists())

    def test_revoked_account_is_cleared_and_login_can_start_again(self):
        self.remember()
        with patch.object(store, 'api', side_effect=[store.CredentialRejected('revoked'), GRANT]), \
             patch.object(store.webbrowser, 'open'):
            result = store.login(server=SERVER)
        self.assertEqual(result['status'], 'awaiting_browser_approval')
        self.assertNotIn('credential', json.loads(credentials.account_path(SERVER).read_text()))

    def test_account_switch_cannot_publish_with_the_previous_account_app_token(self):
        self.remember(session_id='b' * 32)
        env, app, package, _, identity = self.app()
        path = store.cache_path(SERVER, identity)
        atomic_json(path, {**UPLOAD, 'server': SERVER, 'token_expires_at': time.time() + 1800,
                          'creator_session_id': 'a' * 32}, private=True)
        with patch.object(store, 'package_identity', return_value=identity), patch('pxa_agent.scope.enforce'), \
             patch.object(store, 'authorize', side_effect=ToolError('other owner')) as authorize, \
             patch.object(store, 'upload') as upload, self.assertRaisesRegex(ToolError, 'other owner'):
            store.publish(env, app, package, server=SERVER)
        authorize.assert_called_once(); upload.assert_not_called()

    def test_global_account_commands_do_not_require_a_workspace_or_expose_credentials(self):
        self.remember()
        from .cli import main
        with patch('sys.argv', ['agent.py', '--workspace', str(self.root / 'missing'), 'store', 'account', '--server', SERVER]), \
             patch('pxa_agent.cli.Environment', side_effect=AssertionError('unexpected SDK workspace')), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(main(), 0)
        result = json.loads(output.getvalue())
        self.assertTrue(result['authorized'])
        self.assertNotIn(ACCOUNT['credential'], output.getvalue())

    def test_native_submission_requires_matching_wasm_on_server_before_any_writes(self):
        self.remember()
        env, app, package, _, value = self.app()
        value.update(aot_targets=['esp32-s3'], version='1.0.0', release_sequence=7)
        atomic_json(store.cache_path(SERVER, value), {**UPLOAD, 'server': SERVER,
            'token_expires_at': time.time() + 1800, 'creator_session_id': ACCOUNT['session_id']}, private=True)
        prepared = {'listing': {}, 'images': [], 'needs_choice': [], 'omitted': [], 'warnings': []}
        for available in (False, True):
            with self.subTest(available=available), patch.object(store, 'package_identity', return_value=value), \
                 patch('pxa_agent.scope.enforce'), patch.object(store, 'prepare', return_value=prepared) as prepare, \
                 patch.object(store, 'api', return_value={'wasm_uploaded': available}) as api, \
                 patch.object(store, 'upload', return_value=({'status': 'pending', 'sha256': 'digest'}, 'digest')) as upload:
                if available:
                    self.assertEqual(store.publish(env, app, package, server=SERVER)['status'], 'pending_review')
                    upload.assert_called_once()
                else:
                    with self.assertRaisesRegex(ToolError, 'separate Wasm'):
                        store.publish(env, app, package, server=SERVER)
                    prepare.assert_not_called(); upload.assert_not_called()
                self.assertIn('version=1.0.0&release_sequence=7&publisher_key_id=', api.call_args.args[1])
        with patch.object(store, 'package_identity', return_value=value), patch('pxa_agent.scope.enforce'), \
             patch.object(store, 'api', side_effect=store.CredentialRejected('revoked')), \
             patch.object(store, 'upload') as upload, self.assertRaises(store.CredentialRejected):
            store.publish(env, app, package, server=SERVER)
        upload.assert_not_called()
        self.assertNotIn('token', json.loads(store.cache_path(SERVER, value).read_text()))

    def test_publish_only_updates_explicit_listing_fields_and_returns_draft_review_state(self):
        self.remember()
        env, app, package, _, value = self.app()
        atomic_json(store.cache_path(SERVER, value), {**UPLOAD, 'server': SERVER,
            'token_expires_at': time.time() + 1800, 'creator_session_id': ACCOUNT['session_id']}, private=True)
        changes = {'summary': 'Dodge obstacles to score points.', 'description': '', 'tags': []}
        prepared = {'listing': changes, 'images': [{'kind': 'screenshot', 'path': 'runtime.png'}],
                    'needs_choice': [], 'omitted': [], 'warnings': []}
        reviewed = {'review_status': 'pending', 'public_listing_retained': True, 'draft_revision': 'revision'}
        with patch.object(store, 'package_identity', return_value=value), patch('pxa_agent.scope.enforce'), \
             patch.object(store, 'prepare', return_value=prepared), patch.object(store, 'api', return_value=reviewed) as api, \
             patch.object(store, 'upload', side_effect=[({'kind': 'screenshot', **reviewed}, 'image-digest'),
                 ({'status': 'pending', 'sha256': 'package-digest'}, 'package-digest')]) as upload:
            result = store.publish(env, app, package, server=SERVER)
        self.assertEqual(api.call_args.args[2], changes)
        self.assertEqual(api.call_args.kwargs['method'], 'PATCH')
        self.assertEqual(upload.call_count, 2)
        self.assertEqual(result['listing_update'], reviewed)
        self.assertEqual(result['media_updates'], [{'kind': 'screenshot', 'review_status': 'pending', 'public_listing_retained': True}])
        self.assertEqual(result['status'], 'pending_review')

    @unittest.skipIf(os.name == 'nt', 'POSIX credential modes')
    def test_unsafe_modes_and_symlinked_metadata_are_rejected(self):
        self.remember()
        path = credentials.account_path(SERVER)
        path.chmod(0o644)
        with self.assertRaisesRegex(ToolError, 'private'):
            credentials.read_account(SERVER)
        path.chmod(0o600)
        target = path.with_suffix('.backup')
        path.rename(target); path.symlink_to(target)
        with self.assertRaisesRegex(ToolError, 'linked'):
            credentials.read_account(SERVER)

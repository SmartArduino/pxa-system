"""Browser-assisted publishing. Private keys and upload tokens stay local."""
from __future__ import annotations

import base64
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
from urllib.error import HTTPError
from urllib.parse import urlparse
import uuid
import webbrowser

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from .common import ToolError, atomic_json, data_root, digest, request
from .environment import sdk_data

class CredentialRejected(ToolError):
    pass


ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def server_origin(server):
    url = urlparse(server)
    if url.scheme != 'https' or not url.hostname or url.username or url.password or url.query or url.fragment or url.path not in ('', '/'):
        raise ToolError('Choose the canonical HTTPS store origin')
    return server.rstrip('/')


def api(server, path, value=None):
    data = json.dumps(value).encode() if value is not None else None
    try:
        with request(server + path, data=data, method='POST' if data else 'GET',
                     headers={'Accept': 'application/json', **({'Content-Type': 'application/json'} if data else {})}) as response:
            return json.load(response)
    except HTTPError as error:
        if error.code in (400, 429):
            result = json.load(error)
            if result.get('error') in ('authorization_pending', 'slow_down', 'expired_token', 'access_denied', 'rate_limited'):
                return result
        raise ToolError(f'Store request failed ({error.code}); check account permissions and the server response') from error


def package_identity(environment, package):
    script = (Path(environment.config['sdk']) / 'tools/pxadb/pxadb.py' if environment.native
              else Path(environment.config['host_tools']) / 'pxadb.py')
    spec = spec_from_file_location('pxa_agent_package_reader', script)
    module = module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    fields = dict(module.manifest_records(module.read_package_manifest(package)[12:]))
    public = fields.get(11)
    if not public:
        raise ToolError('The package has no publisher public key')
    # The lineage TLV contains an authenticated root when present. Keep the
    # current key as the default for ordinary, non-rotated development apps.
    root = hashlib.sha256(public).hexdigest()
    if lineage := fields.get(10):
        if len(lineage) < 96 or lineage[:4] != b'PXKL' or struct.unpack_from('<HHHHI', lineage, 4)[:3] != (0, 1, 16):
            raise ToolError('Invalid publisher lineage metadata')
        root = lineage[24:56].hex()
    return {'app_id': module.package_manifest_identity(package), 'spki': public, 'root': root}


def cache_path(server, identity):
    name = hashlib.sha256((server + '\0' + identity['app_id'] + '\0' + identity['root']).encode()).hexdigest()
    directory = data_root() / 'credentials'
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    return directory / (name + '.json')


def authorize(environment, project, package, *, server='https://app.doit.am', key=None, open_browser=True, renew=False):
    server = server_origin(server)
    project = environment.project(project)
    package = environment.project(package)
    environment.call(['publish', package, '--check-only'])
    identity = package_identity(environment, package)
    cached = cache_path(server, identity)
    current_key = hashlib.sha256(identity['spki']).hexdigest()
    if (not renew and cached.is_file() and json.loads(cached.read_text()).get('current_key') == current_key
            and status(cached.stem, poll=False).get('authorized')):
        return {**status(cached.stem, poll=False), 'credential_ref': cached.stem, 'status': 'authorized'}
    key = key or os.environ.get('PXA_SIGNING_KEY')
    key_path = Path(key).expanduser().resolve() if key else sdk_data() / 'identities/development.pem'
    try:
        private = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    except (ValueError, TypeError) as error:
        raise ToolError('Cannot open the selected local signing key; do not paste private keys or passwords into chat') from error
    if not isinstance(private, ec.EllipticCurvePrivateKey) or not isinstance(private.curve, ec.SECP256R1):
        raise ToolError('The selected publisher identity is not P-256')
    public = private.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    if public != identity['spki']:
        raise ToolError('The selected private key does not match the signed package; preserve the existing app identity')
    metadata = json.loads((project / 'package.json').read_text())
    if metadata['id'] != identity['app_id']:
        raise ToolError('The project and signed package have different app identities')
    listing_path = project / 'store.json'
    listing = json.loads(listing_path.read_text()) if listing_path.is_file() else {}
    payload = {'client_name': 'PXA Agent Kit', 'app_id': identity['app_id'],
               'lineage_root_key_id': identity['root'], 'publisher_spki': base64.b64encode(public).decode(),
               'name': listing.get('name') or metadata.get('name') or identity['app_id'],
               'summary': listing.get('summary') or (metadata.get('description') or '使用 PXA 创作的应用')[:200],
               'description': listing.get('description', ''), 'kind': listing.get('kind', 'app'),
               'category': listing.get('category', '')}
    grant = api(server, '/api/v2/creator/authorize', payload)
    if 'error' in grant:
        raise ToolError('Creator authorization could not start: ' + grant['error'])
    challenge = grant['challenge']
    if not re.fullmatch(r'[A-Za-z0-9_-]{43}', challenge):
        raise ToolError('The store returned an invalid signing challenge')
    der = private.sign(b'PXA-STORE-PUBLISHER-KEY\0' + challenge.encode(), ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    signature = r.to_bytes(32, 'big') + min(s, ORDER - s).to_bytes(32, 'big')
    proof = api(server, '/api/v2/creator/proof', {'device_code': grant['device_code'],
                'signature': base64.b64encode(signature).decode()})
    if proof.get('verified') is not True:
        raise ToolError('The store did not verify the local publisher proof')
    path = cache_path(server, identity)
    previous = json.loads(path.read_text()) if path.exists() else {}
    previous.pop('token', None)
    previous.pop('token_expires_at', None)
    atomic_json(path, {**previous, 'server': server, 'current_key': current_key, 'identity': {'app_id': identity['app_id'], 'root': identity['root']},
        'grant': grant, 'grant_expires_at': time.time() + grant['expires_in'], 'next_poll_at': time.time() + grant['interval']}, private=True)
    if open_browser:
        uri = grant['verification_uri_complete']
        if server_origin(uri.split('/developer/authorize', 1)[0]) != server:
            raise ToolError('The store returned a different browser authorization origin')
        webbrowser.open(uri)
    return {'status': 'awaiting_browser_approval', 'user_code': grant['user_code'],
            'verification_uri': grant['verification_uri_complete'], 'expires_in': grant['expires_in'],
            'poll_after_seconds': grant['interval'], 'credential_ref': path.stem}


def status(reference, *, poll=True):
    if not re.fullmatch('[a-f0-9]{64}', reference):
        raise ToolError('Invalid local credential reference')
    path = data_root() / 'credentials' / (reference + '.json')
    saved = json.loads(path.read_text())
    if saved.get('token') and saved.get('token_expires_at', 0) > time.time():
        return {'authorized': True, 'app_id': saved['app_id'], 'scope': 'app:upload',
                'dashboard_url': saved['dashboard_url'], 'display_name': saved['display_name']}
    grant = saved.get('grant')
    if not grant or saved['grant_expires_at'] <= time.time():
        return {'authorized': False, 'error': 'expired_token'}
    remaining = saved.get('next_poll_at', 0) - time.time()
    if not poll or remaining > 0:
        return {'authorized': False, 'error': 'authorization_pending', 'poll_after_seconds': max(1, int(remaining) + 1)}
    result = api(saved['server'], '/api/v2/creator/token', {'device_code': grant['device_code']})
    saved['next_poll_at'] = time.time() + grant['interval']
    if result.get('token'):
        saved.update({key: result[key] for key in ('token', 'app_id', 'scope', 'dashboard_url', 'display_name')})
        saved['token_expires_at'] = time.time() + result['expires_in']
        saved.pop('grant', None)
    elif result.get('error') in ('expired_token', 'access_denied'):
        saved.pop('grant', None)
    atomic_json(path, saved, private=True)
    if result.get('token'):
        return status(reference, poll=False)
    return {'authorized': False, 'error': result.get('error', 'authorization_pending'),
            'poll_after_seconds': grant['interval']}


def upload(server, endpoint, credential, filename, *, field='package', extra=None):
    path = Path(filename)
    before = digest(path)
    boundary = 'pxa-' + uuid.uuid4().hex
    safe_name = 'image' + path.suffix.lower() if field == 'image' else 'app.pxa'
    prefix = b''
    for name, value in (extra or {}).items():
        prefix += (f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n').encode()
    prefix += (f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"; filename="{safe_name}"\r\n'
               'Content-Type: application/octet-stream\r\n\r\n').encode()
    suffix = f'\r\n--{boundary}--\r\n'.encode()
    def body():
        yield prefix
        with path.open('rb') as source:
            while block := source.read(1024 * 1024):
                yield block
        yield suffix
    try:
        with request(server + endpoint, data=body(), method='POST', timeout=180,
            headers={'Authorization': 'Bearer ' + credential['token'], 'Accept': 'application/json',
                     'Content-Type': f'multipart/form-data; boundary={boundary}',
                     'Content-Length': str(len(prefix) + path.stat().st_size + len(suffix))}) as response:
            result = json.load(response)
    except HTTPError as error:
        if error.code == 401:
            raise CredentialRejected('Upload authorization expired or was revoked. Run store authorize again.') from error
        raise ToolError(f'Upload failed ({error.code}); inspect the app dashboard or authorize again. No automatic write retry was made.') from error
    if digest(path) != before:
        raise ToolError('The signed package changed during upload')
    return result, before


def publish(environment, project, package, *, reference=None, server='https://app.doit.am', icon=None, screenshots=(), changelog=''):
    server = server_origin(server)
    project, package = environment.project(project), environment.project(package)
    identity = package_identity(environment, package)
    path = cache_path(server, identity)
    if reference and path.stem != reference:
        raise ToolError('The authorization belongs to a different server or app signing identity')
    if not path.exists() or not status(path.stem, poll=False).get('authorized'):
        raise ToolError('Authorize this app with store authorize first; no token is needed in the prompt or command line')
    credential = json.loads(path.read_text())
    environment.call(['publish', package, '--check-only'])
    app_id = credential['app_id']
    try:
        for kind, filename in [('icon', icon), *[('screenshot', path) for path in screenshots]]:
            if filename:
                image = environment.project(filename)
                upload(server, f'/api/v2/apps/{app_id}/media', credential, image, field='image', extra={'kind': kind})
        result, sha = upload(server, f'/api/v2/apps/{app_id}/releases', credential, package, extra={'changelog': changelog})
    except CredentialRejected:
        credential.pop('token', None)
        credential.pop('grant', None)
        atomic_json(path, credential, private=True)
        raise
    if result.get('sha256') != sha or result.get('status') != 'pending':
        raise ToolError('The store did not acknowledge the exact signed package as pending review')
    return {'app_id': app_id, 'status': 'pending_review', 'sha256': sha,
            'dashboard_url': credential['dashboard_url'], 'publicly_listed': False}

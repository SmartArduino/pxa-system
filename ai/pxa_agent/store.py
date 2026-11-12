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
import unicodedata
import warnings
from urllib.error import HTTPError
from urllib.parse import urlencode, urlparse
import uuid
import webbrowser

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from .common import ToolError, atomic_json, data_root, digest, request, locked
from .environment import sdk_data
from .account_credentials import (account_path, read_account, save_account, forget_account,
                                  clear_app_credentials, credential_directory)

class CredentialRejected(ToolError):
    pass


ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def server_origin(server):
    url = urlparse(server)
    if url.scheme != 'https' or not url.hostname or url.username or url.password or url.query or url.fragment or url.path not in ('', '/'):
        raise ToolError('Choose the canonical HTTPS store origin')
    return server.rstrip('/')


def api(server, path, value=None, *, method=None, credential=None):
    data = json.dumps(value).encode() if value is not None else None
    try:
        with request(server + path, data=data, method=method or ('POST' if data else 'GET'),
                     headers={'Accept': 'application/json', **({'Content-Type': 'application/json'} if data else {}),
                              **({'Authorization': 'Bearer ' + credential['token']} if credential else {})}) as response:
            return json.load(response)
    except HTTPError as error:
        if error.code == 401 and credential:
            raise CredentialRejected('Upload authorization expired or was revoked. Run store authorize again.') from error
        if error.code in (400, 429):
            result = json.load(error)
            if result.get('error') in ('authorization_pending', 'slow_down', 'expired_token', 'access_denied', 'rate_limited'):
                return result
        raise ToolError(f'Store request failed ({error.code}); check account permissions and the server response') from error


def listing(project):
    path = project / 'store.json'
    value = json.loads(path.read_text()) if path.is_file() else {}
    if not isinstance(value, dict):
        raise ToolError('store.json must be an object')
    if 'tags' in value:
        if not isinstance(value['tags'], list) or not all(isinstance(tag, str) for tag in value['tags']):
            raise ToolError('store.json tags must be a list of labels')
        labels = []
        for raw in value['tags']:
            tag = unicodedata.normalize('NFKC', raw).strip().removeprefix('#').strip()
            if any(unicodedata.category(c) in ('Cc', 'Cf', 'Cs') for c in tag) or re.search('[,，、;；]', tag):
                raise ToolError('Tags cannot contain separators or control characters')
            tag = ' '.join(tag.split())
            if len(tag) > 24:
                raise ToolError('Each tag must have at most 24 characters')
            if tag and tag.casefold() not in [v.casefold() for v in labels]:
                labels.append(tag)
        if len(labels) > 10:
            raise ToolError('Use at most 10 tags')
        value['tags'] = labels
    for field in ('kind', 'category'):
        if field in value:
            if not isinstance(value[field], str):
                raise ToolError(f'store.json {field} must be a string')
            value[field] = value[field].strip().lower()
    for field in ('icon', 'cover'):
        if field in value and value[field] is not None and (not isinstance(value[field], str) or not value[field]):
            raise ToolError(f'store.json {field} must be a nonempty image path or null to skip upload')
    return value


def listing_text(project, metadata):
    package = json.loads((project / 'package.json').read_text())
    values = {'name': metadata.get('name', package.get('name', package['id'])),
              'summary': metadata.get('summary', package.get('description', '')),
              'description': metadata.get('description', '')}
    issues = {}
    for field, minimum, maximum in [('name', 2, 100), ('summary', 5, 200), ('description', 0, 10000)]:
        raw = values[field]
        if not isinstance(raw, str):
            issues[field] = 'must be text'
        else:
            values[field] = raw.strip()
            if not minimum <= len(values[field]) <= maximum:
                issues[field] = f'must contain {minimum}..{maximum} characters'
    return {'draft': values, 'needs_text': list(issues), 'issues': issues}


def prepare(environment, project, *, server='https://app.doit.am', icon=None, cover=None,
            screenshots=(), skip_icon=False, skip_cover=False):
    """Read-only listing/media preflight. All files are checked before any upload."""
    from PIL import Image, ImageOps
    server = server_origin(server)
    project = environment.project(project)
    metadata = listing(project)
    spec = api(server, '/api/v2/creator/publishing-spec')
    if spec.get('schema') != 'pxa-publishing-spec-1':
        raise ToolError('The store did not return a supported publishing specification')
    taxonomy = spec['taxonomy']
    kind = metadata.get('kind')
    category = metadata.get('category', '')
    if (kind is not None and kind not in [v['value'] for v in taxonomy['kinds']]) or (category and not any(
            (kind is None or v['kind'] == kind) and v['value'] == category for v in taxonomy['categories'])):
        raise ToolError('Choose a valid kind and category from the store publishing specification')
    selected = {'icon': icon, 'cover': cover}
    omitted, needs_choice, notes, images = [], [], [], []
    for name, skip in [('icon', skip_icon), ('cover', skip_cover)]:
        if skip and selected[name]:
            raise ToolError(f'Cannot both upload and skip {name}')
        if skip:
            selected[name] = None
            omitted.append(name)
        elif selected[name] is None:
            if name in metadata and metadata[name] is None:
                omitted.append(name)
            elif metadata.get(name):
                selected[name] = project / metadata[name]
            else:
                needs_choice.append(name)
    files = [(kind, value) for kind, value in selected.items() if value]
    files += [('screenshot', filename) for filename in screenshots]
    if len(screenshots) > spec['media']['screenshot']['max_count']:
        raise ToolError('Too many store screenshots')
    for media_kind, filename in files:
        path = environment.project(filename)
        policy = spec['media'][media_kind]
        if not path.is_file() or not 0 < path.stat().st_size <= policy['max_bytes']:
            raise ToolError(f'{media_kind} file is missing, empty or exceeds the store upload limit')
        try:
            with warnings.catch_warnings():
                warnings.simplefilter('error', Image.DecompressionBombWarning)
                with Image.open(path) as opened:
                    if opened.format not in spec['media']['formats']:
                        raise ToolError('Store images must be PNG, JPEG or WebP')
                    if opened.width * opened.height > spec['media']['max_pixels']:
                        raise ToolError('Store image exceeds the decoded pixel limit')
                    opened.verify()
                with Image.open(path) as opened:
                    dimensions = ImageOps.exif_transpose(opened).size
        except (OSError, ValueError, Image.DecompressionBombError, Image.DecompressionBombWarning) as error:
            raise ToolError(f'Cannot decode {media_kind} image: {path.name}') from error
        if media_kind in ('icon', 'cover'):
            width, height = policy['recommended_width'], policy['recommended_height']
            if dimensions[0] * height != dimensions[1] * width:
                notes.append(f'{media_kind}: recommended {width}x{height}; current {dimensions[0]}x{dimensions[1]}')
        images.append({'kind': media_kind, 'path': str(path), 'width': dimensions[0], 'height': dimensions[1]})
    from .authoring import STORE_LISTING_GUIDANCE
    return {'listing': metadata, 'listing_text': listing_text(project, metadata),
            'listing_guidance': STORE_LISTING_GUIDANCE, 'specification': spec, 'images': images,
            'omitted': omitted, 'needs_choice': needs_choice, 'warnings': notes,
            'workflow': spec.get('workflow', {}),
            'checklist': {'listing': ['name', 'summary', 'kind', 'category', 'tags', 'changelog'],
                          'package': 'store build: mandatory separate Wasm package plus requested native targets, one version/sequence; then publish-targets',
                          'icon': 'Generate matching artwork or a simple app-level vector icon; preserve asset rights',
                          'cover': 'Generate only if image generation is available; otherwise skip the optional cover',
                          'screenshots': 'verify --store-screenshots; inspect each original rectangular simulator capture',
                          'authorization': 'Reuse remembered account; first login uses browser approval, never pasted secrets',
                          'result': 'Submission remains pending review'}}


def package_identity(environment, package):
    from .host_tools import client
    script = client(environment=environment)
    spec = spec_from_file_location('pxa_agent_package_reader', script)
    module = module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    records = list(module.manifest_records(module.read_package_manifest(package)[12:]))
    fields = dict(records)
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
    targets, contains_wasm = set(), False
    for tag, component in records:
        if tag != 16:
            continue
        for field, value in module.manifest_records(component):
            if field != 4:
                continue
            artifact = dict(module.manifest_records(value))
            if artifact.get(1) == b'\x02':
                target = artifact.get(3, b'').decode('ascii')
                if not target:
                    raise ToolError('An AOT artifact has no target in the signed manifest')
                targets.add(target)
            elif artifact.get(1) == b'\x01':
                contains_wasm = True
    sequence = fields.get(9, b'')
    if len(sequence) != 8:
        raise ToolError('Invalid release sequence in the signed manifest')
    return {'app_id': module.package_manifest_identity(package), 'spki': public, 'root': root,
            'version': fields.get(3, b'').decode('ascii'), 'release_sequence': struct.unpack('<Q', sequence)[0],
            'aot_targets': sorted(targets), 'contains_wasm': contains_wasm}


def cache_path(server, identity):
    name = hashlib.sha256((server + '\0' + identity['app_id'] + '\0' + identity['root']).encode()).hexdigest()
    directory = data_root() / 'credentials'
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    return directory / (name + '.json')


def _open_approval(server, grant, open_browser):
    uri = grant['verification_uri_complete']
    if server_origin(uri.split('/developer/authorize', 1)[0]) != server:
        raise ToolError('The store returned a different browser authorization origin')
    if open_browser:
        webbrowser.open(uri)


def login(*, server='https://app.doit.am', open_browser=True, renew=False):
    import socket
    server = server_origin(server)
    if renew:
        logout(server=server)
    path = account_path(server)
    saved = read_account(server)
    if saved and saved.get('credential'):
        try:
            result = account_status(server, refresh=True)
            return {**result, 'status': 'authorized', 'credential_ref': path.stem}
        except ToolError:
            if read_account(server) is not None:
                raise
    elif saved and saved.get('session_id'):
        raise ToolError('Unlock or restore this computer credential store, then retry store login')
    with locked(path.with_suffix('.lock')):
        saved = read_account(server)
        if saved and saved.get('credential'):
            return {**account_status(server), 'status': 'authorized', 'credential_ref': path.stem}
        if saved and saved.get('grant') and saved.get('grant_expires_at', 0) > time.time():
            grant = saved['grant']
        else:
            client_name = ('PXA Agent Kit · ' + socket.gethostname())[:80]
            grant = api(server, '/api/v2/creator/login', {'client_name': client_name})
            if grant.get('error'):
                raise ToolError('Store login could not start: ' + grant['error'])
            _open_approval(server, grant, False)
            atomic_json(path, {'kind': 'creator-account', 'server': server, 'grant': grant,
                'grant_expires_at': time.time() + grant['expires_in'],
                'next_poll_at': time.time() + grant['interval']}, private=True)
        _open_approval(server, grant, open_browser)
        return {'status': 'awaiting_browser_approval', 'user_code': grant['user_code'],
            'verification_uri': grant['verification_uri_complete'], 'expires_in': grant['expires_in'],
            'poll_after_seconds': grant['interval'], 'credential_ref': path.stem}


def account_status(server='https://app.doit.am', *, refresh=False):
    server = server_origin(server)
    path = account_path(server)

    def inspect():
        saved = read_account(server)
        if not saved or not saved.get('credential'):
            return {'authorized': False, 'server': server,
                    'error': 'credential_unavailable' if saved and saved.get('session_id') else 'login_required'}
        if refresh:
            try:
                result = api(server, '/api/v2/creator/session/refresh', {}, credential={'token': saved['credential']})
            except CredentialRejected:
                forget_account(server)
                clear_app_credentials(server)
                raise ToolError('This computer authorization was revoked or expired. Run store login again.') from None
            saved.update(result, expires_at=time.time() + result['expires_in'])
            save_account(server, saved)
        return {**{key: saved.get(key) for key in ('session_id', 'user_id', 'display_name', 'client_name', 'credential_storage')},
                'server': server, 'authorized': saved.get('expires_at', 0) > time.time(), 'scope': 'creator:publish',
                'credential_ref': path.stem}

    if refresh:
        with locked(path.with_suffix('.lock')):
            return inspect()
    return inspect()


def logout(*, server='https://app.doit.am'):
    server = server_origin(server)
    path = account_path(server)
    remote_revoked, warning = False, None
    with locked(path.with_suffix('.lock')):
        saved = read_account(server)
        if saved and saved.get('credential'):
            try:
                api(server, '/api/v2/creator/session/logout', {}, credential={'token': saved['credential']})
                remote_revoked = True
            except CredentialRejected:
                remote_revoked = True
            except (ToolError, OSError):
                warning = 'Local credentials removed. Revoke this computer in the store account when online.'
        forget_account(server)
        clear_app_credentials(server)
    return {'logged_out': True, 'server': server, 'remote_revoked': remote_revoked, 'warning': warning}


def authorize(environment, project, package, *, server='https://app.doit.am', key=None, open_browser=True, renew=False, remember=True):
    server = server_origin(server)
    project = environment.project(project)
    package = environment.project(package)
    environment.call(['publish', package, '--check-only'])
    identity = package_identity(environment, package)
    cached = cache_path(server, identity)
    current_key = hashlib.sha256(identity['spki']).hexdigest()
    account = read_account(server)
    if account and account.get('session_id') and not account.get('credential'):
        raise ToolError('Unlock or restore this computer credential store before authorizing an app')
    same_account = (not account and not (json.loads(cached.read_text()).get('creator_session_id') if cached.is_file() else None))
    if account and cached.is_file():
        same_account = json.loads(cached.read_text()).get('creator_session_id') == account.get('session_id')
    if (not renew and cached.is_file() and json.loads(cached.read_text()).get('current_key') == current_key
            and same_account and status(cached.stem, poll=False).get('authorized')):
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
    listing_data = listing(project)
    text = listing_text(project, listing_data)
    if text['needs_text']:
        raise ToolError('Complete factual store.json listing text before authorization: ' +
                        ', '.join(field + ' ' + issue for field, issue in text['issues'].items()))
    payload = {'client_name': 'PXA Agent Kit', 'app_id': identity['app_id'],
               'lineage_root_key_id': identity['root'], 'publisher_spki': base64.b64encode(public).decode(),
               **text['draft'], 'kind': listing_data.get('kind', 'app'),
               'category': listing_data.get('category', ''), 'tags': listing_data.get('tags', []), 'remember': remember}
    if account and account.get('credential') and not renew:
        account_status(server, refresh=True)
        account = read_account(server)
    credential = {'token': account['credential']} if account and account.get('credential') and not renew else None
    grant = api(server, '/api/v2/creator/authorize', payload, **({'credential': credential} if credential else {}))
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
        'creator_session_id': account.get('session_id') if credential else None,
        'grant': grant, 'grant_expires_at': time.time() + grant['expires_in'], 'next_poll_at': time.time() + grant['interval']}, private=True)
    if grant.get('account_authorized'):
        result = status(path.stem)
        if not result.get('authorized'):
            raise ToolError('The store did not complete the remembered app authorization')
        return {**result, 'credential_ref': path.stem, 'status': 'authorized'}
    _open_approval(server, grant, open_browser)
    return {'status': 'awaiting_browser_approval', 'user_code': grant['user_code'],
            'verification_uri': grant['verification_uri_complete'], 'expires_in': grant['expires_in'],
            'poll_after_seconds': grant['interval'], 'credential_ref': path.stem}


def status(reference, *, poll=True):
    if not re.fullmatch('[a-f0-9]{64}', reference):
        raise ToolError('Invalid local credential reference')
    path = data_root() / 'credentials' / (reference + '.json')
    with locked(path.with_suffix('.lock')):
        return _status(path, poll=poll)


def _status(path, *, poll=True):
    saved = json.loads(path.read_text())
    if saved.get('kind') == 'creator-account' and not saved.get('grant'):
        return account_status(saved['server'])
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
    if result.get('account'):
        account = result['account']
        def remember_result():
            current = json.loads(path.read_text()) if path.is_file() else {}
            if current.get('grant', {}).get('device_code') != grant['device_code']:
                try:
                    api(saved['server'], '/api/v2/creator/session/logout', {},
                        credential={'token': account['credential']})
                except (ToolError, OSError):
                    pass
                raise ToolError('This login was cancelled locally; run store login again if needed')
            save_account(saved['server'], {**account, 'expires_at': time.time() + account['expires_in']})
        account_file = account_path(saved['server'])
        if account_file == path:
            remember_result()  # status already holds this account's lock.
        else:
            with locked(account_file.with_suffix('.lock')):
                remember_result()
        saved['creator_session_id'] = account['session_id']
        if not result.get('token'):
            return account_status(saved['server'])
    if result.get('token'):
        saved.update({key: result[key] for key in ('token', 'app_id', 'scope', 'dashboard_url', 'display_name')})
        saved['token_expires_at'] = time.time() + result['expires_in']
        saved.pop('grant', None)
    elif result.get('error') in ('expired_token', 'access_denied'):
        saved.pop('grant', None)
    atomic_json(path, saved, private=True)
    if result.get('token'):
        return _status(path, poll=False)
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


def publish(environment, project, package, *, reference=None, server='https://app.doit.am', icon=None,
            cover=None, screenshots=(), skip_icon=False, skip_cover=False, changelog='', allow_universal=False,
            update_listing=True):
    from .scope import enforce
    enforce(environment.workspace)
    server = server_origin(server)
    project, package = environment.project(project), environment.project(package)
    identity = package_identity(environment, package)
    if not allow_universal and len(identity.get('aot_targets', [])) > 1:
        raise ToolError('Store uploads default to one target per .pxa. Use store build --targets and publish-targets, '
                        'or explicitly choose --allow-universal for an intentional universal package.')
    path = cache_path(server, identity)
    if reference and path.stem != reference:
        raise ToolError('The authorization belongs to a different server or app signing identity')
    if not path.exists() or not status(path.stem, poll=False).get('authorized'):
        account = read_account(server)
        if account and account.get('credential'):
            authorize(environment, project, package, server=server, open_browser=False)
        else:
            raise ToolError('Run store login once, then publish; credentials stay on this computer')
    account = read_account(server)
    cached = json.loads(path.read_text())
    if account and cached.get('creator_session_id') != account.get('session_id'):
        authorize(environment, project, package, server=server, open_browser=False)
    credential = json.loads(path.read_text())
    environment.call(['publish', package, '--check-only'])
    try:
        if identity.get('aot_targets'):
            query = urlencode({'version': identity['version'], 'release_sequence': identity['release_sequence'],
                               'publisher_key_id': hashlib.sha256(identity['spki']).hexdigest()})
            portable = api(server, f'/api/v2/apps/{credential["app_id"]}/releases/wasm?' + query,
                           credential=credential)
            if portable.get('wasm_uploaded') is not True:
                raise ToolError('Upload a separate Wasm package with the same version, release sequence and signer first. '
                                'Use store build and publish-targets to submit the complete release.')
        prepared = prepare(environment, project, server=server, icon=icon, cover=cover, screenshots=screenshots,
                           skip_icon=skip_icon, skip_cover=skip_cover)
        if prepared['needs_choice']:
            raise ToolError('Choose images or explicitly skip upload for: ' + ', '.join(prepared['needs_choice']) +
                            '. Run store prepare for the specifications; use --skip-icon/--skip-cover to keep existing media.')
        app_id = credential['app_id']
        changes = {key: value for key, value in prepared['listing'].items()
                   if key in ('name', 'summary', 'description', 'kind', 'category', 'tags')}
        listing_update = None
        media_updates = []
        if changes and update_listing:
            listing_update = api(server, f'/api/v2/apps/{app_id}/listing', changes, method='PATCH', credential=credential)
        for item in prepared['images']:
            media_result, _ = upload(server, f'/api/v2/apps/{app_id}/media', credential, item['path'], field='image', extra={'kind': item['kind']})
            media_updates.append({key: media_result[key] for key in ('kind', 'review_status', 'public_listing_retained')
                                  if key in media_result})
        result, sha = upload(server, f'/api/v2/apps/{app_id}/releases', credential, package, extra={'changelog': changelog})
    except CredentialRejected:
        credential.pop('token', None)
        credential.pop('grant', None)
        atomic_json(path, credential, private=True)
        raise
    if result.get('sha256') != sha or result.get('status') != 'pending':
        raise ToolError('The store did not acknowledge the exact signed package as pending review')
    return {'app_id': app_id, 'status': 'pending_review', 'sha256': sha,
            'listing_update': listing_update,
            'media_updates': media_updates,
            'dashboard_url': credential['dashboard_url'], 'publicly_listed': False,
            'media_uploaded': [item['kind'] for item in prepared['images']], 'media_omitted': prepared['omitted'],
            'media_needs_choice': prepared['needs_choice'], 'warnings': prepared['warnings']}

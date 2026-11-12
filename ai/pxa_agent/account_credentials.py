"""Private per-store computer credentials, with an optional native keyring."""
import hashlib
import json
import os
from pathlib import Path

from .common import ToolError, atomic_json, data_root

SERVICE = 'PXA Creator Publishing'


def credential_directory():
    path = data_root() / 'credentials'
    if path.is_symlink():
        raise ToolError('Refusing a linked credential directory')
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    if os.name != 'nt':
        if path.stat().st_uid != os.getuid():
            raise ToolError('Credential directory belongs to another user')
        path.chmod(0o700)
    return path


def account_path(server):
    return credential_directory() / (hashlib.sha256(('account\0' + server).encode()).hexdigest() + '.json')


def native_keyring():
    try:
        import keyring
        backend = keyring.get_keyring()
        for candidate in getattr(backend, 'backends', [backend]):
            if type(candidate).__module__ in ('keyring.backends.SecretService', 'keyring.backends.macOS',
                                             'keyring.backends.Windows', 'keyring.backends.libsecret'):
                return candidate
    except Exception:
        pass
    return None


def read_account(server):
    path = account_path(server)
    if path.is_symlink():
        raise ToolError('Refusing a linked credential file')
    if not path.is_file():
        return None
    if os.name != 'nt' and (path.stat().st_uid != os.getuid() or path.stat().st_mode & 0o077):
        raise ToolError('Account credential file must be private to its owner')
    saved = json.loads(path.read_text())
    if saved.get('server') != server or saved.get('kind') != 'creator-account':
        raise ToolError('Account credential origin does not match')
    if saved.get('credential_storage') == 'system-keyring':
        ring = native_keyring()
        try:
            saved['credential'] = ring.get_password(SERVICE, path.stem) if ring else None
        except Exception:
            saved['credential'] = None
    return saved


def save_account(server, saved):
    path = account_path(server)
    value = {**saved, 'kind': 'creator-account', 'server': server}
    secret = value['credential']
    if (not isinstance(secret, str) or not secret.startswith('pxa_creator_') or not 32 <= len(secret) <= 128
            or value.get('scope') != 'creator:publish' or not value.get('session_id')):
        raise ToolError('The store returned an invalid computer publishing credential')
    ring = native_keyring()
    if ring:
        try:
            ring.set_password(SERVICE, path.stem, secret)
            value.pop('credential')
            value['credential_storage'] = 'system-keyring'
        except Exception:
            value['credential_storage'] = 'private-file'
    else:
        value['credential_storage'] = 'private-file'
    atomic_json(path, value, private=True)


def forget_account(server):
    path = account_path(server)
    saved = read_account(server)
    if saved and saved.get('credential_storage') == 'system-keyring':
        ring = native_keyring()
        if ring:
            try:
                ring.delete_password(SERVICE, path.stem)
            except Exception:
                pass
    path.unlink(missing_ok=True)


def clear_app_credentials(server):
    for path in credential_directory().glob('*.json'):
        if path.is_symlink():
            continue
        saved = json.loads(path.read_text())
        if saved.get('server') == server and saved.get('kind') != 'creator-account':
            for key in ('token', 'token_expires_at', 'grant', 'grant_expires_at'):
                saved.pop(key, None)
            atomic_json(path, saved, private=True)

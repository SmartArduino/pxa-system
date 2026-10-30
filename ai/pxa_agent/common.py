from __future__ import annotations

from contextlib import contextmanager
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

KIT = Path(__file__).resolve().parents[1]


class ToolError(Exception):
    pass


def data_root():
    if value := os.environ.get('PXA_AGENT_HOME'):
        return Path(value).expanduser().resolve()
    base = os.environ.get('LOCALAPPDATA') if os.name == 'nt' else os.environ.get('XDG_DATA_HOME')
    return (Path(base) if base else Path.home() / '.local/share') / 'pxa-agent'


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        while block := source.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def atomic_json(path, value, *, private=False):
    path = Path(path)
    if path.is_symlink():
        raise ToolError('Refusing a symlink: ' + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.pxa-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as target:
            json.dump(value, target, ensure_ascii=False, sort_keys=True, indent=2)
            target.write('\n')
        os.chmod(temporary, 0o600 if private else 0o644)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


@contextmanager
def locked(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            if not stream.tell():
                stream.write(b'\0')
                stream.flush()
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


class SecureRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        if urlparse(new_url).scheme != 'https':
            raise ToolError('An HTTPS download redirected to an insecure URL')
        if request.get_header('Authorization'):
            raise ToolError('Credential-bearing requests must not redirect')
        return super().redirect_request(request, fp, code, message, headers, new_url)


def request(url, *, data=None, headers=None, method=None, timeout=60):
    if urlparse(url).scheme != 'https':
        raise ToolError('Network access requires HTTPS')
    from . import VERSION
    attempts = 3 if data is None and method in (None, 'GET', 'HEAD') else 1
    opener = build_opener(SecureRedirect)
    for attempt in range(attempts):
        try:
            return opener.open(Request(url, data=data, method=method,
                headers={'User-Agent': 'PXA-Agent-Kit/' + VERSION, **(headers or {})}), timeout=timeout)
        except HTTPError as error:
            if error.code not in (502, 503, 504) or attempt == attempts - 1:
                raise
            error.close()
        except (URLError, TimeoutError) as error:
            if attempt == attempts - 1:
                raise ToolError(f'Network request to {urlparse(url).hostname} failed; check the connection and retry the current command. Existing project files and credentials were retained.') from error
        time.sleep(.5 * (attempt + 1))


def download(source, target, *, limit=1024 * 1024):
    source = str(source)
    stream = request(source) if urlparse(source).scheme else Path(source).open('rb')
    total = 0
    with stream, Path(target).open('wb') as output:
        while block := stream.read(1024 * 1024):
            total += len(block)
            if total > limit:
                raise ToolError('Download exceeds its size limit')
            output.write(block)


def signed_document(source, *, schema=None):
    with tempfile.TemporaryDirectory(prefix='pxa-manifest-') as temporary:
        path = Path(temporary) / 'manifest.json'
        download(source, path)
        download(str(source) + '.sig', str(path) + '.sig', limit=4096)
        payload = path.read_bytes()
        key = serialization.load_pem_public_key((KIT / 'release-public.pem').read_bytes())
        if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
            raise ToolError('The release trust anchor is not P-256')
        try:
            key.verify(Path(str(path) + '.sig').read_bytes(), payload, ec.ECDSA(hashes.SHA256()))
        except Exception as error:
            raise ToolError('Release signature verification failed') from error
        document = json.loads(payload)
        if schema and (not isinstance(document, dict) or document.get('schema') != schema):
            raise ToolError('Unexpected release manifest schema')
        return document, payload, Path(str(path) + '.sig').read_bytes()


def artifact(source, expected, *, maximum=2 * 1024**3):
    name, size, sha = expected.get('file'), expected.get('size', expected.get('bytes')), expected.get('sha256')
    if (not isinstance(name, str) or Path(name).name != name or '\\' in name
            or not isinstance(size, int) or not 0 < size <= maximum
            or not isinstance(sha, str) or len(sha) != 64 or any(c not in '0123456789abcdef' for c in sha)):
        raise ToolError('Invalid signed artifact metadata')
    cache = data_root() / 'downloads' / sha
    with locked(cache.with_suffix('.lock')):
        if cache.is_file() and cache.stat().st_size == size and digest(cache) == sha:
            return cache
        cache.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(prefix='.download-', dir=cache.parent)
        os.close(descriptor)
        try:
            download(source, temporary, limit=size)
            if Path(temporary).stat().st_size != size or digest(temporary) != sha:
                raise ToolError('Downloaded artifact does not match its signed hash: ' + name)
            os.replace(temporary, cache)
        finally:
            Path(temporary).unlink(missing_ok=True)
    return cache


def extract(archive, destination):
    with tarfile.open(archive) as source:
        members = source.getmembers()
        names, total = set(), 0
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or '..' in path.parts or path.as_posix() in names
                    or not (member.isfile() or member.isdir() or member.issym() or member.islnk())):
                raise ToolError('Unsafe release archive')
            names.add(path.as_posix())
            total += member.size
            if total > 6 * 1024**3 or len(names) > 100000:
                raise ToolError('Release archive expands beyond its limit')
            existing = Path(destination) / member.name
            if existing.exists() and not (member.isdir() and existing.is_dir()):
                raise ToolError('Release components contain overlapping files')
        source.extractall(destination, members=members, filter='data')


def run(arguments, *, cwd=None, env=None, timeout=300):
    result = subprocess.run([str(arg) for arg in arguments], cwd=cwd, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise ToolError(f'Command failed ({result.returncode}):\n' + result.stdout[-16000:])
    return result.stdout


def evidence(workspace, topic='pxa-agent'):
    path = Path(workspace) / 'local/artifacts' / topic / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    path.mkdir(parents=True)
    return path

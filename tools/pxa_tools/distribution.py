"""Authenticate and install the same component archives online or offline."""
from __future__ import annotations

import json
import os
from pathlib import Path, PurePosixPath
import re
import platform
import shutil
import subprocess
import tarfile
import tempfile
from urllib.parse import urljoin, urlparse
from urllib.request import urlopen

from .common import ToolError, digest, locked
from .project import SDK_ROOT


def data_root():
    return Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'pxa'


def fetch(source: str, destination: Path, *, offline=False, limit=2 * 1024**3):
    parsed = urlparse(source)
    if parsed.scheme:
        if offline or parsed.scheme != 'https':
            raise ToolError('Downloads require HTTPS; offline mode never accesses the network')
        stream = urlopen(source, timeout=60)
        if urlparse(stream.geturl()).scheme != 'https':
            stream.close()
            raise ToolError('Insecure download redirect')
    else:
        stream = Path(source).open('rb')
    with stream, destination.open('wb') as target:
        total = 0
        while block := stream.read(1024 * 1024):
            total += len(block)
            if total > limit:
                raise ToolError('Distribution download exceeds size limit')
            target.write(block)


def verify_manifest(path: Path, signature: Path, *, root=SDK_ROOT):
    public = root / 'config/release-public.pem'
    if not public.is_file():
        raise ToolError('This SDK has no trusted distribution public key')
    openssl = root / 'runtime/bin/openssl'
    result = subprocess.run([str(openssl) if openssl.is_file() else 'openssl', 'dgst',
                             '-sha256', '-verify', str(public), '-signature', str(signature), str(path)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ToolError('Distribution manifest signature verification failed')
    document = json.loads(path.read_bytes())
    if not isinstance(document, dict) or document.get('schema') != 'pxa-distribution-1':
        raise ToolError('Unsupported distribution manifest')
    return document


def extract(archive: Path, destination: Path, *, maximum=6 * 1024**3):
    """Bound expansion and reject traversal, device files and duplicate paths."""
    with tarfile.open(archive, 'r:*') as source:
        members = source.getmembers()
        total, names = 0, set()
        for member in members:
            path = PurePosixPath(member.name)
            normalized = path.as_posix()
            if (path.is_absolute() or '..' in path.parts or normalized in names
                    or not (member.isfile() or member.isdir() or member.issym() or member.islnk())):
                raise ToolError('Unsafe distribution archive member')
            names.add(normalized)
            total += member.size
            if total > maximum or len(names) > 100000:
                raise ToolError('Distribution archive exceeds expansion limit')
            existing = destination / member.name
            if existing.exists() and not (member.isdir() and existing.is_dir()):
                raise ToolError('Overlapping distribution component files')
        # Python's data filter also checks link targets and parent symlinks.
        source.extractall(destination, members=members, filter='data')


def install(release: str, manifest: str | None, *, offline=False):
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ToolError('This release supports Linux x86_64')
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?', release):
        raise ToolError('Expected an explicit release version')
    manifest = manifest or f'https://pxa.doit.am/downloads/{release}/distribution.json'
    if urlparse(manifest).scheme:
        base = manifest
        locate = lambda name: urljoin(base, name)
    else:
        base = Path(manifest).resolve()
        locate = lambda name: str(base.parent / name)
    destination = data_root() / 'releases' / release
    with locked(destination.parent / (destination.name + '.lock')):
        with tempfile.TemporaryDirectory(prefix='pxa-distribution-') as td:
            temporary = Path(td)
            local_manifest = temporary / 'distribution.json'
            signature = temporary / 'distribution.json.sig'
            fetch(manifest, local_manifest, offline=offline, limit=1024**2)
            fetch(manifest + '.sig', signature, offline=offline, limit=4096)
            document = verify_manifest(local_manifest, signature)
            if document.get('version') != release or document.get('platform') != 'linux-x86_64':
                raise ToolError('Distribution version/platform mismatch')
            required_glibc = tuple(int(part) for part in document['minimum_glibc'].split('.'))
            libc, libc_version = platform.libc_ver()
            if libc != 'glibc' or tuple(int(part) for part in libc_version.split('.')) < required_glibc:
                raise ToolError(f"This release requires glibc >= {document['minimum_glibc']}")
            if destination.is_dir():
                receipt = destination / 'distribution.json'
                if not receipt.is_file() or digest(receipt) != digest(local_manifest):
                    raise ToolError('Installed release differs; versions are immutable')
                return destination
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='.install-', dir=destination.parent) as stage_dir:
                stage = Path(stage_dir) / 'sdk'
                stage.mkdir()
                components = document.get('components')
                if not isinstance(components, list) or not components:
                    raise ToolError('Distribution has no components')
                for component in components:
                    name = component['file']
                    if not re.fullmatch(r'[A-Za-z0-9._-]+\.tar\.gz', name):
                        raise ToolError('Invalid distribution component name')
                    archive = temporary / name
                    fetch(locate(name), archive, offline=offline, limit=component['size'])
                    if archive.stat().st_size != component['size'] or digest(archive) != component['sha256']:
                        raise ToolError(f'Distribution checksum mismatch: {name}')
                    extract(archive, stage)
                shutil.copy2(local_manifest, stage / 'distribution.json')
                shutil.copy2(signature, stage / 'distribution.json.sig')
                if not (stage / 'bin/pxa').is_file():
                    raise ToolError('Distribution launcher is missing')
                os.replace(stage, destination)
    return destination

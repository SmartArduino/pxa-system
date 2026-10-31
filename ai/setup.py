#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = ["cryptography==50.0.2"]
# ///
"""Verify and install the PXA Agent Kit, then prepare a project-local environment.

Run: uv run --python 3.12 https://pxa.doit.am/setup.py --workspace ./my-pxa
Only HTTPS or explicit offline local files are accepted. No model API key needed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import tempfile
from urllib.parse import urljoin, urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

PUBLIC_KEY = b'''-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE5kafTuRt9N4kN7dC8M7tbskgb3eN
uB2Pn03qdZCRVpgan+2bkd3aO2ug3IxYTp6N/+ywKe5e6tZO4HMnd9zI8A==
-----END PUBLIC KEY-----
'''


class HTTPSRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, url):
        if urlparse(url).scheme != 'https':
            raise ValueError('An HTTPS release redirected to an insecure URL')
        return super().redirect_request(req, fp, code, msg, headers, url)


def fetch(source, limit):
    if urlparse(source).scheme:
        if urlparse(source).scheme != 'https':
            raise ValueError('Release downloads require HTTPS')
        stream = build_opener(HTTPSRedirect).open(Request(source, headers={'User-Agent': 'PXA-Agent-Setup/0.4.2'}), timeout=120)
    else:
        stream = Path(source).open('rb')
    with stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Release exceeds its size limit')
    return data


def data_root():
    if override := os.environ.get('PXA_AGENT_HOME'):
        return Path(override).expanduser().resolve()
    base = os.environ.get('LOCALAPPDATA') if os.name == 'nt' else os.environ.get('XDG_DATA_HOME')
    return (Path(base) if base else Path.home() / '.local/share') / 'pxa-agent'


def install(source):
    raw = fetch(source, 1024 * 1024)
    signature = fetch(source + '.sig', 4096)
    public = serialization.load_pem_public_key(PUBLIC_KEY)
    public.verify(signature, raw, ec.ECDSA(hashes.SHA256()))
    manifest = json.loads(raw)
    version = manifest['version']
    import re
    if manifest.get('schema') != 'pxa-ai-distribution-1' or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid signed AI-kit manifest')
    info = manifest['archive']
    name, size, sha = info['file'], info['size'], info['sha256']
    if (name != f'pxa-ai-kit-{version}.tar.gz' or type(size) is not int or not 0 < size < 32 * 1024**2
            or not re.fullmatch('[a-f0-9]{64}', sha)):
        raise ValueError('Invalid signed archive metadata')
    root = data_root() / 'kits'
    root.mkdir(parents=True, exist_ok=True)
    target = root / version
    if target.is_symlink():
        raise ValueError('Refusing a symlink kit installation')
    receipt = target / 'release.json'
    if target.exists():
        if not receipt.is_file() or receipt.read_bytes() != raw:
            raise ValueError('The installed kit differs from this immutable release; keep it for inspection')
        return target
    location = info.get('url') if urlparse(source).scheme else str(Path(source).resolve().parent / name)
    location = location or urljoin(source, name)
    data = fetch(location, size)
    if len(data) != size or hashlib.sha256(data).hexdigest() != sha:
        raise ValueError('AI-kit archive does not match the signed hash')
    with tempfile.TemporaryDirectory(prefix='.kit-', dir=root) as temporary:
        archive = Path(temporary) / name
        archive.write_bytes(data)
        stage = Path(temporary) / 'unpacked'
        stage.mkdir()
        with tarfile.open(archive) as package:
            total, seen = 0, set()
            for member in package.getmembers():
                path = PurePosixPath(member.name)
                if (path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != f'pxa-ai-kit-{version}'
                        or member.name in seen or not (member.isfile() or member.isdir())):
                    raise ValueError('Unsafe AI-kit archive member')
                seen.add(member.name)
                total += member.size
                if total > 128 * 1024**2 or len(seen) > 5000:
                    raise ValueError('AI-kit archive expands beyond its limit')
            package.extractall(stage, filter='data')
        folder = stage / f'pxa-ai-kit-{version}'
        files = json.loads((folder / 'manifest.json').read_text())['files']
        actual = {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()}
        if actual != set(files) | {'manifest.json'}:
            raise ValueError('Unexpected AI-kit contents')
        for relative, expected in files.items():
            if hashlib.sha256((folder / relative).read_bytes()).hexdigest() != expected:
                raise ValueError('AI-kit file integrity check failed')
        if not (folder / 'agent.py').is_file() or not (folder / 'uv.lock').is_file():
            raise ValueError('Incomplete AI-kit archive')
        (folder / 'release.json').write_bytes(raw)
        (folder / 'release.json.sig').write_bytes(signature)
        try:
            os.rename(folder, target)
        except OSError:
            if not receipt.is_file() or receipt.read_bytes() != raw:
                raise
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--channel', default='https://pxa.doit.am/agent/stable.json', help='signed channel manifest or an explicit local offline manifest')
    parser.add_argument('--workspace', type=Path, default=Path.cwd())
    parser.add_argument('--platform', choices=['all', 'codex', 'claude', 'dsh', 'generic'], default='all')
    parser.add_argument('--mode', choices=['app', 'board'], default='app',
                        help='app DevKit setup, or board tools in an existing firmware workspace')
    parser.add_argument('--engine', choices=['auto', 'native', 'docker', 'podman'], default='auto')
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--manifest', help='signed SDK distribution manifest, for offline setup')
    parser.add_argument('--no-smoke', action='store_true')
    args = parser.parse_args()
    try:
        print('PXA: verifying the signed Agent Kit release', file=sys.stderr, flush=True)
        kit = install(args.channel)
        uv = shutil.which('uv')
        if not uv:
            raise ValueError('Run this bootstrap with uv: https://docs.astral.sh/uv/getting-started/installation/')
        environment = kit / '.venv'
        os.environ['UV_PROJECT_ENVIRONMENT'] = str(environment)
        os.environ.pop('VIRTUAL_ENV', None)
        command = [uv, 'run', '--python', '3.12', '--locked', '--project', str(kit), str(kit / 'agent.py'),
                   '--workspace', str(args.workspace.resolve())]
        if args.mode == 'board':
            if args.sdk or args.manifest or args.engine != 'auto' or args.no_smoke:
                raise ValueError('Board setup uses the existing firmware toolchain; app SDK options do not apply')
            command += ['board', 'setup', '--platform', args.platform]
        else:
            command += ['setup', '--platform', args.platform, '--engine', args.engine]
        if args.sdk:
            command += ['--sdk', str(args.sdk.resolve())]
        if args.manifest:
            command += ['--manifest', args.manifest]
        if args.no_smoke:
            command += ['--no-smoke']
        sys.stdout.flush()
        os.execv(uv, command)
    except Exception as error:
        print('PXA setup failed: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

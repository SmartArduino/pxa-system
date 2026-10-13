"""Verify, then upload an existing artifact byte for byte; never sign here."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile
from urllib.parse import urlparse
from urllib.request import Request, HTTPRedirectHandler, build_opener
import uuid
from types import SimpleNamespace

from .common import ToolError, command, digest
from .run import pxadb_path, simulator_bin


class NoUploadRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        raise ToolError('Upload redirects are disabled; use the canonical HTTPS store URL')


def inspect(package: Path):
    # Reuse PXADB's bounded container metadata parser, then the native installer's
    # actual signature/file verifier. Embedded SPKI proves integrity, not trust.
    from importlib.util import module_from_spec, spec_from_file_location
    import sys
    spec = spec_from_file_location('pxa_publish_pxadb', pxadb_path())
    module = module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    fields = {}
    for tag, value in module.manifest_records(module.read_package_manifest(package)[12:]):
        if tag in (2, 11):
            if tag in fields:
                raise ToolError('Duplicate package identity/public key')
            fields[tag] = value
    if set(fields) != {2, 11} or len(fields[11]) > 4096:
        raise ToolError('Missing package identity/public key')
    return SimpleNamespace(app_id=module.package_manifest_identity(package), publisher_spki_der=fields[11])


def publish(package: Path, server: str, token_file: Path | None, *, check_only=False):
    package = package.resolve()
    metadata = inspect(package)
    with tempfile.TemporaryDirectory(prefix='pxa-publish-') as td:
        temporary = Path(td)
        public = temporary / 'publisher.der'
        public.write_bytes(metadata.publisher_spki_der)
        (temporary / 'state').mkdir()
        command([simulator_bin('pxsys_package_installer'), '--storage-root', temporary / 'state',
                 '--publisher-key', public, '--source', package, '--expected-id', metadata.app_id])
    before = digest(package)
    if check_only:
        print(json.dumps({'app_id': metadata.app_id, 'sha256': before,
                          'size': package.stat().st_size, 'verified': True}))
        return
    if urlparse(server).scheme != 'https' or not token_file:
        raise ToolError('Publishing requires an HTTPS server and --token-file (an app-scoped store upload token)')
    credential = json.loads(token_file.read_text())
    boundary = 'pxa-' + uuid.uuid4().hex
    # urllib accepts an iterable body: bounded chunks, without loading the package.
    prefix = (f'--{boundary}\r\nContent-Disposition: form-data; name="package"; filename="app.pxa"\r\n'
              'Content-Type: application/octet-stream\r\n\r\n').encode()
    suffix = f'\r\n--{boundary}--\r\n'.encode()
    def body():
        yield prefix
        with package.open('rb') as stream:
            while block := stream.read(1024 * 1024):
                yield block
        yield suffix
    endpoint = server.rstrip('/') + f'/api/v2/apps/{int(credential["app_id"])}/releases'
    request = Request(endpoint, data=body(), method='POST', headers={
        'Authorization': 'Bearer ' + credential['token'],
        'Content-Type': f'multipart/form-data; boundary={boundary}',
        'Content-Length': str(len(prefix) + package.stat().st_size + len(suffix)),
        'Accept': 'application/json'})
    with build_opener(NoUploadRedirect).open(request, timeout=180) as response:
        result = json.load(response)
    if digest(package) != before or result.get('sha256') != before:
        raise ToolError('Publication digest mismatch')
    print(json.dumps(result, ensure_ascii=False))

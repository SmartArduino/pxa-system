from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urljoin, urlparse

from . import SDK_VERSION, VERSION
from .common import KIT, ToolError, artifact, atomic_json, data_root, digest, extract, locked, run, signed_document


def sdk_data():
    # Reuse the standalone SDK's existing identity; installing AI tools must not
    # silently create a different publisher for an existing app.
    if os.environ.get('PXA_AGENT_HOME'):
        return data_root() / 'sdk-data/pxa'
    base = os.environ.get('XDG_DATA_HOME') or (os.environ.get('LOCALAPPDATA') if os.name == 'nt' else None)
    return (Path(base) if base else Path.home() / '.local/share') / 'pxa'


def locate(manifest, filename):
    if urlparse(str(manifest)).scheme:
        return urljoin(str(manifest), filename)
    return str(Path(manifest).resolve().parent / filename)


def native_supported():
    libc, version = platform.libc_ver()
    return (platform.system() == 'Linux' and platform.machine().lower() in ('x86_64', 'amd64')
            and libc == 'glibc' and tuple(map(int, version.split('.'))) >= (2, 39))


def install_native(version, manifest=None):
    source = manifest or f'https://pxa.doit.am/downloads/{version}/distribution.json'
    document, raw, signature = signed_document(source, schema='pxa-distribution-1')
    if document.get('version') != version or document.get('platform') != 'linux-x86_64':
        raise ToolError('SDK version/platform does not match the signed manifest')
    if not native_supported():
        raise ToolError('Native DevKit needs Linux x86_64 and glibc 2.39+; use a container engine')
    destination = data_root() / 'sdks' / version
    with locked(destination.with_suffix('.lock')):
        receipt = destination / 'distribution.json'
        if destination.exists():
            if not receipt.is_file() or receipt.read_bytes() != raw:
                raise ToolError('The installed SDK differs from this immutable release')
            return destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.sdk-', dir=destination.parent) as temporary:
            stage = Path(temporary) / 'sdk'
            stage.mkdir()
            components = document.get('components')
            if not isinstance(components, list) or {item.get('name') for item in components} != {'sdk', 'devtools', 'runtime', 'toolchain', 'simulator'}:
                raise ToolError('Incomplete SDK component manifest')
            for item in components:
                print(f"PXA: installing signed {item['name']} {version} ({item['size'] // 1048576} MiB)", file=sys.stderr, flush=True)
                archive = artifact(locate(source, item['file']), item)
                extract(archive, stage)
            if (stage / 'VERSION').read_text().strip() != version or not (stage / 'bin/pxa').is_file():
                raise ToolError('SDK contents do not match the requested release')
            (stage / 'distribution.json').write_bytes(raw)
            (stage / 'distribution.json.sig').write_bytes(signature)
            os.replace(stage, destination)
    return destination


def install_container(version, engine, manifest=None):
    binary = shutil.which(engine)
    if not binary:
        raise ToolError(f'{engine} is not installed. Install Docker Desktop or Podman, then repeat setup; no ESP-IDF is needed.')
    try:
        run([binary, 'info'], timeout=30)
    except ToolError as error:
        raise ToolError(f'{engine} is installed but its engine is not running. Start Docker Desktop or run podman machine start, then repeat setup.') from error
    source = manifest or f'https://github.com/SmartArduino/pxa-system/releases/download/v{version}/host-tools-{version}.json'
    document, raw, signature = signed_document(source, schema='pxa-host-distribution-1')
    if document.get('version') != version or document['devkit']['release'] != version:
        raise ToolError('Container release does not match its signed companion manifest')
    identity = document['container']['image_id'].removeprefix('sha256:')
    if not re.fullmatch('[a-f0-9]{64}', identity):
        raise ToolError('Invalid signed container image ID')
    destination = data_root() / 'host-tools' / version
    with locked(destination.with_suffix('.lock')):
        if not destination.exists():
            item = next(a for a in document['artifacts'] if a['file'] == f'pxa-host-tools-{version}.tar.gz')
            archive = artifact(locate(source, item['file']), item, maximum=10 * 1024**2)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='.host-', dir=destination.parent) as temporary:
                stage = Path(temporary)
                extract(archive, stage)
                folder = stage / f'pxa-host-tools-{version}'
                if not (folder / 'pxadb.py').is_file():
                    raise ToolError('Host tools have no native PXADB client')
                (folder / 'host-distribution.json').write_bytes(raw)
                (folder / 'host-distribution.json.sig').write_bytes(signature)
                os.replace(folder, destination)
        elif (destination / 'host-distribution.json').read_bytes() != raw:
            raise ToolError('Installed host tools differ from the immutable signed release')
        inspected = subprocess.run([binary, 'image', 'inspect', '--format', '{{.Id}}', identity], text=True, capture_output=True)
        if inspected.returncode:
            item = next(a for a in document['artifacts'] if a['file'] == f'pxa-devkit-container-{version}-linux-amd64.tar.gz')
            image = artifact(locate(source, item['file']), item)
            run([binary, 'load', '--input', image], timeout=600)
        actual = run([binary, 'image', 'inspect', '--format', '{{.Id}}', identity]).strip().removeprefix('sha256:')
        if actual != identity:
            raise ToolError('Loaded image ID does not match the signed release')
    return {'engine': str(Path(binary).resolve()), 'image': 'sha256:' + identity, 'host_tools': str(destination)}


def configure(workspace, *, engine='auto', sdk=None, version=None, manifest=None):
    workspace = Path(workspace).expanduser().resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    path = workspace / '.pxa/agent.json'
    if (workspace / '.pxa').is_symlink() or path.is_symlink():
        raise ToolError('Refusing a symlink workspace configuration')
    previous = json.loads(path.read_text()) if path.exists() else {}
    version = version or previous.get('sdk_version') or SDK_VERSION
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ToolError('Choose an explicit SDK release, for example 0.6.0')
    if previous and previous.get('sdk_version') != version:
        raise ToolError('This workspace pins another SDK; use a new workspace or an explicit SDK migration')
    if sdk:
        sdk = Path(sdk).expanduser().resolve()
        if not (sdk / 'bin/pxa').is_file() or (sdk / 'VERSION').read_text().strip() != version:
            raise ToolError('The selected DevKit is missing or has a different version')
        selected = {'engine': 'native', 'sdk': str(sdk), 'verified_distribution': False}
    else:
        if engine == 'auto':
            engine = previous.get('engine') or ('native' if native_supported() else ('docker' if shutil.which('docker') else 'podman'))
        if engine == 'native':
            selected = {'engine': 'native', 'sdk': str(install_native(version, manifest)), 'verified_distribution': True}
        elif engine in ('docker', 'podman') or Path(engine).stem in ('docker', 'podman'):
            selected = install_container(version, engine, manifest)
            selected['verified_distribution'] = True
        else:
            raise ToolError('Engine must be auto, native, docker or podman')
    print('PXA: preparing project-local AI tools', file=sys.stderr, flush=True)
    config = {'schema': 'pxa-agent-workspace-1', 'kit_version': VERSION, 'sdk_version': version, **selected}
    atomic_json(path, config)
    return Environment(workspace, config)


class Environment:
    def __init__(self, workspace, config=None):
        self.workspace = Path(workspace).expanduser().resolve()
        path = self.workspace / '.pxa/agent.json'
        if config is None and not path.is_file():
            raise ToolError('Run PXA Agent Kit setup in this workspace first')
        self.config = config or json.loads(path.read_text())
        self.native = self.config['engine'] == 'native'

    def project(self, value):
        path = (self.workspace / value).resolve()
        if not path.is_relative_to(self.workspace):
            raise ToolError('Project paths must stay inside the configured workspace')
        return path

    def ensure_identity(self, project, key=None):
        """Refuse an implicit publisher change for an existing development app."""
        from .scope import app
        app(self, project)
        from cryptography.hazmat.primitives import serialization
        project = self.project(project)
        expected = []
        public_file = project / '.pxa/publisher.der'
        if public_file.is_file():
            expected.append(public_file.read_bytes())
        # A built package may be the only remaining record after cache cleanup.
        from .store import package_identity
        for package in (project / 'dist').glob('*.pxa'):
            expected.append(package_identity(self, package)['spki'])
        if not expected:
            return
        key = key or os.environ.get('PXA_SIGNING_KEY')
        key_path = Path(key).expanduser().resolve() if key else sdk_data() / 'identities/development.pem'
        if not key_path.is_file():
            raise ToolError('This app already has a publisher. Restore its original private key or select --signing-key; no replacement key was generated.')
        try:
            private = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
        except (ValueError, TypeError) as error:
            raise ToolError('The selected signing key could not be opened; preserve the original identity') from error
        public = private.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        if any(public != previous for previous in expected):
            raise ToolError('The signing key differs from this existing app. Select its original key; implicit identity changes are blocked.')
        if not self.native and not (key_path.is_relative_to(self.workspace) or key_path.is_relative_to(sdk_data())):
            raise ToolError('For container use, select a signing key in the workspace or the existing PXA identity directory')

    def mapped(self, value):
        if self.native:
            return str(value)
        path = Path(value)
        if path.is_absolute() and path.is_relative_to(self.workspace):
            return '/workspace/' + path.relative_to(self.workspace).as_posix()
        if path.is_absolute() and path.is_relative_to(sdk_data()):
            return '/pxa-data/pxa/' + path.relative_to(sdk_data()).as_posix()
        return str(value)

    def variables(self):
        # A rootless container engine uses host XDG paths for its image store.
        # Its image already defines guest XDG paths; do not change engine storage.
        if not self.native:
            return dict(os.environ)
        return {**os.environ, 'XDG_DATA_HOME': str(sdk_data().parent),
                'XDG_CACHE_HOME': str(data_root() / 'cache')}

    def invocation(self, arguments, *, container_name=None, entrypoint=None):
        arguments = [self.mapped(arg) for arg in arguments]
        if self.native:
            return [str(Path(self.config['sdk']) / 'bin/pxa'), *arguments]
        sdk_data().mkdir(parents=True, exist_ok=True)
        (data_root() / 'cache').mkdir(parents=True, exist_ok=True)
        for path in (self.workspace, data_root(), KIT):
            if ',' in str(path):
                raise ToolError('Container mount paths cannot contain commas')
        cmd = [self.config['engine'], 'run', '--rm', '--platform', 'linux/amd64']
        if platform.system() == 'Linux':
            cmd += ['--user', f'{os.getuid()}:{os.getgid()}']
            if Path(self.config['engine']).name == 'podman':
                cmd += ['--userns=keep-id']
        if container_name:
            cmd += ['--name', container_name]
        cmd += ['--mount', f'type=bind,source={self.workspace},target=/workspace',
                '--mount', f'type=bind,source={sdk_data()},target=/pxa-data/pxa',
                '--mount', f'type=bind,source={data_root() / "cache"},target=/pxa-cache',
                '--mount', f'type=bind,source={KIT},target=/pxa-agent-kit,readonly',
                '--workdir', '/workspace']
        if entrypoint:
            cmd += ['--entrypoint', entrypoint]
        return [*cmd, self.config['image'], *arguments]

    def call(self, arguments, *, timeout=300, extra_env=None):
        if extra_env and not self.native:
            invocation = self.invocation(arguments)
            # Insert engine environment flags before the image/guest command.
            image = invocation.index(self.config['image'])
            flags = [part for name, value in extra_env.items() for part in ('-e', name + '=' + value)]
            invocation[image:image] = flags
        else:
            invocation = self.invocation(arguments)
        result = run(invocation, cwd=self.workspace, env={**self.variables(), **(extra_env or {})}, timeout=timeout)
        if arguments and str(arguments[0]) in ('build', 'run'):
            from .scope import enforce
            enforce(self.workspace)
        return result

    def device(self, arguments, *, timeout=60):
        script = (Path(self.config['sdk']) / 'tools/pxadb/pxadb.py' if self.native else Path(self.config['host_tools']) / 'pxadb.py')
        return run([sys.executable, script, *arguments], cwd=self.workspace, env=self.variables(), timeout=timeout)

    def doctor(self):
        if self.config.get('mode') == 'board':
            return {'mode': 'board', 'kit_version': VERSION, 'workspace': str(self.workspace),
                    'idf_path': os.environ.get('IDF_PATH'), 'firmware_workspace': True,
                    'note': 'Board adaptation needs the workspace ESP-IDF toolchain; SDK/API are read-only'}
        from serial.tools.list_ports import comports
        return {'ready': True, 'kit_version': VERSION, 'workspace': str(self.workspace),
                'sdk_version': self.config['sdk_version'], 'engine': self.config['engine'],
                'verified_distribution': self.config.get('verified_distribution', False),
                'pxa': self.call(['--version']).strip(), 'python': platform.python_version(),
                'devices': [{'port': p.device, 'description': p.description, 'serial': p.serial_number,
                             'vid': p.vid, 'pid': p.pid} for p in comports() if p.vid or p.description != 'n/a']}

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import sys

from .common import ToolError, atomic_json, command, digest, identity


SDK_ROOT = Path(__file__).resolve().parents[2]
ID_PATTERN = re.compile(r'[a-z][a-z0-9._-]{0,63}')
TARGETS = {name: (value['target'], value['cpu'], value['flags'])
           for name, value in json.loads((SDK_ROOT / 'config/aot-targets.json').read_text()).items()}


def aot_flags(metadata: dict, target: str) -> list[str]:
    """Apply optional per-app safety instrumentation to the target recipe."""
    flags = list(TARGETS[target][2])
    build = metadata.get('build', {})
    if 'native_stack_checks' in build:
        checks = build['native_stack_checks']
        if type(checks) is not bool:
            raise ToolError('build.native_stack_checks must be a boolean')
        flags = [flag for flag in flags if not flag.startswith('--stack-bounds-checks=')]
        flags.append('--stack-bounds-checks=' + str(int(checks)))
    return flags



def sdk_identity(root: Path) -> dict:
    files = sorted([*root.glob('sdk/guest-cpp/include/**/*.hpp'),
                    *root.glob('sdk/guest-cpp/src/*.cpp'), *root.glob('sdk/cmake/*.cmake'),
                    *root.glob('spec/draft/pxa-*.json'), *root.glob('tools/pxa_tools/*.py'),
                    *root.glob('tools/package/*.py'), *root.glob('config/*.json'), *root.glob('sdk/guest-c/include/*.h')])
    files = sorted(set(files) | {p for p in (root / 'tools/i18n').rglob('*')
                                if p.is_file() and '__pycache__' not in p.parts}
                   | {p for p in (root / 'extensions').rglob('*')
                      if p.is_file() and '__pycache__' not in p.parts
                      and p.suffix in ('.h', '.hpp', '.cmake', '.json')}
                   | set(root.glob('config/*.pem')) | set(root.glob('bin/pxa*')))
    return {'release': (root / 'VERSION').read_text().strip(),
            'digest': identity([(str(p.relative_to(root)), digest(p)) for p in files])}


@dataclass(frozen=True)
class Project:
    path: Path
    metadata: dict
    components: tuple[dict, ...]

    @classmethod
    def open(cls, path: Path):
        path = path.resolve()
        try:
            data = json.loads((path / 'package.json').read_text())
        except (OSError, ValueError) as error:
            raise ToolError(f'Cannot read {path / "package.json"}: {error}') from error
        if not isinstance(data, dict) or not isinstance(data.get('id'), str) or not ID_PATTERN.fullmatch(data['id']):
            raise ToolError('package.json: invalid application id')
        build = data.get('build', {})
        if not isinstance(build, dict) or build.get('system', 'cmake') != 'cmake':
            raise ToolError('The C++ development tool requires a CMake project')
        aot_flags(data, 'simulator')
        component_metadata = data.get('components', [{'id': 'main', 'kind': 'ui'}])
        if not isinstance(component_metadata, list):
            raise ToolError('components must be an array')
        components = tuple(component_metadata)
        if not components or any(not isinstance(c, dict) or not isinstance(c.get('id'), str) or not ID_PATTERN.fullmatch(c['id']) for c in components):
            raise ToolError('Invalid component ids')
        if len({c['id'] for c in components}) != len(components):
            raise ToolError('Duplicate component ids')
        return cls(path, data, components)

    @property
    def app_id(self):
        return self.metadata['id']


def resolve_tools(root: Path, *, offline=False) -> tuple[Path, Path]:
    bundled = root / 'toolchains/wasi-sdk'
    if bundled.is_dir():
        wasi = bundled
    else:
        env = os.environ.copy()
        env['PYTHON'] = sys.executable
        if offline:
            env['PXA_WASI_SDK_AUTO_DOWNLOAD'] = '0'
        wasi = Path(command(['bash', root / 'tools/package/resolve_wasi_sdk.sh'], env=env, capture=True))
    wamrc = Path(os.environ.get('WAMRC', root / 'bin/wamrc'))
    if not wamrc.is_file():
        if offline:
            raise ToolError('Offline: wamrc is not installed')
        wamrc = Path(command(['bash', root / 'tools/package/build_wamrc.sh'], capture=True))
    if not (wasi / 'bin/clang++').is_file() or not wamrc.is_file():
        raise ToolError('Incomplete PXA toolchain')
    return wasi.resolve(), wamrc.resolve()


def lock_project(project: Project, root: Path, wasi: Path, wamrc: Path, *, update=False):
    wamr = json.loads((root / 'config/wamr.json').read_text())
    current = {'schema': 'pxa-lock-1', 'sdk': sdk_identity(root),
               'toolchain': {'wasi_version': (wasi / 'VERSION').read_text().strip(),
                             'clang_sha256': digest(wasi / 'bin/clang'),
                             'wamrc_sha256': digest(wamrc), 'recipe': wamr},
               'core': project.metadata.get('min_core', project.metadata.get('min_sdk', [2, 0]))}
    path = project.path / 'pxa.lock'
    if path.exists() and not update:
        previous = json.loads(path.read_text())
        if current != previous:
            raise ToolError('Locked SDK/toolchain differs. Run pxa sdk update explicitly; existing projects never follow latest.')
    else:
        atomic_json(path, current)
    return current

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time

from .common import ToolError, atomic_json, command, digest, identity, locked
from .project import Project, SDK_ROOT, TARGETS, lock_project, resolve_tools


def tree_digest(root: Path):
    return [(str(p.relative_to(root)), digest(p)) for p in sorted(root.rglob('*')) if p.is_file()]


def resource_inputs(project: Path):
    """Include declared inputs even when they live outside assets/resources."""
    config = project / 'resources.json'
    if not config.exists():
        return []
    from importlib.util import module_from_spec, spec_from_file_location
    spec = spec_from_file_location('pxa_compile_resources', SDK_ROOT / 'tools/package/compile_resources.py')
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    document = json.loads(config.read_text())
    if not isinstance(document, dict) or not isinstance(document.get('assets'), list):
        raise ToolError('resources.json requires an assets list')
    inputs = {config}
    for entry in document['assets']:
        if not isinstance(entry, dict):
            raise ToolError('Invalid resource declaration')
        for field in ('source', 'palette'):
            if field in entry:
                inputs.add(module.checked_path(project, entry[field]))
    return [(str(p.relative_to(project)), digest(p)) for p in sorted(inputs)]


def tool(root: Path, name: str) -> Path:
    private = root / 'runtime/bin' / name
    return private if private.is_file() else Path(name)


def signing_key(root: Path, supplied: str | None) -> Path:
    if supplied or os.environ.get('PXA_SIGNING_KEY'):
        key = Path(supplied or os.environ['PXA_SIGNING_KEY']).expanduser().resolve()
        if not key.is_file():
            raise ToolError(f'Signing key is missing: {key}')
        return key
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    key = data / 'pxa/identities/development.pem'
    with locked(key.with_suffix('.lock')):
        if not key.exists():
            key.parent.mkdir(parents=True, exist_ok=True)
            command([tool(root, 'openssl'), 'genpkey', '-algorithm', 'EC',
                     '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', key])
            key.chmod(0o600)
    return key


def build(project: Project, *, root=SDK_ROOT, targets=('simulator',), output=None,
          key=None, offline=False, update_lock=False, definitions=(), aot_only=False,
          jobs=None, build_type='Release') -> Path:
    started = time.monotonic()
    targets = tuple(dict.fromkeys(targets))
    if not targets or any(t not in TARGETS for t in targets):
        raise ToolError('Targets: ' + ', '.join(TARGETS))
    for value in definitions:
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(=[^;\n\r]+)?', value):
            raise ToolError(f'Invalid definition: {value}')
    wasi, wamrc = resolve_tools(root, offline=offline)
    lock = lock_project(project, root, wasi, wamrc, update=update_lock)
    jobs = jobs or min(os.cpu_count() or 1, 8)
    if not 1 <= jobs <= 64:
        raise ToolError('jobs must be in 1..64')
    state = project.path / '.pxa'
    source = (project.path / project.metadata.get('build', {}).get('source_dir', '.')).resolve()
    if not source.is_relative_to(project.path) or not (source / 'CMakeLists.txt').is_file():
        raise ToolError('Invalid CMake source directory')
    maximum = project.metadata.get('build', {}).get('linear_memory', {}).get('maximum_bytes', 0)
    config = {'sdk': lock['sdk'], 'toolchain': lock['toolchain'], 'type': build_type,
              'definitions': list(definitions), 'maximum': maximum,
              # CMake caches absolute compiler/source locations. Keep that local
              # state separate from the portable lock and content-addressed AOT.
              'paths': {'sdk': str(root), 'wasi': str(wasi), 'source': str(source)}}
    build_dir = state / 'build' / identity(config)[:20]
    artifacts = build_dir / 'artifacts'
    cmake = tool(root, 'cmake')
    with locked(state / 'build.lock'):
        command([cmake, '-S', source, '-B', build_dir, '-G', 'Ninja',
                 f'-DCMAKE_MAKE_PROGRAM={shutil.which(str(tool(root, "ninja"))) or tool(root, "ninja")}',
                 f'-DCMAKE_BUILD_TYPE={build_type}',
                 f'-DCMAKE_TOOLCHAIN_FILE={root}/sdk/cmake/pxa-wasi-toolchain.cmake',
                 f'-DWASI_SDK_DIR={wasi}', f'-DPxa_DIR={root}/sdk/cmake',
                 f'-DPXA_CPP_SDK_DIR={root}/sdk/guest-cpp',
                 f'-DPXA_GUEST_SDK_DIR={root}/sdk/guest-c',
                 f'-DPXA_CMAKE_MODULE_DIR={root}/sdk/cmake',
                 f'-DPXA_ARTIFACT_DIR={artifacts}',
                 f'-DPython3_EXECUTABLE={sys.executable}',
                 f'-DPXA_LINEAR_MEMORY_MAXIMUM={maximum}',
                 '-DPXA_APP_DEFINITIONS=' + ';'.join(definitions)])
        command([cmake, '--build', build_dir, '--parallel', jobs, '--target',
                 *[c.get('cmake_target', c['id']) for c in project.components]])
        with tempfile.TemporaryDirectory(prefix='package-', dir=state) as temporary:
            package = Path(temporary)
            (package / 'artifacts').mkdir()
            core = lock['core']
            tools = root / 'tools/package'
            for c in project.components:
                wasm = artifacts / (c['id'] + '.wasm')
                command([sys.executable, tools / 'verify_wasm_core_imports.py', wasm,
                         '--core-major', core[0], '--core-minor', core[1],
                         '--manifest-source', project.path / 'package.json', '--component-id', c['id']])
                if maximum:
                    command([sys.executable, tools / 'verify_wasm_memory.py', wasm,
                             '--maximum-bytes', maximum])
                shutil.copy2(wasm, package / 'artifacts' / wasm.name)

            def aot(pair):
                c, target = pair
                if target == 'wasm' or c.get('artifact') == 'wasm':
                    return
                manifest_target, cpu, flags = TARGETS[target]
                wasm = package / 'artifacts' / (c['id'] + '.wasm')
                recipe = identity({'wasm': digest(wasm), 'compiler': lock['toolchain']['wamrc_sha256'],
                                   'cpu': cpu, 'flags': flags})
                cached = state / 'cache/aot' / (recipe + '.aot')
                destination = package / 'artifacts' / f'{c["id"]}.{manifest_target}.aot'
                with locked(cached.with_suffix('.lock')):
                    if not cached.exists():
                        temporary_aot = cached.with_suffix('.tmp')
                        command([wamrc, '--target=' + cpu, *flags, '-o', temporary_aot, wasm])
                        os.replace(temporary_aot, cached)
                shutil.copy2(cached, destination)

            with ThreadPoolExecutor(max_workers=min(jobs, 4)) as executor:
                list(executor.map(aot, [(c, t) for c in project.components for t in targets]))
            for c in project.components:
                mode = c.get('artifact', 'both')
                if 'wasm' not in targets and mode != 'wasm' and (aot_only or mode == 'aot'):
                    (package / 'artifacts' / (c['id'] + '.wasm')).unlink()

            selected_assets = project.path / ('assets-' + targets[0])
            if not selected_assets.is_dir():
                selected_assets = project.path / 'assets'
            if len(targets) > 1 and any((project.path / ('assets-' + t)).is_dir() for t in targets):
                raise ToolError('Target-specific assets require separate packages; universal packages share one resource set')
            resources = identity({'assets': tree_digest(selected_assets) if selected_assets.is_dir() else [],
                                  'sources': tree_digest(project.path / 'resources') if (project.path / 'resources').is_dir() else [],
                                  'declared': resource_inputs(project.path),
                                  'sdk': lock['sdk']})
            resource_cache = state / 'cache/resources' / resources
            if not resource_cache.is_dir():
                with tempfile.TemporaryDirectory(dir=state) as td:
                    stage = Path(td)
                    if selected_assets.is_dir():
                        shutil.copytree(selected_assets, stage / 'assets')
                    command([sys.executable, tools / 'compile_resources.py', project.path, stage])
                    resource_cache.parent.mkdir(parents=True, exist_ok=True)
                    with tempfile.TemporaryDirectory(prefix='resource-', dir=resource_cache.parent) as rd:
                        complete = Path(rd) / 'complete'
                        shutil.copytree(stage, complete)
                        os.replace(complete, resource_cache)
            if (resource_cache / 'assets').exists():
                shutil.copytree(resource_cache / 'assets', package / 'assets')

            private = signing_key(root, key)
            public = command([tool(root, 'openssl'), 'pkey', '-in', private, '-pubout'], capture=True)
            content = identity({'metadata': project.metadata, 'files': tree_digest(package),
                                'publisher': public, 'sdk': lock['sdk'], 'targets': sorted(targets),
                                'aot_only': aot_only,
                                'i18n': tree_digest(project.path / 'i18n') if (project.path / 'i18n').is_dir() else []})
            release = state / 'cache/packages' / content
            cached_container = release / (project.app_id + '.pxa')
            if not release.is_dir():
                release.parent.mkdir(parents=True, exist_ok=True)
                mode = ['--artifact-mode', 'wasm'] if targets == ('wasm',) else (['--artifact-mode', 'aot'] if aot_only else [])
                command([sys.executable, tools / 'build_package_manifest.py', project.path / 'package.json',
                         package, private, TARGETS[targets[0]][0], lock['toolchain']['recipe']['engine_abi'], *mode])
                with tempfile.TemporaryDirectory(prefix='generation-', dir=release.parent) as td:
                    staged = Path(td) / 'complete'
                    staged.mkdir()
                    command([sys.executable, tools / 'build_pxa_container.py', package, private,
                             staged / cached_container.name])
                    shutil.copytree(package, staged / 'package')
                    os.replace(staged, release)
            output = (Path(output).absolute().parent.resolve() / Path(output).name) if output else project.path / 'dist' / project.app_id
            if output == project.path or project.path.is_relative_to(output):
                raise ToolError('Output cannot replace the source directory')
            output.parent.mkdir(parents=True, exist_ok=True)
            # Publish a complete generation, then switch the directory atomically.
            generation = output.parent / ('.' + output.name + '-' + content[:20])
            if not generation.exists():
                shutil.copytree(release / 'package', generation)
            if output.exists() and not output.is_symlink():
                raise ToolError(f'Output already exists as a directory: {output}; choose a new output')
            link = output.with_name('.' + output.name + '.next')
            link.unlink(missing_ok=True)
            link.symlink_to(generation.name, target_is_directory=True)
            os.replace(link, output)
            container = Path(str(output) + '.pxa')
            temporary_output = container.with_name('.' + container.name + '.next')
            shutil.copy2(cached_container, temporary_output)
            os.replace(temporary_output, container)
            env = os.environ.copy()
            env.update(PXA_RESOLVED_CLANG=str(wasi / 'bin/clang'), PXA_RESOLVED_WAMRC=str(wamrc),
                       PXA_RESOLVED_WASI_SDK=str(wasi))
            command([sys.executable, tools / 'build_pxa_provenance.py', release / 'package',
                     container, project.path / 'package.json', ','.join(targets),
                     lock['toolchain']['recipe']['engine_abi'], str(container) + '.provenance.json'], env=env)
            atomic_json(Path(str(container) + '.build.json'), {'schema': 'pxa-build-1', 'content_id': content,
                        'lock': lock, 'targets': list(targets), 'configuration': config,
                        'seconds': round(time.monotonic() - started, 3), 'sha256': digest(container)})
            print(f'Built {container} ({time.monotonic() - started:.2f}s)')
            return container

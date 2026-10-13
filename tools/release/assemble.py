#!/usr/bin/env python3
"""Assemble a movable DevKit inside the pinned release build container.

Uses already-built binaries; LLVM is never rebuilt by an end user or server.
Private keys are supplied outside the source tree and are never copied.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def sha(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024**2):
            result.update(block)
    return result.hexdigest()


def copy(source, destination, *, ignore=None):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination, symlinks=True, ignore=ignore, dirs_exist_ok=True)
    else:
        shutil.copy2(source, destination)


def private_dependencies(root):
    """Keep the system loader/glibc; bundle transitive userspace dependencies."""
    library_dir = root / 'runtime/lib'
    library_dir.mkdir(parents=True, exist_ok=True)
    dependency_env = {**os.environ, 'LD_LIBRARY_PATH':
                      str(library_dir) + ':' + str(root / 'toolchains/wasi-sdk/lib')}
    excluded = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0',
                'librt.so.1', 'libresolv.so.2', 'libutil.so.1', 'ld-linux-x86-64.so.2'}
    checked = set()
    pending = [p for p in root.rglob('*') if p.is_file() and not p.is_symlink()]
    while pending:
        path = pending.pop()
        if path in checked:
            continue
        checked.add(path)
        with path.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        dependencies = subprocess.run(['ldd', str(path)], text=True, capture_output=True,
                                      env=dependency_env)
        if dependencies.returncode:
            continue  # Static object files and archives need no runtime.
        if 'not found' in dependencies.stdout:
            raise RuntimeError(f'Unresolved dependencies: {path}: {dependencies.stdout}')
        private = False
        for name, source in re.findall(r'^\s*(\S+) => (/\S+)', dependencies.stdout, re.M):
            if name in excluded:
                continue
            private = True
            if Path(source).resolve().is_relative_to(root):
                continue  # Preserve the toolchain's own versioned libraries.
            destination = library_dir / name
            if not destination.exists():
                copy(Path(source).resolve(), destination)
                pending.append(destination)
        if not private:
            continue  # A glibc-only leaf needs no extra search path.
        if path.is_relative_to(root / 'toolchains') and '.so' in path.name:
            continue  # The upstream toolchain already resolves its own libraries.
        relative = os.path.relpath(library_dir, path.parent)
        old = run('patchelf', '--print-rpath', path)
        # Relative internal toolchain RPATHs remain valid after relocation.
        entries = [entry for entry in old.split(':') if entry.startswith('$ORIGIN')]
        entries.append('$ORIGIN/' + relative)
        # DT_RPATH resolves the executable's transitive private dependencies.
        # Avoid exporting LD_LIBRARY_PATH into CMake's host shell subprocesses.
        run('patchelf', '--force-rpath', '--set-rpath', ':'.join(dict.fromkeys(entries)), path)


def archive(root, paths, destination, epoch):
    with destination.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=0, filename='') as zipped:
        with tarfile.open(fileobj=zipped, mode='w|', format=tarfile.PAX_FORMAT) as tar:
            def normalize(info):
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                info.mtime = epoch
                return info
            for path in sorted(paths):
                tar.add(root / path, arcname=path, recursive=True, filter=normalize)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--wamrc', type=Path, required=True)
    parser.add_argument('--wasi', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--signing-key', type=Path, required=True)
    parser.add_argument('--allow-dirty', action='store_true', help='local validation only; never publish this result')
    args = parser.parse_args()
    source = args.workspace / 'deps/pxa-system'
    version = (source / 'VERSION').read_text().strip()
    provenance = {}
    for name, checkout in (('system', source), ('workspace', args.workspace)):
        dirty = bool(run('git', '-C', checkout, 'status', '--porcelain'))
        if dirty and not args.allow_dirty:
            raise RuntimeError(f'{name} source is dirty; official releases require committed sources')
        provenance[name] = {'commit': run('git', '-C', checkout, 'rev-parse', 'HEAD'), 'dirty': dirty}
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    root = output / f'pxa-devkit-{version}-linux-x86_64'
    if root.exists():
        shutil.rmtree(root)
    root.mkdir()
    ignore = shutil.ignore_patterns('__pycache__', '*.pyc', '.git', '.pxa', 'dist', 'build')
    for name in ('VERSION', 'LICENSE', 'sdk', 'config', 'docs'):
        copy(source / name, root / name, ignore=ignore)
    copy(source / 'spec/draft', root / 'spec/draft', ignore=ignore)
    for name in ('pxa_tools', 'package', 'i18n'):
        copy(source / 'tools' / name, root / 'tools' / name, ignore=ignore)
    copy(args.workspace / 'tools/pxadb', root / 'tools/pxadb', ignore=ignore)
    copy(args.workspace / 'tools/simulator_pxadb.py', root / 'tools/simulator_pxadb.py')
    copy(source / 'bin/pxa', root / 'bin/pxa')
    copy(source / 'bin/pxadb', root / 'bin/pxadb')
    copy(args.wamrc, root / 'bin/wamrc')
    copy(args.wasi, root / 'toolchains/wasi-sdk', ignore=ignore)
    for name in ('pxsys_desktop_simulator', 'pxsys_product_simulator', 'pxsys_package_installer'):
        copy(args.simulator / name, root / 'simulator/bin' / name)
    copy(args.workspace / 'simulator/profiles', root / 'share/pxa/profiles', ignore=ignore)
    copy(args.workspace / 'factory/base/system/fonts/noto_sans_cjk_common.ttf',
         root / 'share/pxa/fonts/noto_sans_cjk_common.ttf')
    for name in ('cmake', 'ninja', 'openssl'):
        copy(Path(shutil.which(name)), root / 'runtime/bin' / name)
    copy(Path('/usr/share/cmake-3.28'), root / 'runtime/share/cmake-3.28', ignore=ignore)
    copy(Path('/etc/ssl/certs/ca-certificates.crt'), root / 'runtime/share/ssl/ca-certificates.crt')
    copy(Path('/usr/bin/python3.12'), root / 'runtime/python/bin/python3')
    copy(Path('/usr/lib/python3.12'), root / 'runtime/python/lib/python3.12',
         ignore=shutil.ignore_patterns('__pycache__', '*.pyc', 'test', 'tests', 'idlelib', 'tkinter', 'ensurepip', 'sitecustomize.py'))
    for name in ('serial', 'PIL'):
        copy(Path('/usr/lib/python3/dist-packages') / name,
             root / 'runtime/python/lib/python3.12/site-packages' / name, ignore=ignore)
    copy(Path('/usr/lib/x86_64-linux-gnu/liblz4.so.1').resolve(), root / 'runtime/lib/liblz4.so.1')
    # Native PXA shared libraries are not installed on the builder's system.
    for path in args.simulator.rglob('*.so*'):
        if path.is_file() and not path.is_symlink():
            soname = run('patchelf', '--print-soname', path)
            copy(path, root / 'runtime/lib' / (soname or path.name))
    private_dependencies(root)
    licenses = root / 'licenses'
    licenses.mkdir()
    for path in Path('/usr/share/doc').glob('*/copyright'):
        copy(path, licenses / (path.parent.name + '.txt'))
    copy(args.workspace / 'factory/base/system/fonts/OFL.txt', licenses / 'font-OFL.txt')
    copy(args.workspace / 'factory/base/system/fonts/NOTICE', licenses / 'font-NOTICE.txt')
    copy(source / 'wamr/LICENSE', licenses / 'WAMR.txt')
    copy(Path('/build/cache/llvm-344b928e67bfc1d07cb150609c55548e86a9bf19/llvm/LICENSE.TXT'), licenses / 'LLVM.txt')
    copy(args.workspace / 'firmware/managed_components/lvgl__lvgl/LICENCE.txt', licenses / 'LVGL.txt')
    (root / 'build-environment-packages.txt').write_text(run('dpkg-query', '-W', '-f=${Package}\t${Version}\n') + '\n')
    (root / 'README.md').write_text(f'''# PXA DevKit {version}

Linux x86_64, glibc >= 2.39. No system compiler, CMake or Python is required.
Add this directory's bin to PATH; run `pxa init hello`, then `pxa run hello`.
For devices: `pxa run hello --device /dev/ttyACM1`.
Offline: `pxa build hello --offline --target esp32s3,esp32s31,simulator`.
SDK/tool versions are locked in pxa.lock. Updates require `pxa sdk update`.
Project build caches and development identities are outside this directory.
Guides: https://pxa.doit.am/docs/setup/
''')
    epoch = int(os.environ.get('SOURCE_DATE_EPOCH', '1791849600'))
    groups = {'sdk': ['VERSION', 'LICENSE', 'sdk', 'config', 'spec', 'docs'],
              'devtools': ['bin/pxa', 'bin/pxadb', 'tools', 'README.md'],
              'runtime': ['runtime', 'build-environment-packages.txt', 'licenses'],
              'toolchain': ['bin/wamrc', 'toolchains'],
              'simulator': ['simulator', 'share']}
    components = []
    for name, paths in groups.items():
        path = output / f'pxa-{name}-{version}-linux-x86_64.tar.gz'
        archive(root, paths, path, epoch)
        components.append({'name': name, 'file': path.name, 'size': path.stat().st_size, 'sha256': sha(path)})
    aggregate = output / (root.name + '.tar.gz')
    archive(root.parent, [root.name], aggregate, epoch)
    sys.path.insert(0, str(root / 'tools'))
    from pxa_tools.project import sdk_identity
    manifest = {'schema': 'pxa-distribution-1', 'version': version, 'platform': 'linux-x86_64',
                'source': provenance,
                'minimum_glibc': '2.39', 'sdk': sdk_identity(root), 'components': components,
                'devkit': {'file': aggregate.name, 'size': aggregate.stat().st_size, 'sha256': sha(aggregate)},
                'toolchain': json.loads((source / 'config/wamr.json').read_text()),
                'wasi_sdk_version': (args.wasi / 'VERSION').read_text().strip()}
    path = output / 'distribution.json'
    path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    run('openssl', 'dgst', '-sha256', '-sign', args.signing_key, '-out', str(path) + '.sig', path)
    (output / 'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in sorted(output.iterdir()) if p.is_file() and p.name != 'SHA256SUMS'))
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()

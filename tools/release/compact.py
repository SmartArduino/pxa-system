"""Reduce copied release payloads; never operate on the input SDK or build tree."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import subprocess


def payload_size(root):
    apparent = unique = 0
    seen = set()
    for path in root.rglob('*'):
        if path.is_symlink() or not path.is_file():
            continue
        stat = path.stat()
        apparent += stat.st_size
        identity = stat.st_dev, stat.st_ino
        if identity not in seen:
            unique += stat.st_size
            seen.add(identity)
    return {'file_bytes': apparent, 'unique_file_bytes': unique}


def is_notice(path):
    return re.match(r'(license|licence|copying|copyright|notice)(?:[._-]|$)',
                    path.name, re.IGNORECASE) is not None


def remove_payload(path):
    """Keep notices even when they live inside an otherwise omitted directory."""
    if is_notice(path):
        return
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        for child in path.iterdir():
            remove_payload(child)
        if not any(path.iterdir()):
            path.rmdir()


def needed_libraries(path, *, windows, objdump, readelf):
    command = [objdump, '-p', str(path)] if windows else [readelf, '-d', str(path)]
    result = subprocess.run(command, text=True, capture_output=True, check=True)
    return re.findall(r'DLL Name:\s*(\S+)', result.stdout) if windows else re.findall(
        r'\(NEEDED\).*?\[([^\]]+)\]', result.stdout)


def compact_libraries(root, *, windows, objdump, readelf):
    directory = root / ('bin' if windows else 'lib')
    libraries = {p.name.lower() if windows else p.name: p for p in directory.iterdir()
                 if p.is_file() and (p.suffix.lower() == '.dll' if windows else '.so' in p.name)}
    used, checked = set(), set()
    pending = []
    for path in (root / 'bin').iterdir():
        if path.is_file():
            with path.open('rb') as stream:
                magic = stream.read(4)
            if (windows and path.suffix.lower() == '.exe') or (not windows and magic == b'\x7fELF'):
                pending.append(path)
    while pending:
        path = pending.pop()
        resolved = path.resolve(strict=True)
        if not resolved.is_relative_to(root):
            raise ValueError('Toolchain links must stay inside the copied toolchain: ' + str(path))
        if resolved in checked:
            continue
        checked.add(resolved)
        for name in needed_libraries(resolved, windows=windows, objdump=objdump, readelf=readelf):
            candidate = libraries.get(name.lower() if windows else name)
            if candidate is not None:
                used.add(candidate.resolve(strict=True))
                pending.append(candidate)
    omitted = [path for path in libraries.values() if path.resolve(strict=True) not in used]
    for path in omitted:
        remove_payload(path)


def deduplicate_binaries(directory):
    """Tar preserves hard links; ZIP remains usable by copying identical aliases."""
    groups = {}
    for path in sorted(directory.iterdir()):
        if path.is_symlink() or not path.is_file():
            continue
        stat = path.stat()
        groups.setdefault((stat.st_size, stat.st_mode), []).append(path)
    for paths in groups.values():
        if len(paths) < 2:
            continue
        known = {}
        for path in paths:
            with path.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            if digest not in known:
                known[digest] = path
                continue
            # Link before replacing: filesystems without hard-link support
            # keep their original complete executable, including on Windows.
            temporary = path.with_name('.' + path.name + '.link')
            try:
                os.link(known[digest], temporary)
                os.replace(temporary, path)
            except OSError:
                temporary.unlink(missing_ok=True)


def compact_toolchain(root, *, profile='pxa', windows=False,
                      objdump='objdump', readelf='readelf'):
    root = root.resolve(strict=True)
    if profile not in ('pxa', 'full'):
        raise ValueError('Unknown toolchain profile')
    before = payload_size(root)
    sysroot = root / 'share/wasi-sysroot'
    for required in ('include/c++/v1', 'include/wasm32-wasip1', 'lib/wasm32-wasip1'):
        if not (sysroot / required).is_dir():
            raise ValueError('Missing WASI preview1 input: ' + required)
    if profile == 'pxa':
        for parent in ('include', 'lib'):
            for target in ('wasm32-wasip1-threads', 'wasm32-wasip2', 'wasm32-wasip3'):
                remove_payload(sysroot / parent / target)
        remove_payload(sysroot / 'experimental-coop-threads')
        tools = {'clang', 'clang++', 'clang.cfg', 'clang++.cfg', 'wasm-ld',
                 'llvm-ar', 'llvm-ranlib', 'llvm-nm', 'llvm-objdump', 'llvm-size',
                 'llvm-objcopy', 'llvm-strip', 'llvm-symbolizer'}
        suffix = '.exe' if windows else ''
        keep = {name if name.endswith('.cfg') else name + suffix for name in tools}
        # Linux aliases point at versioned drivers and the lld/llvm-ar binary.
        for name in tuple(keep):
            path = root / 'bin' / name
            if not path.is_file():
                raise ValueError('Missing required tool: ' + name)
            while path.is_symlink():
                path = path.parent / os.readlink(path)
                if not path.resolve(strict=True).is_relative_to(root / 'bin'):
                    raise ValueError('Compiler alias escapes bin: ' + name)
                keep.add(path.name)
        for path in (root / 'bin').iterdir():
            if path.name not in keep and path.suffix.lower() != '.dll':
                remove_payload(path)
        compact_libraries(root, windows=windows, objdump=objdump, readelf=readelf)
        if windows:
            for path in (root / 'lib').glob('*.lib'):
                remove_payload(path)
    deduplicate_binaries(root / 'bin')
    return {'profile': profile, 'targets': ['wasm32-wasip1'] if profile == 'pxa' else
            ['wasm32-wasip1', 'wasm32-wasip1-threads', 'wasm32-wasip2', 'wasm32-wasip3'],
            'before': before, 'after': payload_size(root)}


def compact_python(root):
    before = payload_size(root)
    # Keep encodings, extension modules and sysconfig metadata. Only build-time
    # static archives are removed; this runtime is not a Python extension SDK.
    for directory in root.glob('lib/python*/config-*'):
        for archive in directory.glob('*.a'):
            remove_payload(archive)
    return {'before': before, 'after': payload_size(root)}


def strip_compiler(path, tool='strip'):
    before = path.stat().st_size
    subprocess.run([tool, '--strip-unneeded', str(path)], check=True, capture_output=True)
    return {'before_bytes': before, 'after_bytes': path.stat().st_size}

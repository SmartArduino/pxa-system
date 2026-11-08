#!/usr/bin/env python3
"""Build native wamrc using PXA's pinned Espressif LLVM, including Xtensa.

Run from a native x64 compiler environment with CMake and Ninja on PATH.
No Bash, flock, uname or container is required. Build output goes to stderr;
stdout contains only the resulting executable path, as in build_wamrc.sh.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pxa_tools.common import ToolError, executable, locked

ROOT = Path(__file__).resolve().parents[2]


def invoke(args, *, capture=False):
    result = subprocess.run([str(a) for a in args], check=True, text=True, encoding='utf-8',
                            stdout=subprocess.PIPE if capture else sys.stderr)
    return result.stdout.strip() if capture else None


def checkout(path, repository, commit):
    if not path.exists():
        invoke(['git', 'init', path])
        if os.name == 'nt':
            invoke(['git', '-C', path, 'config', 'core.longpaths', 'true'])
        invoke(['git', '-C', path, 'remote', 'add', 'origin', repository])
        invoke(['git', '-C', path, 'fetch', '--depth=1', 'origin', commit])
        invoke(['git', '-C', path, 'checkout', '--detach', 'FETCH_HEAD'])
    actual = invoke(['git', '-C', path, 'rev-parse', 'HEAD'], capture=True)
    if actual != commit:
        raise ToolError(f'{path}: expected pinned commit {commit}, found {actual}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, default=Path(os.environ.get('PXA_WAMR_CACHE_DIR', ROOT / '.pxa')))
    parser.add_argument('--jobs', type=int, default=int(os.environ.get('PXA_WAMR_JOBS', min(os.cpu_count() or 1, 8))))
    args = parser.parse_args()
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise ToolError('The native compiler recipe currently requires an x64 host')
    if not 1 <= args.jobs <= 64:
        raise ToolError('--jobs must be in 1..64')
    metadata = json.loads((ROOT / 'config/wamr.json').read_text())
    llvm = metadata['llvm']
    host = 'windows-x64' if os.name == 'nt' else platform.system().lower() + '-x64'
    cache = args.cache.resolve()
    source = Path(os.environ.get('PXA_WAMR_SOURCE_DIR', ROOT / 'wamr')).resolve()
    llvm_source = Path(os.environ.get('PXA_WAMR_LLVM_DIR', cache / ('llvm-' + llvm['commit']))).resolve()
    llvm_build = cache / ('llvm-build-' + llvm['commit'] + '-' + host)
    build = cache / ('wamrc-' + metadata['commit'] + '-' + llvm['commit'] + '-' + host)
    binary = executable(build / 'wamrc')
    with locked(build.with_suffix('.lock')):
        # Verify both source pins even when reusing an existing build.
        checkout(source, metadata['repository'], metadata['commit'])
        checkout(llvm_source, llvm['repository'], llvm['commit'])
        if not binary.is_file():
            options = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
            if os.name == 'nt':
                options.append('-DCMAKE_PROJECT_INCLUDE=' + str(ROOT / 'cmake/PxaWindowsWamrc.cmake'))
            for variable, compiler in (('PXA_WAMR_CC', 'C'), ('PXA_WAMR_CXX', 'CXX')):
                if os.environ.get(variable):
                    options.append(f'-DCMAKE_{compiler}_COMPILER={os.environ[variable]}')
            if os.name != 'nt' or any(value in os.environ.get('PXA_WAMR_CXX', '').lower()
                                      for value in ('g++', 'clang++')):
                options.append('-DCMAKE_CXX_FLAGS=-include cstdint')
            invoke(['cmake', '-S', llvm_source / 'llvm', '-B', llvm_build, *options,
                    '-DLLVM_TARGETS_TO_BUILD=AArch64;ARM;Mips;RISCV;X86',
                    '-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Xtensa',
                    '-DLLVM_INCLUDE_TESTS=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
                    '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_DOCS=OFF',
                    '-DLLVM_INCLUDE_TOOLS=OFF', '-DLLVM_INCLUDE_UTILS=OFF',
                    '-DLLVM_ENABLE_BINDINGS=OFF', '-DLLVM_ENABLE_LIBEDIT=OFF',
                    '-DLLVM_ENABLE_TERMINFO=OFF', '-DLLVM_ENABLE_LIBXML2=OFF',
                    '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
                    '-DLLVM_BUILD_LLVM_DYLIB=OFF', '-DLLVM_APPEND_VC_REV=ON'])
            invoke(['cmake', '--build', llvm_build, '--parallel', args.jobs])
            invoke(['cmake', '-S', source / 'wamr-compiler', '-B', build, *options,
                    '-DWAMR_BUILD_WITH_CUSTOM_LLVM=1',
                    '-DLLVM_DIR=' + str(llvm_build / 'lib/cmake/llvm'),
                    '-DWAMR_BUILD_PLATFORM=' + ('windows' if os.name == 'nt' else platform.system().lower()),
                    '-DWAMR_BUILD_TARGET=X86_64', '-DWAMR_BUILD_LIBC_WASI=0',
                    '-DWAMR_BUILD_LIB_WASI_THREADS=0', '-DWAMR_DISABLE_HW_BOUND_CHECK=1'])
            invoke(['cmake', '--build', build, '--parallel', args.jobs])
        if not binary.is_file():
            raise ToolError(f'Build did not produce {binary}')
    print(binary)


if __name__ == '__main__':
    try:
        main()
    except (ToolError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)

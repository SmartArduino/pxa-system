#!/usr/bin/env python3
"""Build a bounded browser runtime bundle without changing the Guest SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--wasi-sdk', type=Path, required=True)
    parser.add_argument('--host-build', type=Path, help='Reuse a separately compiled Emscripten Host')
    parser.add_argument('--lvgl', type=Path, help='Existing LVGL 9.6.0 tree; otherwise fetch pinned version')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    out = args.out.resolve()
    host = args.host_build.resolve() if args.host_build else build / 'host'
    if not args.host_build:
        options = [f'-DPXSYS_LVGL_SOURCE_DIR={args.lvgl.resolve()}'] if args.lvgl else []
        run('emcmake', 'cmake', '-S', ROOT / 'simulator/browser', '-B', host,
            '-DCMAKE_BUILD_TYPE=Release', *options)
        run('cmake', '--build', host, '-j', args.jobs)
    apps = []
    for name, source in [('paddle', ROOT / 'simulator/browser/demo'),
                         ('counter', ROOT / 'ai/templates/c-app'),
                         ('cpp', ROOT / 'sdk/templates/hello')]:
        target = build / name
        run('cmake', '-S', source, '-B', target, '-DCMAKE_BUILD_TYPE=Release',
            f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/sdk/cmake/pxa-wasi-toolchain.cmake',
            f'-DPxa_DIR={ROOT}/sdk/cmake', f'-DPXA_GUEST_SDK_DIR={ROOT}/sdk/guest-c',
            f'-DWASI_SDK_DIR={args.wasi_sdk.resolve()}')
        run('cmake', '--build', target, '-j', args.jobs)
        apps.append((name, target / 'artifacts/main.wasm'))
    out.mkdir(parents=True, exist_ok=True)
    for filename in ['pxa-browser.mjs', 'pxa-browser.wasm']:
        shutil.copy2(host / filename, out / filename)
    for filename in ['runtime.mjs', 'worker.mjs']:
        shutil.copy2(ROOT / 'simulator/browser' / filename, out / filename)
    manifest = {'version': '0.1.0', 'apps': []}
    for name, source in apps:
        filename = f'{name}.wasm'
        shutil.copy2(source, out / filename)
        data = source.read_bytes()
        manifest['apps'].append({'id': name, 'file': filename, 'bytes': len(data),
                                 'sha256': hashlib.sha256(data).hexdigest()})
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shutil.copy2(ROOT / 'LICENSE', out / 'LICENSE-PXA')
    # Keep third-party license text with the deployed runtime.
    lvgl = args.lvgl.resolve() if args.lvgl else host / '_deps/lvgl_source-src'
    shutil.copy2(lvgl / 'LICENCE.txt', out / 'LICENSE-LVGL')
    for source, name in [(ROOT / 'ui/reference/lvgl/fonts/Montserrat-OFL.txt', 'LICENSE-Montserrat'),
                         (ROOT / 'libpxa/adapters/lvgl/fonts/symbols-LICENSE.txt', 'LICENSE-symbols'),
                         (ROOT / 'libpxa/adapters/lvgl/fonts/OFL.txt', 'LICENSE-OFL')]:
        shutil.copy2(source, out / name)
    print(f'Browser bundle: {out}')


if __name__ == '__main__':
    main()

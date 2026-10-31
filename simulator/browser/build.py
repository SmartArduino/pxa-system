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
                         ('notes', ROOT / 'simulator/browser/services-demo'),
                         ('mainloop-test', ROOT / 'simulator/browser/main-loop-test'),
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
    for filename in ['pxa-browser.mjs', 'pxa-browser.wasm', 'LICENSE-Emscripten', 'LICENSE-musl', 'LICENSE-compiler-rt']:
        shutil.copy2(host / filename, out / filename)
    for filename in ['runtime.mjs', 'worker.mjs', 'fonts.mjs', 'storage.mjs', 'network.mjs', 'borrow.mjs', 'wasm-policy.mjs', 'package.mjs', 'surface.mjs', 'capture.mjs', 'sockets.mjs', 'microphone-worklet.mjs', 'audio-worklet.mjs', 'capabilities.json']:
        shutil.copy2(ROOT / 'simulator/browser' / filename, out / filename)
    version = json.loads((ROOT / 'simulator/browser/capabilities.json').read_text())['version']
    manifest = {'version': version, 'apps': []}
    for name, source in apps:
        filename = f'{name}.wasm'
        shutil.copy2(source, out / filename)
        data = source.read_bytes()
        manifest['apps'].append({'id': name, 'file': filename, 'bytes': len(data),
                                 'sha256': hashlib.sha256(data).hexdigest()})
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shutil.copy2(ROOT / 'LICENSE', out / 'LICENSE-PXA')
    shutil.copy2(ROOT / 'libpxa/THIRD_PARTY_NOTICES.md', out / 'THIRD_PARTY_NOTICES.md')
    # Reusing an output directory must not retain retired Host notices.
    for filename in ('LICENSE-GPL-2.0', 'LICENSE-MPL-2.0'):
        (out / filename).unlink(missing_ok=True)
    (out / 'NOTICE').write_text(
        'PXA Browser Host: PXA Core, generic raster/compute kernels and PSG (MIT), '
        'LVGL (MIT), AnimatedGIF (Apache-2.0), pngle (MIT), miniz (public domain), TJpgDec (ChaN permissive), Montserrat and symbols fonts (SIL OFL; see included licenses). '
        'The LVGL FrogFS and internal ThorVG sources are excluded from compilation. '
        'Toolchain runtime: Emscripten (MIT selected), musl (MIT and retained permissive notices), '
        'compiler-rt (Apache-2.0 WITH LLVM-exception). Included license files retain upstream terms. '
        'Engine-specific rendering and emulation remain in separately licensed applications. '
        'Sources and build instructions: '
        f'https://github.com/SmartArduino/pxa-system/tree/browser-v{version}/simulator/browser\n')
    # Keep third-party license text with the deployed runtime.
    lvgl = args.lvgl.resolve() if args.lvgl else host / '_deps/lvgl_source-src'
    shutil.copy2(lvgl / 'LICENCE.txt', out / 'LICENSE-LVGL')
    for source, name in [(lvgl / 'src/libs/gif/LICENSE', 'LICENSE-AnimatedGIF'), (ROOT / 'ui/reference/lvgl/fonts/Montserrat-OFL.txt', 'LICENSE-Montserrat'),
                         (ROOT / 'libpxa/adapters/lvgl/fonts/symbols-LICENSE.txt', 'LICENSE-symbols'),
                         (ROOT / 'libpxa/adapters/lvgl/fonts/OFL.txt', 'LICENSE-OFL')]:
        shutil.copy2(source, out / name)
    pngle = (ROOT / 'libpxa/adapters/image/vendor/pngle.c').read_text()
    miniz = (ROOT / 'libpxa/adapters/image/vendor/miniz.c').read_text()
    jpeg = (lvgl / 'src/libs/tjpgd/tjpgd.c').read_text()
    (out / 'LICENSE-image-codecs').write_text(
        'pngle (MIT)\n' + pngle[pngle.index('/*-'):pngle.index('*/')+2] +
        '\n\nminiz (public domain dedication)\n' + miniz[miniz.rindex('/*'):] +
        '\n\nTJpgDec (ChaN)\n' + jpeg[:jpeg.index('*/')+2])
    print(f'Browser bundle: {out}')


if __name__ == '__main__':
    main()

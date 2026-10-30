#!/usr/bin/env python3
"""Build a reproducible, signed optional AI kit without rebuilding the SDK."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from assemble import archive, run, sha


def build(source: Path, output: Path, key: Path, *, allow_dirty=False):
    dirty = bool(run('git', '-C', source, 'status', '--porcelain'))
    if dirty and not allow_dirty:
        raise ValueError('Official AI-kit releases require committed source')
    metadata = json.loads((source / 'ai/plugin.json').read_text())
    version = metadata['version']
    commit = run('git', '-C', source, 'rev-parse', 'HEAD')
    epoch = int(run('git', '-C', source, 'show', '-s', '--format=%ct', 'HEAD'))
    output.mkdir(parents=True, exist_ok=True)
    names = [f'pxa-ai-kit-{version}.tar.gz', f'ai-kit-{version}.json',
             f'ai-kit-{version}.json.sig', f'pxa-ai-kit-{version}.SHA256SUMS']
    if any((output / name).exists() for name in names):
        raise ValueError('Release assets already exist; use a new output directory')
    if any(p.is_symlink() for p in (source / 'ai').rglob('*')):
        raise ValueError('AI-kit source must not contain symlinks')
    with tempfile.TemporaryDirectory(prefix='pxa-ai-kit-') as temporary:
        stage = Path(temporary) / f'pxa-ai-kit-{version}'
        if allow_dirty:
            shutil.copytree(source / 'ai', stage, ignore=shutil.ignore_patterns(
                '__pycache__', '*.pyc', '.venv', '.env', '.pytest_cache', '.ruff_cache'))
        else:
            # Official releases contain reviewed Git inputs only, never ignored
            # virtual environments, machine configuration or local credentials.
            for relative in run('git', '-C', source, 'ls-files', 'ai').splitlines():
                original = source / relative
                destination = stage / original.relative_to(source / 'ai')
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, destination)
        shutil.copy2(source / 'LICENSE', stage / 'LICENSE')
        files = {str(path.relative_to(stage)): sha(path) for path in sorted(stage.rglob('*')) if path.is_file()}
        (stage / 'manifest.json').write_text(json.dumps({'schema': 'pxa-ai-kit-files-1', 'version': version,
            'files': files}, sort_keys=True, indent=2) + '\n')
        archive(stage.parent, [stage.name], output / names[0], epoch)
    manifest = {'schema': 'pxa-ai-distribution-1', 'version': version,
                'devkit': {'minimum_release': '0.5.0', 'tested_release': (source / 'VERSION').read_text().strip(), 'core_major': 2},
                'source': {'commit': commit, 'dirty': dirty},
                'archive': {'file': names[0], 'url': f'https://github.com/SmartArduino/pxa-system/releases/download/ai-kit-v{version}/{names[0]}', 'size': (output / names[0]).stat().st_size, 'sha256': sha(output / names[0])}}
    (output / names[1]).write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(key), '-out', str(output / names[2]),
                    str(output / names[1])], check=True)
    (output / names[3]).write_text(''.join(f'{sha(output / name)}  {name}\n' for name in names[:3]))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--signing-key', type=Path, required=True)
    parser.add_argument('--allow-dirty', action='store_true', help='local validation only; do not publish')
    args = parser.parse_args()
    print(json.dumps(build(args.source.resolve(), args.output.resolve(), args.signing_key.resolve(strict=True),
                           allow_dirty=args.allow_dirty), indent=2))


if __name__ == '__main__':
    main()

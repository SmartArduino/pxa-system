#!/usr/bin/env python3
"""Install optional PXA skills/agents into a project, without global settings."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import tempfile
import os

ROOT = Path(__file__).resolve().parent
RECEIPT = '.agents/pxa-ai-kit.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def destination(project, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or '..' in relative.parts or '\\' in name:
        raise ValueError('Invalid managed path: ' + name)
    parts = relative.parts
    valid = (name == RECEIPT or
             (len(parts) >= 4 and parts[:2] == ('.agents', 'skills') and parts[2].startswith('pxa-')) or
             (len(parts) == 3 and parts[:2] == ('.codex', 'agents') and parts[2].startswith('pxa-') and name.endswith('.toml')))
    if not valid:
        raise ValueError('Invalid managed path: ' + name)
    path = project / relative
    current = path
    while current != project:
        if current.is_symlink():
            raise ValueError('Refusing a symlink destination: ' + str(current))
        current = current.parent
    if path.exists() and not path.is_file():
        raise ValueError('Destination is not a file: ' + str(path))
    return path


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.pxa-ai-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data)
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def bundle():
    metadata = json.loads((ROOT / 'plugin.json').read_text())
    manifest_path = ROOT / 'manifest.json'
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        for name, expected in manifest['files'].items():
            path = ROOT / name
            if not path.resolve().is_relative_to(ROOT) or not path.is_file() or digest(path.read_bytes()) != expected:
                raise ValueError('Bundle integrity check failed: ' + name)
    return metadata


def apply(project, changes):
    # Preflight every conflict before making the first mutation. Keep the tiny
    # previous files for rollback if a write fails partway through the set.
    previous = {name: (path.read_bytes() if path.exists() else None)
                for name in changes for path in [destination(project, name)]}
    done = []
    try:
        for name, data in changes.items():
            path = destination(project, name)
            if data is None:
                path.unlink(missing_ok=True)
            else:
                atomic_write(path, data)
            done.append(name)
    except BaseException:
        for name in reversed(done):
            path = destination(project, name)
            if previous[name] is None:
                path.unlink(missing_ok=True)
            else:
                atomic_write(path, previous[name])
        raise
    for name in changes:
        parent = (project / name).parent
        while parent != project:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('list')
    for action in ('install', 'remove'):
        command = sub.add_parser(action)
        command.add_argument('--project', type=Path, required=True)
        command.add_argument('--dry-run', action='store_true')
        if action == 'install':
            command.add_argument('--skill', action='append', choices=sorted(p.name for p in (ROOT / 'skills').iterdir() if p.is_dir()))
            command.add_argument('--agents', action='store_true', help='also install project-scoped Codex agents')
            command.add_argument('--update', action='store_true', help='update only unmodified files owned by this kit')
    args = parser.parse_args()
    try:
        metadata = bundle()
        if args.action == 'list':
            print(json.dumps({'version': metadata['version'], 'skills': sorted(p.name for p in (ROOT / 'skills').iterdir() if p.is_dir()),
                              'agents': sorted(p.stem for p in (ROOT / 'agents').glob('*.toml'))}, indent=2))
            return 0
        project = args.project.expanduser().resolve(strict=True)
        if not project.is_dir():
            raise ValueError('Project must be an existing directory')
        receipt_path = destination(project, RECEIPT)
        receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {'files': {}}
        owned = receipt['files']
        changes = {}
        if args.action == 'remove':
            for name, expected in owned.items():
                path = destination(project, name)
                if path.exists() and digest(path.read_bytes()) != expected:
                    raise ValueError('Locally edited file retained; move it before removal: ' + name)
                changes[name] = None
            if receipt_path.exists():
                changes[RECEIPT] = None
        else:
            selected = args.skill or sorted(p.name for p in (ROOT / 'skills').iterdir() if p.is_dir())
            sources = {}
            for name in selected:
                for path in sorted((ROOT / 'skills' / name).rglob('*')):
                    if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc':
                        sources['.agents/skills/' + str(path.relative_to(ROOT / 'skills'))] = path.read_bytes()
            if args.agents:
                sources.update({'.codex/agents/' + path.name: path.read_bytes() for path in sorted((ROOT / 'agents').glob('*.toml'))})
            for name, data in sources.items():
                path = destination(project, name)
                if path.exists():
                    actual = digest(path.read_bytes())
                    if name not in owned or actual != owned[name]:
                        raise ValueError('Existing or locally edited file retained: ' + name)
                    if actual == digest(data):
                        continue
                    if not args.update:
                        raise ValueError('New kit content available; use --update: ' + name)
                changes[name] = data
            files = {**owned, **{name: digest(data) for name, data in sources.items()}}
            new_receipt = (json.dumps({'schema': 'pxa-ai-install-1', 'version': metadata['version'], 'files': files}, sort_keys=True, indent=2) + '\n').encode()
            if not receipt_path.exists() or receipt_path.read_bytes() != new_receipt:
                changes[RECEIPT] = new_receipt
        if not args.dry_run:
            apply(project, changes)
        print(json.dumps({'action': args.action, 'project': str(project), 'dry_run': args.dry_run,
                          'changed': sorted(changes)}, indent=2))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print('pxa-ai-kit: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())

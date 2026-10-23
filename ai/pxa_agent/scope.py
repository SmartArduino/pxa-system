"""Change-boundary checks, not an OS sandbox. Never revert a user's files."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess

from .common import ToolError, atomic_json, digest, evidence

SCHEMA = 'pxa-change-scope-1'
SKIP = {'.git', '.venv', '__pycache__', 'node_modules', '.pytest_cache'}


def inside(path, root):
    return path == root or path.is_relative_to(root)


def state_path(workspace):
    path = Path(workspace).resolve() / '.pxa/change-scope.json'
    if path.parent.is_symlink() or path.is_symlink():
        raise ToolError('Scope state must not be a symlink')
    return path


def files(root):
    """Git includes tracked ignored files; recursively inspect submodules too."""
    root = Path(root)
    if root.is_file() or root.is_symlink():
        yield root
        return
    if not root.exists():
        return
    probe = subprocess.run(['git', '-C', str(root), 'rev-parse', '--show-toplevel'],
                           capture_output=True, text=True) if (root / '.git').exists() else None
    if probe and probe.returncode == 0 and Path(probe.stdout.strip()).resolve() == root.resolve():
        output = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--cached',
                                 '--others', '--exclude-standard'], capture_output=True, check=True).stdout
        for name in sorted(set(os.fsdecode(output).split('\0')) - {''}):
            path = root / name
            if path.is_dir() and not path.is_symlink():
                yield from files(path)
            elif path.exists() or path.is_symlink():
                yield path
        return
    for directory, names, entries in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in list(names):
            path = base / name
            if name in SKIP or path.is_symlink() or path.relative_to(root).as_posix() in (
                    '.pxa', 'local/artifacts', 'build', 'dist'):
                names.remove(name)
                if path.is_symlink():
                    yield path
        for name in entries:
            if name not in SKIP and not name.endswith('.pyc'):
                yield base / name


def snapshot(workspace, roots, allowed, previous=None):
    previous = previous or {}
    result = {}
    permitted = [Path(p) for p in allowed]
    for root in map(Path, roots):
        for path in files(root):
            # Compare lexical paths; never follow an escaping symlink.
            if any(inside(path, p) for p in permitted):
                continue
            if any(part in SKIP for part in path.parts):
                continue
            if inside(path, workspace / '.pxa') or inside(path, workspace / 'local/artifacts'):
                continue
            name = str(path)
            stat = path.lstat()
            stamp = [stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_mode, stat.st_ino]
            cached = previous.get(name, {})
            if cached.get('stamp') == stamp:
                fingerprint = cached['hash']
            elif path.is_symlink():
                fingerprint = 'link:' + os.readlink(path)
            else:
                fingerprint = digest(path)
            result[name] = {'stamp': stamp, 'hash': fingerprint, 'mode': stat.st_mode & 0o777}
    return result


def begin(workspace, *, mode, project=None, board=None, sdk=None):
    workspace = Path(workspace).resolve()
    destination = state_path(workspace)
    if destination.exists():
        enforce(workspace)
    allowed = []
    if mode == 'app':
        if not project:
            raise ToolError('App scope needs a project path')
        path = (workspace / project).resolve()
        if not path.is_relative_to(workspace) or path == workspace:
            raise ToolError('Use an app directory inside the workspace, preferably apps/name')
        if path.relative_to(workspace).parts[0] in ('deps', 'sdk', 'firmware', 'tools', 'spec', '.pxa', 'local'):
            raise ToolError('App scope cannot include SDK, firmware, tools or artifact directories')
        if any((path / name).exists() for name in ('firmware', 'sdk/guest-c', 'sdk/guest-cpp')):
            raise ToolError('Select the app, not a platform checkout')
        allowed = [str(path)]
    elif mode == 'board':
        from .boards import board_id
        board = board_id(board)
        allowed = [str(workspace / 'firmware/boards' / board),
                   str(workspace / 'simulator/profiles' / (board + '.toml')),
                   str(workspace / 'firmware' / ('dependencies.lock.' + board))]
        for value in allowed:
            if Path(value).resolve() != Path(value):
                raise ToolError('Board scope cannot use symlink destinations')
    else:
        raise ToolError('Scope must be app or board')
    roots = [str(workspace)]
    if sdk:
        sdk = Path(sdk).resolve()
        if inside(sdk, Path(allowed[0])) or inside(Path(allowed[0]), sdk):
            raise ToolError('The app and SDK must be separate')
        # Runtime/toolchain binaries are distribution inputs, not source edits.
        roots += [str(sdk / p) for p in ('sdk', 'spec', 'tools', 'config', 'VERSION', 'bin/pxa', 'bin/pxadb')]
    value = {'schema': SCHEMA, 'mode': mode, 'allowed': allowed, 'roots': roots,
             'board': board, 'project': str(path) if mode == 'app' else None}
    value['baseline'] = snapshot(workspace, roots, allowed)
    atomic_json(destination, value)
    return {'mode': mode, 'allowed': allowed, 'protected_files': len(value['baseline']),
            'boundary': 'change audit; not a sandbox', 'baseline': str(destination)}


def check(workspace):
    workspace = Path(workspace).resolve()
    path = state_path(workspace)
    if not path.exists():
        return {'active': False, 'passed': False, 'next': 'Start scope before editing source'}
    state = json.loads(path.read_text())
    if state.get('schema') != SCHEMA:
        raise ToolError('Unsupported scope state')
    cache = workspace / '.pxa/change-scope-cache.json'
    previous = json.loads(cache.read_text()) if cache.is_file() and not cache.is_symlink() else state['baseline']
    current = snapshot(workspace, state['roots'], state['allowed'], previous)
    changed = sorted(p for p in state['baseline'].keys() | current.keys()
                     if any(state['baseline'].get(p, {}).get(key) != current.get(p, {}).get(key)
                            for key in ('hash', 'mode')
                            if key != 'mode' or 'mode' in state['baseline'].get(p, {})))
    # An allowed directory may have been replaced by a symlink after begin.
    changed += [p for p in state['allowed'] if Path(p).resolve() != Path(p)]
    atomic_json(cache, current)
    return {'active': True, 'passed': not changed, 'mode': state['mode'], 'allowed': state['allowed'],
            'violations': changed, 'protected_files': len(current)}


def enforce(workspace):
    result = check(workspace)
    if result['active'] and not result['passed']:
        raise ToolError('Out-of-scope changes detected; retain and review them before continuing:\n' +
                        '\n'.join(result['violations'][:40]))
    return result


def finish(workspace):
    workspace = Path(workspace).resolve()
    result = enforce(workspace)
    if not result['active']:
        return {'active': False, 'ended': False}
    archive = evidence(workspace, 'pxa-scope') / 'scope.json'
    state_path(workspace).replace(archive)
    return {'active': False, 'ended': True, 'passed': True, 'archive': str(archive)}


def app(environment, project):
    if environment.config.get('mode') == 'board':
        raise ToolError('Use a separate app workspace; this environment is configured for board adaptation')
    project = environment.project(project)
    path = state_path(environment.workspace)
    if path.exists():
        enforce(environment.workspace)
        current = json.loads(path.read_text())
        if current['mode'] != 'app':
            raise ToolError('Board scope is active; finish its check before beginning app work')
        if current['project'] == str(project):
            return
    begin(environment.workspace, mode='app', project=project,
          sdk=environment.config.get('sdk'))

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys

from .build import build
from .common import ToolError, atomic_json, command
from .project import Project, SDK_ROOT, ID_PATTERN, lock_project, resolve_tools


def parser():
    result = argparse.ArgumentParser(prog='pxa', description='PXA C++ application development tools')
    result.add_argument('--version', action='version', version='PXA ' + (SDK_ROOT / 'VERSION').read_text().strip())
    commands = result.add_subparsers(dest='command', required=True)
    init = commands.add_parser('init', help='create a C++ application')
    init.add_argument('directory', type=Path)
    init.add_argument('--id')
    for name in ('build', 'run'):
        cmd = commands.add_parser(name)
        cmd.add_argument('project', type=Path, nargs='?', default=Path.cwd())
        cmd.add_argument('--target', default='simulator')
        cmd.add_argument('--output', type=Path)
        cmd.add_argument('--signing-key')
        cmd.add_argument('--offline', action='store_true')
        cmd.add_argument('--update-lock', action='store_true')
        cmd.add_argument('--aot-only', action='store_true')
        cmd.add_argument('--define', action='append', default=[])
        cmd.add_argument('--jobs', type=int)
        cmd.add_argument('--debug', action='store_true')
        if name == 'run':
            cmd.add_argument('--profile', default='pai-touch')
            cmd.add_argument('--device')
            cmd.add_argument('--baud', type=int, default=2000000)
            cmd.add_argument('--watch', action='store_true')
            cmd.add_argument('--duration-ms', type=int)
            cmd.add_argument('--screenshot', type=Path)
    sdk = commands.add_parser('sdk')
    sub = sdk.add_subparsers(dest='sdk_command', required=True)
    update = sub.add_parser('update')
    update.add_argument('project', nargs='?', type=Path, default=Path.cwd())
    update.add_argument('--offline', action='store_true')
    install = sub.add_parser('install')
    install.add_argument('release')
    install.add_argument('--manifest', help='HTTPS release manifest or offline distribution.json')
    install.add_argument('--offline', action='store_true')
    device = commands.add_parser('device', help='PXADB device tools')
    device.add_argument('arguments', nargs=argparse.REMAINDER)
    publish = commands.add_parser('publish', help='verify and upload an existing signed package')
    publish.add_argument('package', type=Path)
    publish.add_argument('--check-only', action='store_true')
    publish.add_argument('--server', default='https://app.doit.am')
    publish.add_argument('--token-file', type=Path)
    return result


def init_project(directory: Path, app_id: str | None):
    if directory.exists() and any(directory.iterdir()):
        raise ToolError('init requires an empty directory')
    slug = re.sub(r'[^a-z0-9._-]+', '-', directory.name.lower()).strip('.-_')[:60]
    app_id = app_id or ('pxa-' + (slug or 'app'))
    if not ID_PATTERN.fullmatch(app_id):
        raise ToolError('Invalid application id; use a lowercase letter followed by letters, digits, dots, hyphens or underscores (at most 64 characters)')
    template = SDK_ROOT / 'sdk/templates/hello'
    shutil.copytree(template, directory, dirs_exist_ok=True)
    metadata = json.loads((directory / 'package.json').read_text())
    metadata.update(id=app_id, name=directory.name)
    atomic_json(directory / 'package.json', metadata)
    Project.open(directory)
    print(f'Created {directory.resolve()}. Run: pxa run "{directory}"')


def main():
    args = parser().parse_args()
    try:
        if args.command in ('build', 'run') and not args.update_lock:
            path = args.project / 'pxa.lock'
            if path.is_file():
                locked_version = json.loads(path.read_text()).get('sdk', {}).get('release')
                current = (SDK_ROOT / 'VERSION').read_text().strip()
                if locked_version and locked_version != current:
                    from .distribution import data_root
                    launcher = data_root() / 'releases' / locked_version / 'bin/pxa'
                    if not launcher.is_file():
                        raise ToolError(f'Locked SDK {locked_version} is missing; run pxa sdk install {locked_version}')
                    if os.environ.get('PXA_SDK_REDIRECT') == locked_version:
                        raise ToolError('Installed locked SDK version is inconsistent')
                    env = os.environ.copy()
                    env['PXA_SDK_REDIRECT'] = locked_version
                    env.pop('PYTHONHOME', None)
                    os.execve(launcher, [str(launcher), *sys.argv[1:]], env)
        if args.command == 'init':
            init_project(args.directory, args.id)
        elif args.command in ('build', 'run'):
            project = Project.open(args.project)
            if args.command == 'run':
                from .run import run
                run(project, args)
            else:
                build(project, targets=args.target.split(','), output=args.output,
                      key=args.signing_key, offline=args.offline, update_lock=args.update_lock,
                      definitions=args.define, aot_only=args.aot_only, jobs=args.jobs,
                      build_type='Debug' if args.debug else 'Release')
        elif args.command == 'sdk':
            if args.sdk_command == 'update':
                project = Project.open(args.project)
                wasi, wamrc = resolve_tools(SDK_ROOT, offline=args.offline)
                previous = json.loads((project.path / 'pxa.lock').read_text()) if (project.path / 'pxa.lock').exists() else None
                current = lock_project(project, SDK_ROOT, wasi, wamrc, update=True)
                print(json.dumps({'before': previous, 'after': current}, indent=2))
            else:
                from .distribution import install
                print(install(args.release, args.manifest, offline=args.offline))
        elif args.command == 'device':
            from .run import pxadb_path
            command([sys.executable, pxadb_path(), *args.arguments])
        elif args.command == 'publish':
            from .publish import publish
            publish(args.package, args.server, args.token_file, check_only=args.check_only)
        return 0
    except (ToolError, OSError, ValueError) as error:
        print(f'pxa: {error}', file=sys.stderr)
        return 1

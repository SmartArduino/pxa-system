from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from . import VERSION
from .common import ToolError, evidence
from .environment import Environment, configure


def parser():
    result = argparse.ArgumentParser(prog='pxa-agent', description='PXA environment, AI tools, simulator, devices and publishing')
    result.add_argument('--version', action='version', version='PXA Agent Kit ' + VERSION)
    result.add_argument('--workspace', type=Path, default=Path.cwd())
    commands = result.add_subparsers(dest='command', required=True)
    setup = commands.add_parser('setup', help='install and verify the environment, skills and project-local MCP configuration')
    setup.add_argument('--engine', choices=('auto', 'native', 'docker', 'podman'), default='auto')
    setup.add_argument('--sdk', type=Path, help='reuse a user-selected, complete DevKit')
    setup.add_argument('--sdk-version')
    setup.add_argument('--manifest', help='signed SDK or host-tools manifest; a local path permits offline setup')
    setup.add_argument('--platform', choices=('all', 'codex', 'claude', 'dsh', 'generic'), default='all')
    setup.add_argument('--no-mcp', action='store_true')
    setup.add_argument('--no-smoke', action='store_true', help='skip the initial real Guest environment check')
    commands.add_parser('doctor')
    commands.add_parser('mcp', help='serve tools to an MCP client over stdio')
    for name in ('init', 'build', 'run', 'sdk', 'device'):
        command = commands.add_parser(name)
        command.add_argument('arguments', nargs=argparse.REMAINDER)
    verify = commands.add_parser('verify')
    verify.add_argument('project')
    verify.add_argument('--profiles', nargs='+')
    verify.add_argument('--scenario')
    verify.add_argument('--window', action='store_true')
    install = commands.add_parser('install', help='build for the connected CPU, install and start the app')
    install.add_argument('project')
    install.add_argument('--port', required=True)
    install.add_argument('--baud', type=int, default=2000000)
    firmware = commands.add_parser('firmware')
    sub = firmware.add_subparsers(dest='action', required=True)
    from .firmware import BOARDS
    for name in ('prepare', 'upgrade'):
        cmd = sub.add_parser(name)
        cmd.add_argument('--board', choices=BOARDS, required=True)
        cmd.add_argument('--release', default='latest')
        cmd.add_argument('--mode', choices=('app', 'full'), default='app')
        cmd.add_argument('--manifest')
        if name == 'upgrade':
            cmd.add_argument('--port', required=True)
            cmd.add_argument('--baud', type=int, default=2000000)
            cmd.add_argument('--erase-data', action='store_true')
    store = commands.add_parser('store')
    sub = store.add_subparsers(dest='action', required=True)
    status = sub.add_parser('status')
    status.add_argument('reference')
    for name in ('authorize', 'publish'):
        cmd = sub.add_parser(name)
        cmd.add_argument('project')
        cmd.add_argument('package')
        cmd.add_argument('--server', default='https://app.doit.am')
        if name == 'authorize':
            cmd.add_argument('--signing-key', type=Path)
            cmd.add_argument('--no-open', action='store_true')
            cmd.add_argument('--wait', action='store_true')
            cmd.add_argument('--renew', action='store_true', help='replace a revoked or unwanted cached authorization')
        else:
            cmd.add_argument('--reference')
            cmd.add_argument('--icon')
            cmd.add_argument('--screenshot', action='append', default=[])
            cmd.add_argument('--changelog', default='')
    return result


def main():
    args = parser().parse_args()
    try:
        result = None
        if args.command == 'setup':
            from .integration import install
            env = configure(args.workspace, engine=args.engine, sdk=args.sdk, version=args.sdk_version, manifest=args.manifest)
            result = {'environment': env.doctor(), 'integration': install(env.workspace, platforms=args.platform, mcp=not args.no_mcp)}
            if not args.no_smoke:
                print('PXA: compiling and running the real Guest environment check', file=sys.stderr, flush=True)
                project = env.workspace / '.pxa/environment-check'
                if not project.exists():
                    env.call(['init', project, '--id', 'pxa-environment-check'])
                output = evidence(env.workspace, 'pxa-setup')
                env.call(['run', project, '--offline', '--duration-ms', '800', '--screenshot', output / 'environment.png'],
                         timeout=600, extra_env={'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy'})
                if not (output / 'environment.png').is_file():
                    raise ToolError('The simulator did not produce its environment-check screenshot')
                result['smoke'] = {'runtime_passed': True, 'screenshot': str(output / 'environment.png')}
        else:
            env = Environment(args.workspace)
            if args.command == 'doctor':
                result = env.doctor()
            elif args.command == 'mcp':
                from .mcp_server import serve
                serve(env)
                return 0
            elif args.command == 'device':
                print(env.device(args.arguments), end='')
                return 0
            elif args.command in ('init', 'build', 'run', 'sdk'):
                arguments = args.arguments
                if args.command in ('build', 'run') and '--online' not in arguments and '--offline' not in arguments:
                    arguments = [*arguments, '--offline']
                arguments = [arg for arg in arguments if arg != '--online']
                if args.command in ('init', 'build', 'run'):
                    if not arguments or arguments[0].startswith('-'):
                        raise ToolError('Specify the app directory inside this workspace')
                    arguments[0] = env.project(arguments[0])
                if args.command in ('build', 'run'):
                    key = arguments[arguments.index('--signing-key') + 1] if '--signing-key' in arguments else None
                    env.ensure_identity(arguments[0], key)
                if args.command == 'run':
                    if '--device' in arguments:
                        raise ToolError('Use install <project> --port <port> for host-native device installation')
                    from .workbench import Jobs
                    jobs = Jobs(env)
                    try:
                        state = jobs.start(['run', *arguments, *([] if '--debug' in arguments else ['--debug'])], timeout=3600)
                        offset = 0
                        log = Path(state['log'])
                        while True:
                            state = jobs.status(state['job_id'])
                            if log.exists():
                                with log.open('rb') as stream:
                                    stream.seek(offset)
                                    data = stream.read()
                                    offset = stream.tell()
                                sys.stdout.write(data.decode(errors='replace'))
                                sys.stdout.flush()
                            if not state['running']:
                                return 0 if state['returncode'] == 0 else 1
                            time.sleep(.2)
                    finally:
                        jobs.close()
                print(env.call([args.command, *arguments], timeout=900), end='')
                return 0
            elif args.command == 'verify':
                from .workbench import verification
                result = verification(env, args.project, profiles=args.profiles, scenario=args.scenario, headless=not args.window)
            elif args.command == 'install':
                from .workbench import install_app
                result = install_app(env, args.project, port=args.port, baud=args.baud)
            elif args.command == 'firmware':
                from .firmware import prepare, flash
                if args.action == 'prepare':
                    result = prepare(args.board, version=args.release, mode=args.mode, manifest=args.manifest)
                else:
                    result = flash(env, args.board, port=args.port, baud=args.baud, version=args.release,
                                   mode=args.mode, erase_data=args.erase_data, manifest=args.manifest)
            elif args.command == 'store':
                from .store import authorize, status, publish
                if args.action == 'authorize':
                    result = authorize(env, args.project, args.package, server=args.server,
                        key=args.signing_key, open_browser=not args.no_open, renew=args.renew)
                    print(json.dumps(result, ensure_ascii=False), flush=True)
                    if args.wait:
                        reference = result['credential_ref']
                        while True:
                            result = status(reference)
                            if result.get('authorized') or result.get('error') not in ('authorization_pending', 'slow_down'):
                                break
                            time.sleep(result.get('poll_after_seconds', 5))
                    else:
                        return 0
                elif args.action == 'status':
                    result = status(args.reference)
                else:
                    result = publish(env, args.project, args.package, server=args.server,
                        reference=args.reference, icon=args.icon, screenshots=args.screenshot, changelog=args.changelog)
        if result is not None:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        return 1 if result and result.get('runtime_passed') is False else 0
    except (ToolError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print('pxa-agent: ' + str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130

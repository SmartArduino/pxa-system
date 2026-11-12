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
    setup.add_argument('--keep-archives', action='store_true', help='retain downloaded SDK archives for offline reuse')
    commands.add_parser('doctor').add_argument('--usage', action='store_true', help='include download cache usage')
    workspace=commands.add_parser('workspace',help='explicit workspace SDK migration')
    workspace_commands=workspace.add_subparsers(dest='action',required=True)
    migrate=workspace_commands.add_parser('migrate',help='select a new SDK, keeping app locks and signing identities')
    migrate.add_argument('--sdk-version',required=True)
    migrate.add_argument('--sdk',type=Path)
    migrate.add_argument('--engine',choices=('auto','native','docker','podman'),default='auto')
    migrate.add_argument('--manifest')
    profile = commands.add_parser('device-profile', help='read device design constraints or explain USB/PXADB setup')
    profile.add_argument('--port')
    profile.add_argument('--baud', type=int, default=2000000)
    profile.add_argument('--package',help='compare this prepared package with the device without uploading')
    commands.add_parser('mcp', help='serve tools to an MCP client over stdio')
    init = commands.add_parser('init', help='create a C or C++ app without editing the SDK')
    init.add_argument('project')
    init.add_argument('--id')
    init.add_argument('--language', choices=('c', 'cpp'), default='cpp')
    commands.add_parser('recipes', help='list runnable app starters')
    new = commands.add_parser('new', help='create a new app from a runnable recipe without changing the SDK')
    new.add_argument('project')
    new.add_argument('--recipe', choices=('timer', 'counter'), required=True)
    new.add_argument('--id')
    new.add_argument('--title')
    new.add_argument('--minutes', type=int)
    new.add_argument('--break-minutes', type=int)
    context = commands.add_parser('context', help='read compact, version-matched app and SDK context')
    context.add_argument('project')
    context.add_argument('--query', default='')
    context.add_argument('--language', choices=('c', 'cpp'), default='cpp')
    context.add_argument('--limit', type=int, default=12)
    scope = commands.add_parser('scope', help='audit app/board edit boundaries (not a sandbox)')
    scope_sub = scope.add_subparsers(dest='action', required=True)
    start = scope_sub.add_parser('begin')
    start.add_argument('--mode', choices=('app', 'board'), required=True)
    start.add_argument('--project')
    start.add_argument('--board')
    start.add_argument('--adopt', action='store_true', help='adopt edits only inside the selected app; retain the previous audit')
    scope_sub.add_parser('check')
    end = scope_sub.add_parser('end', help='archive the scope without modifying source files')
    end.add_argument('--archive', action='store_true')
    end.add_argument('--allow-violations', action='store_true', help='requires --archive; retain violations in the audit')
    board = commands.add_parser('board', help='adapt a board through the existing port API')
    board_sub = board.add_subparsers(dest='action', required=True)
    board_setup = board_sub.add_parser('setup', help='install board skills/MCP; does not install ESP-IDF')
    board_setup.add_argument('--platform', choices=('all', 'codex', 'claude', 'dsh', 'generic'), default='all')
    scaffold = board_sub.add_parser('init')
    scaffold.add_argument('board')
    scaffold.add_argument('--target', choices=('esp32s3', 'esp32s31', 'esp32p4'), required=True)
    scaffold.add_argument('--reference')
    for action in ('check', 'build'):
        board_sub.add_parser(action).add_argument('board')
    for name in ('build', 'run', 'clean', 'sdk', 'device'):
        command = commands.add_parser(name)
        command.add_argument('arguments', nargs=argparse.REMAINDER)
    verify = commands.add_parser('verify')
    verify.add_argument('project')
    verify.add_argument('--profiles', nargs='+')
    verify.add_argument('--scenario')
    verify.add_argument('--resource-package', help='preload the signed resource generation bound by this app')
    verify.add_argument('--window', action='store_true')
    verify.add_argument('--store-screenshots', action='store_true')
    verify.add_argument('--define', action='append', default=[], help='test-build definition NAME[=VALUE], repeatable')
    verify.add_argument('--require-ui', action='store_true', help='require visible content in the actual retained UI tree')
    verify.add_argument('--no-build', action='store_true', help='reuse validated build outputs; repeat their --define values')
    verify.add_argument('--jobs', type=int, default=2, help='concurrent screen profiles, 1..4')
    install = commands.add_parser('install', help='build for the connected CPU, install and start the app')
    install.add_argument('project')
    install.add_argument('--port', required=True)
    install.add_argument('--baud', type=int, default=2000000)
    install.add_argument('--strict-compatibility',action='store_true',help='block unknown package requirements before uploading')
    firmware = commands.add_parser('firmware')
    sub = firmware.add_subparsers(dest='action', required=True)
    from .firmware import BOARDS
    firmware_status = sub.add_parser('status',help='compare device INFO with signed release metadata without flashing')
    firmware_status.add_argument('--port',required=True)
    firmware_status.add_argument('--baud',type=int,default=2000000)
    firmware_status.add_argument('--board',choices=BOARDS)
    firmware_status.add_argument('--release',default='latest')
    firmware_status.add_argument('--manifest')
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
    for name in ('login', 'account', 'logout'):
        cmd = sub.add_parser(name)
        cmd.add_argument('--server', default='https://app.doit.am')
        if name == 'login':
            cmd.add_argument('--no-open', action='store_true')
            cmd.add_argument('--wait', action='store_true')
            cmd.add_argument('--renew', action='store_true', help='sign out this computer, then choose an account again')
        elif name == 'account':
            cmd.add_argument('--refresh', action='store_true', help='check and renew the remembered computer authorization')
    build_store = sub.add_parser('build', help='build a mandatory separate Wasm .pxa plus the requested store targets')
    build_store.add_argument('project')
    build_store.add_argument('--targets', nargs='+', required=True,
        choices=('esp32s3', 'esp32s31', 'esp32p4', 'simulator', 'windows-simulator', 'wasm'))
    for name in ('authorize', 'publish', 'publish-targets', 'prepare'):
        cmd = sub.add_parser(name)
        cmd.add_argument('project')
        if name in ('authorize', 'publish'):
            cmd.add_argument('package')
        elif name == 'publish-targets':
            cmd.add_argument('manifest', help='packages.json returned by store build')
        cmd.add_argument('--server', default='https://app.doit.am')
        if name == 'authorize':
            cmd.add_argument('--signing-key', type=Path)
            cmd.add_argument('--no-open', action='store_true')
            cmd.add_argument('--wait', action='store_true')
            cmd.add_argument('--renew', action='store_true', help='replace a revoked or unwanted cached authorization')
            cmd.add_argument('--app-only', action='store_true', help='request legacy one-app approval without remembering the account')
        else:
            if name == 'publish':
                cmd.add_argument('--reference')
                cmd.add_argument('--allow-universal', action='store_true', help='explicitly allow a multi-target container')
            cmd.add_argument('--icon')
            cmd.add_argument('--cover')
            cmd.add_argument('--skip-icon', action='store_true')
            cmd.add_argument('--skip-cover', action='store_true')
            cmd.add_argument('--screenshot', action='append', default=[])
            cmd.add_argument('--changelog', default='')
    return result


def main():
    args = parser().parse_args()
    try:
        result = None
        if args.command == 'workspace':
            from .migration import migrate
            result=migrate(args.workspace,version=args.sdk_version,sdk=args.sdk,
                           engine=args.engine,manifest=args.manifest)
        elif args.command == 'device-profile':
            from .device_profile import inspect
            result = inspect(Environment(args.workspace), args.port, args.baud, args.package)
        elif args.command == 'scope':
            from . import scope
            if args.action == 'begin':
                sdk = Environment(args.workspace).config.get('sdk') if args.mode == 'app' else None
                result = scope.begin(args.workspace, mode=args.mode, project=args.project, board=args.board, sdk=sdk, adopt=args.adopt)
            elif args.action == 'end':
                if args.allow_violations and not args.archive:
                    raise ToolError('--allow-violations requires --archive to preserve the audit')
                result = scope.finish(args.workspace, allow_violations=args.allow_violations)
            else:
                result = scope.check(args.workspace)
        elif args.command == 'board':
            from . import boards
            if args.action == 'setup':
                result = boards.setup(args.workspace, args.platform)
            elif args.action == 'init':
                result = boards.initialize(args.workspace, args.board, target=args.target, reference=args.reference)
            elif args.action == 'build':
                result = boards.build(args.workspace, args.board)
            else:
                result = boards.inspect(args.workspace, args.board)
        elif args.command == 'setup':
            from .scope import finish
            finish(args.workspace)
            from .integration import install
            env = configure(args.workspace, engine=args.engine, sdk=args.sdk, version=args.sdk_version, manifest=args.manifest, keep_archives=args.keep_archives)
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
        elif args.command == 'store' and args.action in ('login', 'account', 'logout'):
            from .store import login, account_status, logout, status
            if args.action == 'account':
                result = account_status(args.server, refresh=args.refresh)
            elif args.action == 'logout':
                result = logout(server=args.server)
            else:
                result = login(server=args.server, open_browser=not args.no_open, renew=args.renew)
                if args.wait and not result.get('authorized'):
                    print(json.dumps(result, ensure_ascii=False), flush=True)
                    while True:
                        checked = status(result['credential_ref'])
                        if checked.get('authorized') or checked.get('error') not in ('authorization_pending', 'slow_down'):
                            result = checked
                            break
                        time.sleep(checked.get('poll_after_seconds', 5))
        else:
            env = Environment(args.workspace)
            if args.command == 'init':
                from .projects import initialize
                result = initialize(env, args.project, args.id, args.language)
            elif args.command == 'recipes':
                from .recipes import catalog
                result = catalog()
            elif args.command == 'new':
                from .recipes import create
                result = create(env, args.project, args.recipe, app_id=args.id, title=args.title,
                                minutes=args.minutes, break_minutes=args.break_minutes)
            elif args.command == 'context':
                from .context import project_context
                result = project_context(env, args.project, args.query, args.language, args.limit)
            elif args.command == 'doctor':
                result = env.doctor()
                if args.usage:
                    from .sdk_cache import usage
                    result['storage'] = usage()
            elif args.command == 'mcp':
                from .mcp_server import serve
                serve(env)
                return 0
            elif args.command == 'device':
                print(env.device(args.arguments), end='')
                return 0
            elif args.command == 'sdk' and args.arguments[:1] in (['cache'], ['prune'], ['repair']):
                from .sdk_cache import command
                result = command(env, args.arguments)
            elif args.command in ('init', 'build', 'run', 'clean', 'sdk'):
                arguments = args.arguments
                if args.command in ('build', 'run') and '--online' not in arguments and '--offline' not in arguments:
                    arguments = [*arguments, '--offline']
                arguments = [arg for arg in arguments if arg != '--online']
                if args.command in ('init', 'build', 'run', 'clean'):
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
                                return 0 if state['returncode'] == 0 and not (state.get('scope') or {}).get('violations') else 1
                            time.sleep(.2)
                    finally:
                        jobs.close()
                print(env.call([args.command, *arguments], timeout=900), end='')
                from .scope import enforce
                enforce(env.workspace)
                return 0
            elif args.command == 'verify':
                from .workbench import verification
                result = verification(env, args.project, profiles=args.profiles, scenario=args.scenario,
                                      headless=not args.window, store_screenshots=args.store_screenshots,
                                      definitions=args.define, require_ui=args.require_ui,
                                      no_build=args.no_build, profile_jobs=args.jobs,
                                      resource_package=args.resource_package)
            elif args.command == 'install':
                from .workbench import install_app
                result = install_app(env, args.project, port=args.port, baud=args.baud,
                                     strict_compatibility=args.strict_compatibility)
            elif args.command == 'firmware':
                from .firmware import prepare, flash, status
                if args.action == 'status':
                    result = status(env,port=args.port,board=args.board,baud=args.baud,
                                    version=args.release,manifest=args.manifest)
                elif args.action == 'prepare':
                    result = prepare(args.board, version=args.release, mode=args.mode, manifest=args.manifest)
                else:
                    result = flash(env, args.board, port=args.port, baud=args.baud, version=args.release,
                                   mode=args.mode, erase_data=args.erase_data, manifest=args.manifest)
            elif args.command == 'store':
                from .store import authorize, status, publish, prepare
                if args.action == 'build':
                    from .store_packages import build
                    result = build(env, args.project, args.targets)
                elif args.action == 'publish-targets':
                    from .store_packages import publish as publish_targets
                    result = publish_targets(env, args.project, args.manifest, server=args.server,
                        icon=args.icon, cover=args.cover, screenshots=args.screenshot,
                        skip_icon=args.skip_icon, skip_cover=args.skip_cover, changelog=args.changelog)
                elif args.action == 'authorize':
                    result = authorize(env, args.project, args.package, server=args.server,
                        key=args.signing_key, open_browser=not args.no_open, renew=args.renew, remember=not args.app_only)
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
                elif args.action == 'prepare':
                    result = prepare(env, args.project, server=args.server, icon=args.icon, cover=args.cover,
                        screenshots=args.screenshot, skip_icon=args.skip_icon, skip_cover=args.skip_cover)
                else:
                    result = publish(env, args.project, args.package, server=args.server,
                        reference=args.reference, icon=args.icon, cover=args.cover, screenshots=args.screenshot,
                        skip_icon=args.skip_icon, skip_cover=args.skip_cover, changelog=args.changelog,
                        allow_universal=args.allow_universal)
        if result is not None:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        return 1 if result and any(result.get(key) is False for key in ('runtime_passed', 'passed', 'static_ready')) else 0
    except (ToolError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print('pxa-agent: ' + str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130

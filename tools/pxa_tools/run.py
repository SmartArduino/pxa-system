from __future__ import annotations

import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import tomllib

from .build import build, signing_key, tool
from .common import ToolError, command, digest, identity
from .project import Project, SDK_ROOT


def workspace_root():
    return SDK_ROOT.parents[1]


def pxadb_path():
    bundled = SDK_ROOT / 'tools/pxadb/pxadb.py'
    return bundled if bundled.is_file() else workspace_root() / 'tools/pxadb/pxadb.py'


def simulator_bin(name: str):
    if directory := os.environ.get('PXA_SIMULATOR_DIR'):
        custom = Path(directory) / name
        if not custom.is_file():
            raise ToolError(f'Simulator executable is missing: {custom}')
        return custom
    bundled = SDK_ROOT / 'simulator/bin' / name
    if bundled.is_file():
        return bundled
    native = workspace_root() / 'build/simulator/pai-touch' / name
    if not native.is_file():
        raise ToolError('Prebuilt simulator is missing; install the full DevKit')
    return native


def profile_options(name: str):
    if not name or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in name):
        raise ToolError('Invalid screen profile')
    path = SDK_ROOT / 'share/pxa/profiles' / (name + '.toml')
    if not path.is_file():
        path = workspace_root() / 'simulator/profiles' / (name + '.toml')
    profile = tomllib.loads(path.read_text())['desktop']
    result = ['--width', str(profile['width']), '--height', str(profile['height']),
              '--density-dpi', str(profile.get('density_dpi', 160))]
    for source, option in [('corner_radius', '--corner-radius'), ('locale', '--locale'),
                           ('shape_background', '--shape-background')]:
        if source in profile:
            result += [option, str(profile[source])]
    if 'safe_insets' in profile:
        result += ['--safe-insets', ','.join(map(str, profile['safe_insets']))]
    if profile.get('round', False):
        result += ['--round']
    if profile.get('gestures', False):
        result += ['--gestures']
    return result


def snapshot(project: Project):
    ignored = {'.git', '.pxa', 'dist', 'build', 'out', '__pycache__'}
    paths = [project.path]
    common = project.path.parent / 'common'
    if common.is_dir():
        paths.append(common)
    result = {}
    for root in paths:
        for directory, folders, files in os.walk(root):
            folders[:] = [f for f in folders if f not in ignored]
            for name in files:
                path = Path(directory) / name
                try:
                    stat = path.stat()
                    result[str(path)] = (stat.st_mtime_ns, stat.st_size)
                except FileNotFoundError:
                    pass
    for root in (SDK_ROOT / 'sdk/guest-cpp', SDK_ROOT / 'sdk/cmake'):
        for path in root.rglob('*'):
            if path.is_file():
                stat = path.stat()
                result[str(path)] = (stat.st_mtime_ns, stat.st_size)
    return result


def stop(process):
    if process is not None and process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def run(project: Project, args):
    process = None
    debug_service = None
    last_digest = None
    target = args.target
    if args.device:
        info = command([sys.executable, pxadb_path(), 'info', '--port', args.device,
                        '--baud', args.baud], capture=True)
        values = dict(v.split('=', 1) for v in info.split(';') if '=' in v)
        if 'pxa_core' in values and values['pxa_core'].split('.')[0] != '2':
            raise ToolError('This SDK requires a Core 2 Host; update the correct board application firmware first')
        target = {'esp32s3': 'esp32s3', 'esp32-s3': 'esp32s3',
                  'esp32s31': 'esp32s31', 'esp32-s31': 'esp32s31'}.get(values.get('target'))
        if target is None:
            raise ToolError('Device reports an unsupported execution target')
    previous = snapshot(project)
    try:
        while True:
            try:
                package = build(project, targets=target.split(','), output=args.output,
                                key=args.signing_key, offline=args.offline,
                                update_lock=args.update_lock, definitions=args.define,
                                aot_only=True, jobs=args.jobs,
                                build_type='Debug' if args.debug else 'Release')
                current_digest = digest(package)
                if current_digest != last_digest:
                    if args.device:
                        command([sys.executable, pxadb_path(), 'package', 'install', package,
                                 '--yes', '--port', args.device, '--baud', args.baud])
                        for attempt in range(10):
                            try:
                                command([sys.executable, pxadb_path(), 'package', 'run', project.app_id,
                                         '--port', args.device, '--baud', args.baud], capture=True)
                                break
                            except ToolError as error:
                                if 'package_launch_failed' not in str(error) or attempt == 9:
                                    raise
                                time.sleep(0.3)
                        for attempt in range(30):
                            inventory = command([sys.executable, pxadb_path(), 'package', 'list',
                                                 '--port', args.device, '--baud', args.baud], capture=True)
                            if any(f':{project.app_id}\t' in line and 'active=1' in line
                                   for line in inventory.splitlines()):
                                break
                            if attempt == 29:
                                raise ToolError('Device accepted the launch request but the app did not become active; check Host Core and service compatibility')
                            time.sleep(0.25)
                    else:
                        private = signing_key(SDK_ROOT, args.signing_key)
                        public = project.path / '.pxa/publisher.der'
                        command([tool(SDK_ROOT, 'openssl'), 'pkey', '-in', private,
                                 '-pubout', '-outform', 'DER', '-out', public])
                        state = project.path / '.pxa/simulator' / args.profile
                        state.mkdir(parents=True, exist_ok=True)
                        command([simulator_bin('pxsys_package_installer'), '--storage-root', state,
                                 '--publisher-key', public, '--source', package,
                                 '--expected-id', project.app_id])
                        # Building and verification succeeded before the running Guest is stopped.
                        stop(process)
                        cmd = [str(simulator_bin('pxsys_desktop_simulator')),
                               '--installed-packages-root', str(state / 'packages'),
                               '--state-root', str(state), '--publisher-key', str(public),
                               '--product-runner', str(simulator_bin('pxsys_product_simulator')),
                               '--launch', project.app_id, *profile_options(args.profile)]
                        if args.debug:
                            # Optional desktop debug bridge; ordinary runs have
                            # no extra process or polling service.
                            instance = ''.join(c if c.isalnum() else '-' for c in project.app_id[:24]) + '-' + identity(str(project.path))[:12]
                            selector = args.profile + '@' + instance
                            sockets = Path(os.environ.get('PXA_SIMULATOR_SOCKET_ROOT', f'/tmp/pxa-simulator-{os.getuid()}'))
                            sockets.mkdir(mode=0o700, parents=True, exist_ok=True)
                            control = sockets / (selector + '.control.sock')
                            cmd += ['--pxadb-control-socket', str(control)]
                            if debug_service is None:
                                bridge = SDK_ROOT / 'tools/simulator_pxadb.py'
                                if not bridge.is_file():
                                    bridge = workspace_root() / 'tools/simulator_pxadb.py'
                                debug_service = subprocess.Popen([sys.executable, str(bridge),
                                    '--state-root', str(state), '--socket', str(sockets / (selector + '.sock')),
                                    '--control-socket', str(control), '--installer', str(simulator_bin('pxsys_package_installer')),
                                    '--publisher-key', str(public)], start_new_session=True)
                                print(f'PXADB: pxa device info --simulator {selector}', flush=True)
                        if args.duration_ms is not None:
                            cmd += ['--duration-ms', str(args.duration_ms)]
                        if args.screenshot:
                            cmd += ['--screenshot', str(args.screenshot.resolve())]
                        env = os.environ.copy()
                        font = SDK_ROOT / 'share/pxa/fonts/noto_sans_cjk_common.ttf'
                        if font.is_file():
                            env['PXA_SIMULATOR_FONT'] = str(font)
                        process = subprocess.Popen(cmd, env=env, start_new_session=True)
                    last_digest = current_digest
            except ToolError as error:
                if not args.watch:
                    raise
                print(f'Update failed; previous application retained: {error}', file=sys.stderr)
            if not args.watch:
                if process is not None and process.wait() != 0:
                    raise ToolError('Simulator failed')
                return
            previous = snapshot(project)
            while snapshot(project) == previous:
                if process is not None and process.poll() is not None:
                    return
                time.sleep(0.35)
            time.sleep(0.30)
    except KeyboardInterrupt:
        pass
    finally:
        stop(process)
        stop(debug_service)

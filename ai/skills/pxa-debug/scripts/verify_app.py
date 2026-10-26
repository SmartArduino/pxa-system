#!/usr/bin/env python3
"""Run real PXA Guests and collect bounded, reproducible debug evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from pxa_agent.simulator_input import inject as simulator_input

PROFILES = ('pai-touch', 'sensecap-watcher', 'esp-mosaico', 'esp32s31-korvo-1', 'generic')
NAME = re.compile(r'[a-z][a-z0-9-]{0,47}')


def device_command(pxa, arguments, env):
    """Use the SDK's own PXADB entrypoint without importing its build CLI."""
    root = pxa.parent.parent
    python = root / 'runtime/python/bin/python3'
    script = root / 'tools/pxadb/pxadb.py'
    if arguments[:1] == ['device'] and python.is_file() and script.is_file():
        variables = dict(env, PYTHONHOME=str(root / 'runtime/python'))
        variables['PYTHONPATH'] = os.pathsep.join(filter(None, [
            str(root / 'tools'), str(root / 'runtime/python/lib/python3.12/site-packages'),
            env.get('PYTHONPATH', '')]))
        return [str(python), str(script), *map(str, arguments[1:])], variables
    return [str(pxa), *map(str, arguments)], env


def validate_scenario(scenario, dimensions):
    if not isinstance(scenario, dict) or set(scenario) - set(PROFILES):
        raise ValueError('Scenario must be an object keyed by built-in profile names')
    for profile, steps in scenario.items():
        if not isinstance(steps, list) or len(steps) > 100:
            raise ValueError('Each profile accepts at most 100 steps')
        names = {'before'}
        for step in steps:
            if not isinstance(step, dict) or not isinstance(step.get('name'), str) or not NAME.fullmatch(step['name']) or step['name'] in names:
                raise ValueError('Step names must be unique safe filenames')
            names.add(step['name'])
            action = step.get('action')
            fields = {'tap': {'x', 'y'}, 'swipe': {'x', 'y', 'x2', 'y2', 'ms'},
                      'controller': {'buttons', 'ms'}, 'cancel': set(),
                      'wait': {'ms'}, 'screenshot': set()}.get(action)
            if fields is None or set(step) != fields | {'name', 'action'}:
                raise ValueError('Invalid scenario action/fields: ' + str(action))
            if 'ms' in fields and (type(step['ms']) is not int or not (1 if action == 'swipe' else 0) <= step['ms'] <= 5000):
                raise ValueError('Step duration must be a bounded integer')
            if action == 'controller' and (type(step['buttons']) is not int or not 0 <= step['buttons'] <= 4095
                                           or not 1 <= step['ms'] <= 3000):
                raise ValueError('Controller requires buttons 0..4095 and hold 1..3000 ms')
            width, height = dimensions[profile]
            for axis in fields & {'x', 'y', 'x2', 'y2'}:
                if type(step[axis]) is not int or not 0 <= step[axis] < (width if axis.startswith('x') else height):
                    raise ValueError('Input coordinate is outside ' + profile)


def stop(process):
    if process.poll() is not None:
        return
    for sig, timeout in ((signal.SIGINT, 12), (signal.SIGTERM, 5), (signal.SIGKILL, 5)):
        try:
            os.killpg(process.pid, sig)
            process.wait(timeout=timeout)
            return
        except ProcessLookupError:
            return
        except subprocess.TimeoutExpired:
            continue
    raise RuntimeError('Owned CLI did not stop')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', type=Path)
    parser.add_argument('--pxa', type=Path, default=shutil.which('pxa'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profiles', nargs='+', choices=PROFILES, default=list(PROFILES))
    parser.add_argument('--scenario', type=Path)
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--store-screenshots', action='store_true',
                        help='Hide simulator system navigation before capturing original store screenshots')
    parser.add_argument('--online', action='store_true')
    parser.add_argument('--timeout', type=int, default=120, help='bounded startup time per profile (10..600 seconds)')
    args = parser.parse_args()
    result = {'schema': 'pxa-debug-evidence-1', 'runtime_passed': False,
              'visual_review_required': True, 'profiles': [], 'physical_device_tested': False}
    output = None
    sockets = None
    try:
        if args.pxa is None:
            raise ValueError('pxa not found; pass --pxa /path/to/devkit/bin/pxa')
        if not 10 <= args.timeout <= 600 or len(set(args.profiles)) != len(args.profiles):
            raise ValueError('Invalid timeout or duplicate profile')
        project = args.project.resolve(strict=True)
        pxa = args.pxa.resolve(strict=True)
        planned = args.output.resolve()
        if planned.is_relative_to(project) or project.is_relative_to(planned):
            raise ValueError('Evidence must be outside the project and use a new directory')
        metadata = json.loads((project / 'package.json').read_text())
        app_id = metadata['id']
        root = pxa.parent.parent
        profiles = root / 'share/pxa/profiles'
        if not profiles.is_dir():
            profiles = root.parents[1] / 'simulator/profiles'
        dimensions = {name: tuple(tomllib.loads((profiles / (name + '.toml')).read_text())['desktop'][axis]
                                   for axis in ('width', 'height')) for name in PROFILES}
        scenario = json.loads(args.scenario.read_text()) if args.scenario else {}
        validate_scenario(scenario, dimensions)
        planned.mkdir(parents=True, exist_ok=False)
        output = planned
        env = dict(os.environ, PYTHONUNBUFFERED='1', PYTHONDONTWRITEBYTECODE='1')
        # Short private paths also stay below AF_UNIX's path-length limit.
        # Old DevKits leave UI control sockets behind; this owned directory
        # isolates debug endpoints and removes those leftovers on completion.
        sockets = tempfile.TemporaryDirectory(prefix='pxa-debug-')
        env['PXA_SIMULATOR_SOCKET_ROOT'] = sockets.name
        if args.headless:
            env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        if args.store_screenshots:
            env['PXA_SIM_NAV_MODE'] = 'gesture-no-bar'
        result.update(project=str(project), app_id=app_id, headless=args.headless, ipc_isolated=True)
        result['capture'] = {
            'purpose': 'store' if args.store_screenshots else 'debug',
            'source': 'real-pxa-simulator', 'post_processed': False,
            'navigation_mode': env.get('PXA_SIM_NAV_MODE', 'default'),
            'visual_review': 'Inspect every original PNG: no performance overlay, gesture bar or three-button navigation.',
        }

        def command(directory, name, arguments, *, required=True, timeout=15):
            invocation, variables = device_command(pxa, arguments, env)
            completed = subprocess.run(invocation, env=variables, capture_output=True,
                                       text=True, timeout=timeout)
            text = completed.stdout + completed.stderr
            (directory / (name + '.log')).write_text(text)
            if required and completed.returncode:
                raise RuntimeError(name + ' failed; see ' + str(directory / (name + '.log')))
            return completed.returncode, text

        _, result['cli_version'] = command(output, 'version', ['--version'])
        for profile in args.profiles:
            directory = output / profile
            directory.mkdir()
            record = {'name': profile, 'runtime_passed': False}
            result['profiles'].append(record)
            log_path = directory / 'run.log'
            arguments = [str(pxa), 'run', str(project), '--debug', '--profile', profile,
                         '--duration-ms', str(args.timeout * 1000)]
            if not args.online:
                arguments.append('--offline')
            started = time.monotonic()
            with log_path.open('w') as log:
                process = subprocess.Popen(arguments, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    deadline = time.monotonic() + args.timeout
                    selector = None
                    while time.monotonic() < deadline:
                        matches = re.findall(r'PXADB: pxa device info --simulator (\S+)', log_path.read_text())
                        if matches:
                            selector = matches[-1]
                            code, _ = command(directory, 'before-capture', ['device', 'screenshot', directory / 'before.png',
                                                                           '--simulator', selector], required=False)
                            if code == 0:
                                break
                        if process.poll() is not None:
                            raise RuntimeError('Guest run exited before screenshot; see ' + str(log_path))
                        time.sleep(0.2)
                    else:
                        raise RuntimeError('Guest readiness timed out; see ' + str(log_path))
                    record['selector'] = selector
                    _, info = command(directory, 'info', ['device', 'info', '--simulator', selector])
                    record['info'] = info.strip()
                    _, packages = command(directory, 'packages', ['device', 'package', 'list', '--simulator', selector])
                    if not any(line.split('\t', 1)[0].rsplit(':', 1)[-1] == app_id and 'active=1' in line
                               for line in packages.splitlines()):
                        raise RuntimeError('Requested Guest is not active; see ' + str(directory / 'packages.log'))
                    code, memory = command(directory, 'memory', ['device', 'memory', '--simulator', selector], required=False)
                    if code == 0:
                        record['memory'] = {'available': True, 'snapshot': json.loads(memory)}
                    elif 'unsupported' in memory.lower():
                        record['memory'] = {'available': False, 'reason': memory.strip()}
                    else:
                        raise RuntimeError('Memory transport/query failed; see ' + str(directory / 'memory.log'))
                    for step in scenario.get(profile, []):
                        action = step['action']
                        if action == 'wait':
                            time.sleep(step['ms'] / 1000)
                        elif action == 'screenshot':
                            command(directory, step['name'], ['device', 'screenshot', directory / (step['name'] + '.png'),
                                                              '--simulator', selector])
                        elif action == 'controller':
                            message = simulator_input(selector, step['buttons'], step['ms'], socket_root=sockets.name)
                            (directory / (step['name'] + '.log')).write_text(message + '\n')
                        elif action == 'cancel':
                            message = simulator_input(selector, socket_root=sockets.name)
                            (directory / (step['name'] + '.log')).write_text(message + '\n')
                        else:
                            values = [step['x'], step['y']]
                            if action == 'swipe':
                                values += [step['x2'], step['y2'], '--duration-ms', step['ms']]
                            command(directory, step['name'], ['device', 'input', action, *values, '--simulator', selector])
                    record['runtime_passed'] = True
                finally:
                    stop(process)
            record['verification_wall_ms'] = round((time.monotonic() - started) * 1000)
            print('PASS: ' + profile + '; inspect screenshots', flush=True)
        result['runtime_passed'] = True
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        result['error'] = str(error)
        print('pxa-debug: ' + str(error), file=sys.stderr)
        return 1
    finally:
        if sockets is not None:
            sockets.cleanup()
        if output is not None:
            result['screenshots'] = [
                {'path': path.relative_to(output).as_posix(),
                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'bytes': path.stat().st_size}
                for path in sorted(output.rglob('*.png'))]
            (output / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    sys.exit(main())

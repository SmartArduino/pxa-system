#!/usr/bin/env python3
"""Run real PXA Guests and collect bounded, reproducible debug evidence."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import contextmanager
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
import threading
import time
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from pxa_agent.simulator_input import inject as simulator_input
from pxa_agent.ui import query_arguments

PROFILES = ('pai-touch', 'sensecap-watcher', 'esp-mosaico', 'esp32s31-korvo-1', 'generic')
ALL_PROFILES = (*PROFILES, 'doit-gamekit', 'doit-gamekit-1', 'doit-esp32s31-devkit', 'espp4-a')
NAME = re.compile(r'[a-z][a-z0-9-]{0,47}')


def ui_diagnostics(text):
    return [{'phase': phase, 'code': int(code)} for phase, code in
            re.findall(r'PXA_UI_ERROR phase=(mount|flush) code=(-?\d+)', text)]


def ui_health(tree):
    """Structural content check from an actual Host tree; screenshots still need review."""
    if tree.get('schema') != 'pxa-ui-inspection-1' or tree.get('complete') is not True:
        raise ValueError('A complete retained UI inspection is required')
    content = [node for node in tree['nodes'] if node['visible'] and
               node['clip']['width'] > 0 and node['clip']['height'] > 0 and
               (node['type'] in {'image', 'button', 'toggle', 'slider', 'text-input', 'select',
                                'progress', 'canvas', 'media-surface'} or
                node['type'] == 'text' and node['text'])]
    return {'kind': 'visible-retained-content', 'passed': bool(content), 'heuristic': False,
            'semantic_assertion': False, 'visible_content_nodes': [
                {'surface': n['surface'], 'id': n['id'], 'type': n['type']} for n in content]}


@contextmanager
def recorded_step(record, step):
    started = time.monotonic()
    item = {'name': step['name'], 'action': step['action'], 'passed': False}
    record['steps'].append(item)
    try:
        yield item
        item['passed'] = True
    except Exception as error:
        item['error'] = str(error)
        raise
    finally:
        item['elapsed_ms'] = round((time.monotonic()-started)*1000, 2)


def device_command(pxa, arguments, env):
    """Invoke pinned SDK Python directly, including on Windows without a shell."""
    root = pxa.parent.parent
    if arguments[:1] == ['device']:
        from pxa_agent.host_tools import client
        variables = dict(env)
        variables.pop('PYTHONHOME',None)
        return [sys.executable,str(client(sdk=root)),*map(str,arguments[1:])],variables
    python = root / ('runtime/python/python.exe' if os.name == 'nt' else 'runtime/python/bin/python3')
    variables = dict(env)
    variables.pop('PYTHONHOME', None)
    if python.is_file() and os.name != 'nt':
        variables['PYTHONHOME'] = str(root / 'runtime/python')
    variables['PYTHONPATH'] = os.pathsep.join(filter(None, [
        str(root / 'tools'), str(root / 'runtime/python/lib/python3.12/site-packages'), env.get('PYTHONPATH', '')]))
    variables['PATH'] = str(root / 'runtime/bin') + os.pathsep + env.get('PATH', '')
    executable = str(python) if python.is_file() else sys.executable
    return [executable, '-m', 'pxa_tools', *map(str, arguments)], variables


def validate_scenario(scenario, dimensions):
    if not isinstance(scenario, dict) or set(scenario) - set(ALL_PROFILES):
        raise ValueError('Scenario must be an object keyed by built-in profile names')
    for profile, steps in scenario.items():
        if not isinstance(steps, list) or len(steps) > 100:
            raise ValueError('Each profile accepts at most 100 steps')
        names = {'before', 'before-capture', 'info', 'packages', 'memory', 'ui-tree', 'version', 'build'}
        for step in steps:
            if not isinstance(step, dict) or not isinstance(step.get('name'), str) or not NAME.fullmatch(step['name']) or step['name'] in names:
                raise ValueError('Step names must be unique safe filenames')
            names.add(step['name'])
            action = step.get('action')
            if not isinstance(action, str):
                raise ValueError('Scenario action must be a string')
            if action in ('expect', 'wait-for', 'tap-node'):
                fields = {'selector'} | ({'expected'} if action != 'tap-node' else set()) | (
                    {'timeout_ms'} if action == 'wait-for' else set())
                if set(step) != fields | {'name', 'action'}:
                    raise ValueError('Invalid semantic UI step fields')
                query_arguments('tap' if action == 'tap-node' else action, step['selector'],
                                step.get('expected'), step.get('timeout_ms', 0))
                continue
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
    if os.name == 'nt':
        process.send_signal(signal.CTRL_BREAK_EVENT)
        try:
            process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            process.wait(timeout=5)
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


def contact_sheet(output, records):
    """A review preview only; store uploaders receive original capture paths."""
    import math
    from PIL import Image, ImageDraw, ImageOps
    images = []
    for record in records:
        paths = [output / item['path'] for item in record.get('screenshots', [])]
        if paths:
            images.append((record, paths[-1]))
    if not images:
        return None
    columns = min(3, math.ceil(math.sqrt(len(images))))
    rows = math.ceil(len(images) / columns)
    sheet = Image.new('RGB', (columns * 300, 55 + rows * 420), '#202228')
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 9), 'Review preview - originals unchanged', fill='white')
    draw.text((12, 28), 'Not for store upload', fill='#bbbbbb')
    for index, (record, path) in enumerate(images):
        x, y = index % columns * 300, 55 + index // columns * 420
        with Image.open(path) as original:
            preview = ImageOps.contain(original.convert('RGB'), (280, 365), Image.Resampling.LANCZOS)
            sheet.paste(preview, (x + (300-preview.width)//2, y + 40))
        label = record['name'] + (' PASS' if record['runtime_passed'] else ' FAIL')
        draw.text((x+10, y+4), label, fill='#a7efb0' if record['runtime_passed'] else '#ff9f9f')
        requested = record['requested_geometry']
        draw.text((x+10, y+20), f"{requested['width']}x{requested['height']}  {requested.get('density_dpi',160)} DPI", fill='#bbbbbb')
    path = output / 'contact-sheet.png'
    sheet.save(path)
    return {'path': path.name, 'purpose': 'review-preview', 'store_eligible': False,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', type=Path)
    parser.add_argument('--pxa', type=Path, default=shutil.which('pxa'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profiles', nargs='+', choices=ALL_PROFILES, default=list(PROFILES))
    parser.add_argument('--scenario', type=Path)
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--store-screenshots', action='store_true', help='Use a rectangular viewport with navigation/performance overlays hidden')
    parser.add_argument('--online', action='store_true')
    parser.add_argument('--define', action='append', default=[])
    parser.add_argument('--require-ui', action='store_true', help='Require visible retained content; omit for Surface-only apps')
    parser.add_argument('--no-build', action='store_true', help='Validate/reuse an existing build; repeat its --define values')
    parser.add_argument('--jobs', type=int, default=2, help='Concurrent profiles, 1..4 (default 2)')
    parser.add_argument('--timeout', type=int, default=120, help='Startup timeout per profile, 10..600 seconds')
    args = parser.parse_args()
    result = {'schema': 'pxa-debug-evidence-1', 'runtime_passed': False,
              'visual_review_required': True, 'profiles': [], 'physical_device_tested': False}
    output = None
    started = time.monotonic()
    cancel = threading.Event()
    processes, process_lock = {}, threading.Lock()
    try:
        if args.pxa is None:
            raise ValueError('pxa not found; pass --pxa /path/to/devkit/bin/pxa')
        if not 10 <= args.timeout <= 600 or not 1 <= args.jobs <= 4 or len(set(args.profiles)) != len(args.profiles):
            raise ValueError('Invalid timeout, jobs or duplicate profile')
        if any(not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(=[^;\n\r]+)?', d) for d in args.define):
            raise ValueError('Invalid definition; use NAME[=VALUE]')
        project, pxa = args.project.resolve(strict=True), args.pxa.resolve(strict=True)
        from pxa_agent.context import locked_sdk
        selected_root = locked_sdk(pxa.parent.parent,project)
        pxa = selected_root/('bin/pxa.cmd' if os.name == 'nt' else 'bin/pxa')
        planned = args.output.resolve()
        if planned.is_relative_to(project) or project.is_relative_to(planned):
            raise ValueError('Evidence must be outside the project and use a new directory')
        metadata = json.loads((project / 'package.json').read_text())
        app_id = metadata['id']
        root = pxa.parent.parent
        profile_root = root / 'share/pxa/profiles'
        if not profile_root.is_dir():
            profile_root = root.parents[1] / 'simulator/profiles'
        scenario = json.loads(args.scenario.read_text()) if args.scenario else {}
        if not isinstance(scenario, dict) or set(scenario) - set(ALL_PROFILES):
            raise ValueError('Unknown scenario profile')
        profiles = {name: tomllib.loads((profile_root / (name + '.toml')).read_text())['desktop']
                    for name in set(args.profiles) | set(scenario)}
        dimensions = {name: (profile['width'], profile['height']) for name, profile in profiles.items()}
        validate_scenario(scenario, dimensions)
        planned.mkdir(parents=True, exist_ok=False)
        output = planned
        env = dict(os.environ, PYTHONUNBUFFERED='1', PYTHONDONTWRITEBYTECODE='1', PYTHONUTF8='1')
        if args.headless:
            env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        if args.store_screenshots:
            env['PXA_SIM_NAV_MODE'] = 'gesture-no-bar'
            env.pop('PXA_SIMULATOR_PERF', None)
        result.update(project=str(project), app_id=app_id, headless=args.headless, ipc_isolated=True,
                      state_isolated=True, definitions=args.define, require_ui=args.require_ui, jobs=args.jobs)
        result['capture'] = {
            'purpose': 'store' if args.store_screenshots else 'debug', 'source': 'real-pxa-simulator',
            'post_processed': False, 'navigation_mode': env.get('PXA_SIM_NAV_MODE', 'default'),
            'display_shape': 'rectangle' if args.store_screenshots else 'profile',
            'visual_review': 'Inspect every original capture for layout, clipping, performance overlays and system navigation. Contact sheets are review previews only.',
        }

        def command(directory, name, arguments, *, variables=env, required=True, timeout=15):
            invocation, child_env = device_command(pxa, arguments, variables)
            completed = subprocess.run(invocation, env=child_env, capture_output=True, text=True, timeout=timeout)
            text = completed.stdout + completed.stderr
            (directory / (name + '.log')).write_text(text, encoding='utf-8')
            if required and completed.returncode:
                raise RuntimeError(name + ' failed: ' + text[-4000:] + '; see ' + str(directory / (name + '.log')))
            return completed.returncode, text

        _, result['cli_version'] = command(output, 'version', ['--version'])
        _, run_help = command(output,'run-help',['run','--help'])
        checked_reuse = all(flag in run_help for flag in
                            ('--no-build','--simulator-state','--expected-package-sha256'))
        result['checked_reuse_supported'] = checked_reuse
        if args.no_build and not checked_reuse:
            raise ValueError('The locked SDK lacks checked --no-build support. Run verify without --no-build for a fresh build, or explicitly migrate the SDK; the lock was retained.')
        build_started = time.monotonic()
        if args.no_build:
            arguments = ['build', str(project), '--check-existing', '--target', 'simulator', '--offline']
            for definition in args.define:
                arguments += ['--define', definition]
            command(output, 'reuse-check', arguments, timeout=120)
        else:
            arguments = ['build', str(project), '--target', 'simulator', '--debug', '--aot-only']
            if not args.online:
                arguments.append('--offline')
            for definition in args.define:
                arguments += ['--define', definition]
            command(output, 'build', arguments, timeout=600)
        receipt_path = project / 'dist' / (app_id + '.pxa.build.json')
        receipt = json.loads(receipt_path.read_text())
        if receipt.get('schema') != ('pxa-build-2' if checked_reuse else 'pxa-build-1'):
            raise ValueError('The locked SDK did not produce the expected build receipt; inspect build.log and SDK capabilities')
        prepared_package = project/'dist'/(app_id+'.pxa')
        with prepared_package.open('rb') as stream:
            package_hash = hashlib.file_digest(stream,'sha256').hexdigest()
        if package_hash != receipt['sha256'] or receipt['configuration']['definitions'] != args.define:
            raise ValueError('Prepared package hash/definitions differ from this verification build')
        if ('windows-simulator' if os.name == 'nt' else 'simulator') not in receipt['targets']:
            raise ValueError('Prepared package lacks the native simulator target')
        if not checked_reuse:
            # Older immutable SDKs have no source inventory / checked reuse.
            # A fresh build is still tested once across isolated native Hosts;
            # keep exactly its bytes, without changing the SDK or app's saves.
            prepared_package = output/'prepared.pxa'
            shutil.copyfile(project/'dist'/(app_id+'.pxa'),prepared_package)
            with prepared_package.open('rb') as stream:
                if hashlib.file_digest(stream,'sha256').hexdigest() != package_hash:
                    raise ValueError('Package changed while freezing the verification build')
        (output / 'build-receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        result['build'] = {'reused': args.no_build, 'sha256': receipt['sha256'], 'targets': receipt['targets'],
                           'input_inventory_available':checked_reuse,
                           'type': receipt['configuration']['type'], 'definitions': receipt['configuration']['definitions'],
                           'receipt': 'build-receipt.json', 'elapsed_ms': round((time.monotonic()-build_started)*1000, 2)}

        def run_profile(profile):
            directory = output / profile
            directory.mkdir()
            requested = dict(profiles[profile])
            if args.store_screenshots:
                requested.update(corner_radius=0, round=False, safe_insets=[0,0,0,0], gestures=False)
            record = {'name': profile, 'runtime_passed': False, 'steps': [], 'ui_errors': [],
                      'requested_geometry': requested, 'screenshots': [], 'package_sha256': receipt['sha256']}
            log_path = directory / 'run.log'
            runtime_started = time.monotonic()
            process = None
            sockets = tempfile.TemporaryDirectory(prefix='pxa-debug-')
            variables = dict(env, PXA_SIMULATOR_SOCKET_ROOT=sockets.name)
            try:
                if cancel.is_set():
                    raise RuntimeError('Verification cancelled')
                # One immutable package, separate state and sockets for every
                # worker. Validation is read-only; no profile rebuilds it.
                budget_ms = sum(step.get('ms', step.get('timeout_ms', 0)) for step in scenario.get(profile, []))
                duration = str(args.timeout * 1000 + budget_ms + 30000)
                if checked_reuse:
                    arguments = ['run', str(project), '--no-build', '--debug', '--profile', profile,
                                 '--expected-package-sha256', receipt['sha256'],
                                 '--simulator-state', str(directory / 'state'), '--duration-ms', duration]
                    if args.store_screenshots:
                        arguments.append('--store-screenshots')
                    for definition in args.define:
                        arguments += ['--define', definition]
                    invocation, child_env = device_command(pxa, arguments, variables)
                else:
                    invocation = [sys.executable,str(Path(__file__).resolve().parents[3]/'pxa_agent/prepared_run.py'),
                        '--sdk',str(root),'--package',str(prepared_package),'--sha256',receipt['sha256'],
                        '--app-id',app_id,'--profile',profile,'--state',str(directory/'state'),
                        '--duration-ms',duration,*(['--store-screenshots'] if args.store_screenshots else [])]
                    child_env=dict(variables)
                    child_env.pop('PYTHONHOME',None)
                    child_env['PATH']=str(root/'runtime/bin')+os.pathsep+variables.get('PATH','')
                with log_path.open('w', encoding='utf-8') as log:
                    options = {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {'start_new_session': True}
                    process = subprocess.Popen(invocation, env=child_env, stdout=log, stderr=subprocess.STDOUT, **options)
                    with process_lock:
                        processes[profile] = process
                    deadline = time.monotonic() + args.timeout
                    selector = None
                    while time.monotonic() < deadline:
                        if cancel.is_set():
                            raise RuntimeError('Verification cancelled')
                        matches = re.findall(r'PXADB: pxa device info --simulator (\S+)', log_path.read_text(encoding='utf-8'))
                        if matches:
                            selector = matches[-1]
                            code, _ = command(directory, 'before-capture', ['device', 'screenshot', directory / 'before.png',
                                                '--simulator', selector], variables=variables, required=False)
                            if code == 0:
                                break
                        if process.poll() is not None:
                            raise RuntimeError('Guest exited before readiness: ' + log_path.read_text(encoding='utf-8')[-4000:])
                        cancel.wait(.2)
                    else:
                        raise RuntimeError('Guest readiness timed out; see ' + str(log_path))
                    record['selector'] = selector
                    _, info = command(directory, 'info', ['device', 'info', '--simulator', selector], variables=variables)
                    record['info'] = info.strip()
                    _, packages = command(directory, 'packages', ['device', 'package', 'list', '--simulator', selector], variables=variables)
                    if not any(line.split('\t',1)[0].rsplit(':',1)[-1] == app_id and 'active=1' in line for line in packages.splitlines()):
                        raise RuntimeError('Requested Guest is not active; see packages.log')
                    code, memory = command(directory, 'memory', ['device', 'memory', '--simulator', selector], variables=variables, required=False)
                    if code == 0:
                        record['memory'] = {'available': True, 'snapshot': json.loads(memory)}
                    elif 'unsupported' in memory.lower():
                        record['memory'] = {'available': False, 'reason': memory.strip()}
                    else:
                        raise RuntimeError('Memory transport/query failed; see memory.log')
                    for step in scenario.get(profile, []):
                        if cancel.is_set():
                            raise RuntimeError('Verification cancelled')
                        with recorded_step(record, step) as step_record:
                            action = step['action']
                            if action == 'wait':
                                if cancel.wait(step['ms']/1000):
                                    raise RuntimeError('Verification cancelled')
                            elif action == 'screenshot':
                                command(directory, step['name'], ['device', 'screenshot', directory / (step['name']+'.png'),
                                                                 '--simulator', selector], variables=variables)
                            elif action in ('controller', 'cancel'):
                                message = simulator_input(selector, step.get('buttons'), step.get('ms',100), socket_root=sockets.name)
                                (directory / (step['name']+'.log')).write_text(message+'\n', encoding='utf-8')
                            elif action in ('expect', 'wait-for', 'tap-node'):
                                values = query_arguments('tap' if action == 'tap-node' else action,
                                                         step['selector'], step.get('expected'), step.get('timeout_ms',0))
                                _, actual = command(directory, step['name'], ['device', *values, '--simulator', selector],
                                                    variables=variables, timeout=max(15, step.get('timeout_ms',0)/1000+5))
                                step_record['result'] = json.loads(actual)
                            else:
                                values = [step['x'], step['y']]
                                if action == 'swipe':
                                    values += [step['x2'],step['y2'],'--duration-ms',step['ms']]
                                command(directory, step['name'], ['device','input',action,*values,'--simulator',selector], variables=variables)
                    code, actual = command(directory, 'ui-tree', ['device','ui','inspect','--simulator',selector],
                                           variables=variables, required=args.require_ui)
                    if code == 0:
                        tree = json.loads(actual)
                        (directory/'ui-tree.json').write_text(json.dumps(tree,ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
                        record['actual_geometry'] = {'viewport':tree['viewport'], 'environment':tree.get('environment')}
                        if args.require_ui:
                            record['ui_health'] = ui_health(tree)
                            if not record['ui_health']['passed']:
                                raise RuntimeError('No visible retained UI content. Inspect ui-tree.json; Surface-only apps omit --require-ui.')
                    else:
                        record['actual_geometry'] = {'available':False,'reason':actual[-1000:]}
                    if process.poll() is not None:
                        raise RuntimeError('Guest exited during verification')
                record['runtime_passed'] = True
            except Exception as error:
                record['error'] = str(error)
            finally:
                if process is not None:
                    try:
                        stop(process)
                    except Exception as error:
                        record.update(runtime_passed=False, error='Owned process cleanup failed: '+str(error))
                with process_lock:
                    processes.pop(profile, None)
                sockets.cleanup()
                text = log_path.read_text(encoding='utf-8',errors='replace') if log_path.exists() else ''
                record['logs'], record['ui_errors'] = text.splitlines()[-256:], ui_diagnostics(text)
                if record['ui_errors']:
                    record.update(runtime_passed=False, error='Guest UI transaction failed; see run.log')
                from PIL import Image
                capture_names = ['before', *[step['name'] for step in scenario.get(profile, []) if step['action'] == 'screenshot']]
                for name in capture_names:
                    path = directory / (name + '.png')
                    if not path.is_file():
                        continue
                    try:
                        with Image.open(path) as frame:
                            frame.load()
                            width,height = frame.size
                    except OSError as error:
                        record.update(runtime_passed=False,error='Invalid capture '+path.name+': '+str(error))
                        continue
                    if (width,height) != (requested['width'],requested['height']):
                        record.update(runtime_passed=False,error='Capture size differs from requested profile')
                    record['screenshots'].append({'path':path.relative_to(output).as_posix(),
                        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size,
                        'width':width,'height':height,'post_processed':False})
                record['verification_wall_ms'] = round((time.monotonic()-runtime_started)*1000,2)
            return record

        completed = {}
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {executor.submit(run_profile, profile): profile for profile in args.profiles}
            try:
                for future in as_completed(futures):
                    profile = futures[future]
                    completed[profile] = future.result()
                    print(('PASS: ' if completed[profile]['runtime_passed'] else 'FAIL: ')+profile+'; inspect original evidence',flush=True)
            except BaseException:
                cancel.set()
                with process_lock:
                    active = list(processes.values())
                for process in active:
                    stop(process)
                raise
            finally:
                result['profiles'] = [completed[p] for p in args.profiles if p in completed]
        result['runtime_passed'] = all(p['runtime_passed'] for p in result['profiles']) and len(result['profiles']) == len(args.profiles)
        failed = [p['name'] for p in result['profiles'] if not p['runtime_passed']]
        if failed:
            result['error'] = 'Verification failed for: '+', '.join(failed)
        result['preview'] = contact_sheet(output,result['profiles'])
        return 0 if result['runtime_passed'] else 1
    except KeyboardInterrupt:
        result.update(runtime_passed=False,cancelled=True,error='Verification cancelled')
        return 130
    except (OSError,ValueError,KeyError,RuntimeError,subprocess.TimeoutExpired) as error:
        result.update(runtime_passed=False,error=str(error))
        print('pxa-debug: '+str(error),file=sys.stderr)
        return 1
    finally:
        if output is not None:
            result['elapsed_ms'] = round((time.monotonic()-started)*1000,2)
            result['screenshots'] = [item for profile in result['profiles'] for item in profile.get('screenshots',[])]
            (output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    raise SystemExit(main())

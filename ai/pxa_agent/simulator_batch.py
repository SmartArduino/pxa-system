"""Run bounded actions on one owned simulator using the pinned SDK transport."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import time
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pxa_agent.simulator_input import inject

NAME = re.compile(r'[a-z][a-z0-9-]{0,47}')


def validate(steps, width, height):
    if not isinstance(steps, list) or not 1 <= len(steps) <= 32:
        raise ValueError('Provide 1..32 ordered steps')
    names, duration, images = set(), 0, 0
    for step in steps:
        if not isinstance(step, dict):
            raise ValueError('Each step must be an object')
        name, action = step.get('name'), step.get('action')
        if not isinstance(name, str) or not NAME.fullmatch(name) or name in names:
            raise ValueError('Use unique lowercase step names')
        names.add(name)
        fields = {'tap': {'x','y'}, 'swipe': {'x','y','x2','y2','ms'},
                  'controller': {'buttons','ms'}, 'wait': {'ms'},
                  'screenshot': set(), 'cancel': set()}.get(action)
        if fields is None or set(step) != fields | {'name','action'}:
            raise ValueError('Invalid action fields: ' + str(action))
        for axis in fields & {'x','y','x2','y2'}:
            if type(step[axis]) is not int or not 0 <= step[axis] < (width if axis.startswith('x') else height):
                raise ValueError('Coordinate outside the running profile')
        if 'ms' in fields:
            maximum = 3000 if action == 'controller' else 5000
            minimum = 0 if action == 'wait' else 1
            if type(step['ms']) is not int or not minimum <= step['ms'] <= maximum:
                raise ValueError('Invalid bounded duration')
            duration += step['ms']
        if action == 'controller' and (type(step['buttons']) is not int or not 0 <= step['buttons'] <= 4095):
            raise ValueError('Controller mask must be 0..4095')
        images += action == 'screenshot'
    if duration > 15000 or images > 8:
        raise ValueError('A batch accepts at most 15 seconds of waits/holds and 8 screenshots')


def load_sdk(root):
    path = Path(root) / 'tools/pxadb/pxadb.py'
    spec = importlib.util.spec_from_file_location('_pxa_batch_sdk', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def execute(sdk, selector, steps, output, *, socket_root=None):
    if not re.fullmatch(r'[a-z0-9-]+@[a-z0-9-]+', selector):
        raise ValueError('Use an explicit owned simulator selector, never a device port')
    profile = selector.split('@', 1)[0]
    desktop = tomllib.loads((Path(sdk) / 'share/pxa/profiles' / (profile + '.toml')).read_text())['desktop']
    validate(steps, desktop['width'], desktop['height'])
    module = load_sdk(sdk)
    sockets = Path(socket_root or os.environ.get('PXA_SIMULATOR_SOCKET_ROOT', f'/tmp/pxa-simulator-{os.getuid()}'))
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    result = {'schema':'pxa-simulator-batch-1', 'runtime_passed':False,
              'visual_review_required':True, 'physical_device_tested':False,
              'selector':selector, 'steps':[], 'screenshots':[], 'input_released':False}
    connected = False
    started = time.monotonic()
    try:
        # Reuse the SDK implementation and one HELLO/session for the whole batch.
        with module.open_client('unix:' + str(sockets / (selector + '.sock')), 5) as client:
            connected = True
            try:
                result['info'] = client.request('INFO')[-1].payload
                for step in steps:
                    action, before = step['action'], time.monotonic()
                    if action == 'tap':
                        client.request(f"INPUT TAP {step['x']} {step['y']}")
                    elif action == 'swipe':
                        client.request(f"INPUT SWIPE {step['x']} {step['y']} {step['x2']} {step['y2']} {step['ms']} 12",
                                       timeout=step['ms']/1000+5)
                    elif action == 'controller':
                        inject(selector, step['buttons'], step['ms'], socket_root=sockets)
                    elif action == 'cancel':
                        inject(selector, socket_root=sockets)
                    elif action == 'wait':
                        time.sleep(step['ms']/1000)
                    elif action == 'screenshot':
                        capture = module.screenshot_capture(client.request('SCREENSHOT', timeout=15))
                        path = output / (step['name'] + '.png')
                        module.write_screenshot(capture, path)
                        result['screenshots'].append({'path':path.name,
                            'sha256':hashlib.sha256(path.read_bytes()).hexdigest(), 'post_processed':False})
                    result['steps'].append({'name':step['name'], 'action':action,
                                            'elapsed_ms':round((time.monotonic()-before)*1000, 2)})
            finally:
                # Never replay a failed write. Release every input, even on errors.
                inject(selector, socket_root=sockets)
                result['input_released'] = True
        result['runtime_passed'] = True
        return result
    except BaseException as error:
        result['error'] = str(error) or type(error).__name__
        result['connected'] = connected
        raise
    finally:
        result['elapsed_ms'] = round((time.monotonic()-started)*1000, 2)
        (output / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--simulator', required=True)
    parser.add_argument('--steps', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = execute(args.sdk, args.simulator, json.loads(args.steps), args.output)
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except (ValueError, RuntimeError, OSError) as error:
        print('pxa-batch: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

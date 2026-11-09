#!/usr/bin/env python3
"""Run the signed direction example through real UI controls and screenshots."""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('simulator', 'package', 'publisher-key', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--density-dpi', type=int, default=160)
    args = p.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    reports = []
    for width, height in [(480, 854), (854, 480), (480, 480)]:
        out = args.output / f'{width}x{height}'
        out.mkdir(exist_ok=True)
        (out / 'state').mkdir(exist_ok=True)
        control = f'/tmp/pxa-rotation-{os.getpid()}.sock'
        log = (out / 'simulator.log').open('w')
        process = subprocess.Popen([str(args.simulator.resolve()), '--package', str(args.package.resolve()),
            '--publisher-key', str(args.publisher_key.resolve()), '--state-root', str(out / 'state'),
            '--width', str(width), '--height', str(height), '--density-dpi', str(args.density_dpi),
            '--pxadb-control-socket', control], env=dict(os.environ,
                SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'), stdout=log, stderr=subprocess.STDOUT)

        def command(text):
            with socket.socket(socket.AF_UNIX) as client:
                client.settimeout(6)
                client.connect(control)
                client.sendall((text + '\n').encode())
                with client.makefile('rb') as stream:
                    header = stream.readline()
                    if header.startswith((b'JSON ', b'PNG ')):
                        data = stream.read(int(header.split()[1]))
                        assert len(data) == int(header.split()[1])
                        return data
                    assert header.startswith(b'OK'), (text, header)
                    return header

        def inspect():
            return json.loads(command('UI INSPECT'))

        def wait(predicate):
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                assert process.poll() is None, (out / 'simulator.log').read_text()[-2000:]
                try:
                    if predicate():
                        return
                except (FileNotFoundError, ConnectionRefusedError, AssertionError):
                    pass
                time.sleep(.05)
            raise AssertionError((out / 'simulator.log').read_text()[-2500:])

        def select(text):
            nodes = inspect()['nodes']
            node = next(n for n in nodes if n.get('text') == text and n['visible'])
            while not node['clickable'] and node['parent']:
                node = next(n for n in nodes if n['id'] == node['parent'] and n['surface'] == node['surface'])
            assert node['clickable'] and node['visible'] and node['enabled']
            box = node['clip']
            x, y = box['x'] + box['width'] // 2, box['y'] + box['height'] // 2
            command(f'TOUCH DOWN 0 {x} {y}')
            time.sleep(.04)
            command(f'TOUCH UP 0 {x} {y}')

        def expect(rotation, name):
            w, h = (height, width) if rotation & 1 else (width, height)
            wait(lambda: inspect()['viewport'] == {'width': w, 'height': h} and
                 any(f'/ {rotation * 90} deg /' in (n.get('text') or '') for n in inspect()['nodes']))
            png = command('SCREENSHOT AFTER_PRESENT')
            assert struct.unpack('>II', png[16:24]) == (w, h)
            (out / (name + '.png')).write_bytes(png)

        try:
            wait(lambda: Path(control).exists() and bool(inspect()['nodes']))
            expect(0, 'native')
            for angle in (90, 180, 270, 0):
                select(str(angle))
                expect(angle // 90, str(angle))
            select('Landscape')
            expect(0 if width >= height else 1, 'landscape')
            select('Portrait')
            expect(0 if width <= height else 1, 'portrait')
            select('0')
            expect(0, 'before-held-pointer')
            # A direction change must cancel a second, still-held contact.
            command('TOUCH DOWN 1 1 1')
            select('90')
            expect(1, 'before-lock')
            command('TOUCH DOWN 1 1 1')
            command('TOUCH UP 1 1 1')
            select('Lock')
            expect(1, 'locked')
            if width == height:
                select('Landscape')
                expect(1, 'square-landscape-retains-angle')
                select('Portrait')
                expect(1, 'square-portrait-retains-angle')
            select('System')
            expect(0, 'system')
            reports.append({'native': [width, height], 'all_angles': True,
                            'dynamic_preferences': True, 'passed': True})
            print(json.dumps(reports[-1]), flush=True)
        finally:
            process.terminate()
            process.wait(timeout=5)
            log.close()
    (args.output / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()

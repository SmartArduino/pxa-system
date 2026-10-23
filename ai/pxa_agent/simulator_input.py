"""Owned desktop input via the existing DevKit UI control socket.

DevKit 0.5.0 exposes phased SDL keys but its PXADB bridge does not forward
controller masks. Keep the adapter in the Agent Kit; never patch the SDK.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import socket
import time

KEYS = ('UP', 'DOWN', 'LEFT', 'RIGHT', 'Z', 'X', 'ENTER', 'TAB', 'A', 'S', 'Q', 'W')


def request(path, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(str(path))
        connection.sendall((command + '\n').encode('ascii'))
        response = bytearray()
        while b'\n' not in response and len(response) <= 128:
            part = connection.recv(128)
            if not part:
                break
            response.extend(part)
        if bytes(response).split(b'\n', 1)[0] != b'OK':
            raise RuntimeError('Simulator input failed: ' + response.decode('ascii', errors='replace').strip())


def cancel(path):
    errors = []
    # Attempt every release even when one command fails. No physical GPIO is
    # involved: these are SDL keyboard events delivered to the Guest controller.
    for command in [*(f'KEY_UP {key}' for key in KEYS), 'TOUCH CANCEL_ALL']:
        try:
            request(path, command)
        except (OSError, RuntimeError) as error:
            errors.append(error)
    if errors:
        raise errors[0]


def inject(selector, buttons=None, hold_ms=100, *, socket_root=None):
    if not re.fullmatch(r'[a-z0-9@-]+', selector):
        raise ValueError('Invalid owned simulator selector')
    if buttons is not None and (type(buttons) is not int or not 0 <= buttons <= 4095
                                or type(hold_ms) is not int or not 1 <= hold_ms <= 3000):
        raise ValueError('Controller requires mask 0..4095 and hold 1..3000 ms')
    root = Path(socket_root or os.environ.get('PXA_SIMULATOR_SOCKET_ROOT', f'/tmp/pxa-simulator-{os.getuid()}'))
    path = root / (selector + '.control.sock')
    if buttons is None:
        cancel(path)
    else:
        try:
            for bit, key in enumerate(KEYS):
                if buttons & (1 << bit):
                    request(path, 'KEY_DOWN ' + key)
            time.sleep(hold_ms / 1000)
        finally:
            cancel(path)
    return 'Simulator SDL controller input released; physical_device_tested=false'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selector')
    parser.add_argument('--buttons', type=int)
    parser.add_argument('--hold-ms', type=int, default=100)
    arguments = parser.parse_args()
    print(inject(arguments.selector, arguments.buttons, arguments.hold_ms))

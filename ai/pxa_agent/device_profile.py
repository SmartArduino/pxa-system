"""Read device design constraints without installing, rebooting or flashing."""
from __future__ import annotations

import re

from .common import ToolError
from .workbench import info_fields


def connection_help():
    return [
        'Connect the powered-on device with a USB data cable, then run doctor to find its port.',
        'On the device open Settings / PXADB debugging (设置 / PXADB 调试) and enable it if that switch exists.',
        'Close serial monitors and other PXADB clients; select the newly detected port and retry device-profile --port PORT.',
        'On Linux check serial-port permissions; on Windows check Device Manager for the COM port and USB driver.',
        'BOOT/RESET enters firmware download mode, not PXADB. Boot normally for app installation and debugging.',
    ]


def inspect(environment, port=None, baud=2000000):
    result = {'connected': False, 'port': port, 'connection_steps': connection_help(),
              'questions': ['Which controls should the app support: touch, buttons, rotary encoder or gamepad?',
                            'Should it adapt to other resolutions, physical sizes, rounded corners or round screens?',
                            'Which sensors or peripherals are required? Unreported capabilities need confirmation.']}
    if not port:
        result['ports'] = environment.doctor().get('devices', [])
        return result
    if not isinstance(port, str) or not port.strip() or port.startswith('-'):
        raise ToolError('Choose an explicit serial port from doctor')
    if not 1200 <= baud <= 4000000:
        raise ToolError('Baud must be 1200..4000000')
    args = ['--port', port, '--baud', str(baud)]
    try:
        fields = info_fields(environment.device(['info', *args], timeout=12))
        if not fields.get('target'):
            raise ToolError('The selected endpoint did not return a PXA target')
    except ToolError as error:
        result['error'] = str(error)
        return result
    result.update(connected=True, info=fields)
    try:
        result['debug_input'] = info_fields(environment.device(['input', 'capabilities', *args], timeout=12))
    except ToolError as error:
        result['input_error'] = str(error)
    # Debug injection support is not evidence of physical touch/controllers.
    inputs = result.get('debug_input', {})
    def dimension(name):
        value = fields.get(name, inputs.get(name, ''))
        return int(value) if re.fullmatch(r'[1-9][0-9]{0,4}', value) else None
    result['display'] = {'width': dimension('width'), 'height': dimension('height'),
                         'shape': fields.get('shape'), 'safe_insets': fields.get('safe_insets')}
    count = fields.get('sensor_count')
    result['sensors'] = {'count': int(count) if count and count.isdecimal() else None,
                         'semantics': fields.get('sensors', '').split(',') if fields.get('sensors') else [],
                         'note': 'Reported descriptors only; absence on older firmware means unknown. Query the Sensor service in the app for the complete list and permissions.'}
    result['physical_controls_confirmed'] = False
    return result

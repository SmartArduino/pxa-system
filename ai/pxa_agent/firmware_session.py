"""Keep ESP32-S31 partition checks and writes on one ROM connection."""
from __future__ import annotations

import os
from pathlib import Path
import re
import time

from .common import ToolError, atomic_json, digest


def download_port(board, port, expected_serial=None):
    """Mosaico's normal CDC has an explicit 1200-baud download control."""
    if board != 'esp-mosaico':
        return port, None
    import serial
    from serial.tools.list_ports import comports
    selected = next((p for p in comports() if p.device.casefold() == port.casefold()), None)
    if not selected or selected.product != 'ESP-Mosaico':
        return port, None  # The user may already have entered ROM manually.
    identity = (selected.serial_number or '').lower()
    if not re.fullmatch(r'[0-9a-f]{2}(?::[0-9a-f]{2}){5}', identity) or (
            expected_serial and identity != expected_serial.lower()):
        raise ToolError('Mosaico USB identity does not match the selected device')
    link = serial.Serial(port=None, baudrate=1200, timeout=.5)
    link.dtr, link.rts, link.port = True, False, port
    try:
        with link:
            if os.name == 'posix':
                import termios
                settings = termios.tcgetattr(link.fileno())
                settings[2] &= ~termios.HUPCL
                termios.tcsetattr(link.fileno(), termios.TCSANOW, settings)
            time.sleep(.1)
            link.dtr = False
    except (OSError, serial.SerialException):
        pass  # USB can disappear while acknowledging its reboot control.
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        rows = [p for p in comports() if p.vid == 0x303a and p.product == 'ESP32-S31'
                and (p.serial_number or '').lower() == identity]
        if len(rows) == 1:
            time.sleep(.2)
            return rows[0].device, identity
        time.sleep(.1)
    raise ToolError('Mosaico did not enter ROM. Press RESET and reconnect; no firmware was written.')


def write(prepared, board, port, baud, output, expected_serial=None):
    import esptool
    from esptool import cmds
    from .firmware import partition_table

    port, identity = download_port(board, port, expected_serial)
    esp = None
    phase = 'connect'
    try:
        esp = esptool.detect_chip(port, baud=baud, connect_mode='no-reset')
        if esp.CHIP_NAME.lower().replace('-', '') != prepared['chip']:
            raise ToolError('Connected ROM CPU does not match the signed firmware')
        # The CLI performs this before its read/write commands; API consumers
        # must also initialize the actual SPI flash connection explicitly.
        cmds.attach_flash(esp)
        if prepared['mode'] == 'app':
            phase = 'read-partitions'
            raw = cmds.read_flash(esp, 0x8000, 0x1000, no_progress=True)
            (output / 'installed-partitions.bin').write_bytes(raw)
            actual = partition_table(raw)
            if actual != prepared['partitions']:
                atomic_json(output / 'partition-mismatch.json', {'installed': actual, 'release': prepared['partitions']})
                raise ToolError('Partition layouts differ; no firmware was written. Back up before an authorized full migration.')
        if digest(prepared['image']) != prepared['sha256']:
            raise ToolError('Firmware changed after signature verification; no firmware was written')
        phase = 'write-application' if prepared['mode'] == 'app' else 'write-full'
        cmds.write_flash(esp, [(prepared['offset'], prepared['image'])], compress=False)
        phase = 'written'
        return port, identity
    except ToolError:
        raise
    except Exception as error:
        raise ToolError(f'ESP32-S31 firmware operation failed at {phase}: {error}. '
                        'Check the USB connection and ROM mode; inspect the evidence before retrying.') from error
    finally:
        atomic_json(output / 'flash-session.json', {'phase': phase, 'port': port,
                    'flash_completed': phase == 'written', 'data_partition_written': prepared['mode'] == 'full' and phase in ('write-full', 'written')})
        if esp is not None:
            try:
                esp.hard_reset()
            except Exception:
                pass  # Report boot verification separately; never mask a failure.
            finally:
                esp._port.close()

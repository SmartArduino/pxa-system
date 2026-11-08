import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from .common import ToolError
from .firmware_session import write


class FirmwareSessionTest(unittest.TestCase):
    def test_one_connection_checks_layout_before_writing_and_closes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image = root / 'app.bin'
            image.write_bytes(b'firmware')
            table = struct.pack('<HBBII16sI', 0x50aa, 0, 0, 0x10000, 0x400000, b'factory', 0) + b'\xff' * 32
            esp = Mock(CHIP_NAME='ESP32-S31')
            calls = []
            cmds = SimpleNamespace(attach_flash=lambda value: calls.append(('attach', value)),
                read_flash=lambda value, *args, **kwargs: calls.append(('read', value)) or table,
                write_flash=lambda value, *args, **kwargs: calls.append(('write', value)))
            detect = Mock(return_value=esp)
            prepared = dict(chip='esp32s31', mode='app', image=str(image), offset=0x10000,
                            sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                            partitions={'factory': {'offset': 0x10000, 'size': 0x400000}})
            with patch.dict(sys.modules, {'esptool': SimpleNamespace(detect_chip=detect, cmds=cmds)}):
                write(prepared, 'doit-gamekit', 'test-port', 2000000, root)
            detect.assert_called_once()
            self.assertEqual([name for name, _ in calls], ['attach', 'read', 'write'])
            self.assertTrue(all(value is esp for _, value in calls))
            esp._port.close.assert_called_once()
            self.assertTrue(json.loads((root / 'flash-session.json').read_text())['flash_completed'])
            calls.clear()
            prepared['partitions']['factory']['size'] = 0x500000
            with patch.dict(sys.modules, {'esptool': SimpleNamespace(detect_chip=detect, cmds=cmds)}):
                with self.assertRaisesRegex(ToolError, 'layouts differ'):
                    write(prepared, 'doit-gamekit', 'test-port', 2000000, root)
            self.assertEqual([name for name, _ in calls], ['attach', 'read'])
            self.assertFalse(json.loads((root / 'flash-session.json').read_text())['flash_completed'])

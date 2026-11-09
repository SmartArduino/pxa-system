import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, Mock, patch

from .common import ToolError
from .firmware_session import download_port, write


class FirmwareSessionTest(unittest.TestCase):
    @unittest.skipUnless(os.name == 'posix', 'POSIX device permissions')
    def test_rom_enumeration_waits_for_permissions_without_reopening_control(self):
        identity='1c:29:04:d0:ad:04'
        app=SimpleNamespace(device='/dev/app',product='ESP-Mosaico',serial_number=identity,vid=0x303a)
        rom=SimpleNamespace(device='/dev/rom',product='ESP32-S31',serial_number=identity,vid=0x303a)
        link=MagicMock();link.__enter__.return_value=link
        with patch('serial.tools.list_ports.comports',side_effect=[[app],[rom],[rom],[rom]]) as ports, \
             patch('serial.Serial',return_value=link) as serial, \
             patch('termios.tcgetattr',return_value=[0,0,0,0,0,0,[]]),patch('termios.tcsetattr'), \
             patch('pxa_agent.firmware_session.os.access',side_effect=[False,False,True]), \
             patch('pxa_agent.firmware_session.time.sleep'):
            self.assertEqual(download_port('esp-mosaico','/dev/app',identity),('/dev/rom',identity))
        serial.assert_called_once();self.assertEqual(ports.call_count,4)
        self.assertFalse(link.dtr)

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
            esp.change_baud.side_effect = lambda rate: calls.append(('baud', esp))
            detect = Mock(return_value=esp)
            prepared = dict(chip='esp32s31', mode='app', image=str(image), offset=0x10000,
                            sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                            partitions={'factory': {'offset': 0x10000, 'size': 0x400000}})
            with patch.dict(sys.modules, {'esptool': SimpleNamespace(detect_chip=detect, cmds=cmds)}):
                write(prepared, 'doit-gamekit', 'test-port', 2000000, root)
            detect.assert_called_once_with('test-port', baud=115200, connect_mode='no-reset')
            esp.change_baud.assert_called_once_with(2000000)
            self.assertEqual([name for name, _ in calls], ['baud', 'attach', 'read', 'write'])
            self.assertTrue(all(value is esp for _, value in calls))
            esp._port.close.assert_called_once()
            self.assertTrue(json.loads((root / 'flash-session.json').read_text())['flash_completed'])
            calls.clear()
            prepared['partitions']['factory']['size'] = 0x500000
            with patch.dict(sys.modules, {'esptool': SimpleNamespace(detect_chip=detect, cmds=cmds)}):
                with self.assertRaisesRegex(ToolError, 'layouts differ'):
                    write(prepared, 'doit-gamekit', 'test-port', 2000000, root)
            self.assertEqual([name for name, _ in calls], ['baud', 'attach', 'read'])
            self.assertFalse(json.loads((root / 'flash-session.json').read_text())['flash_completed'])
            # A ROM without baud negotiation remains usable at its initial
            # synchronized rate; an arbitrary link error must still abort.
            prepared['partitions']['factory']['size'] = 0x400000
            class UnsupportedBaud(Exception):
                pass
            module = SimpleNamespace(detect_chip=detect, cmds=cmds,
                                     NotImplementedInROMError=UnsupportedBaud)
            calls.clear()
            esp.change_baud.side_effect = UnsupportedBaud()
            with patch.dict(sys.modules, {'esptool': module}):
                write(prepared, 'doit-gamekit', 'test-port', 2000000, root)
            self.assertEqual([name for name, _ in calls], ['attach', 'read', 'write'])
            calls.clear()
            esp.change_baud.side_effect = OSError('link lost')
            with patch.dict(sys.modules, {'esptool': module}):
                with self.assertRaisesRegex(ToolError, 'link lost'):
                    write(prepared, 'doit-gamekit', 'test-port', 2000000, root)
            self.assertEqual(calls, [])

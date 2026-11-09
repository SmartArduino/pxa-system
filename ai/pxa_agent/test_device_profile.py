import unittest
from unittest.mock import Mock

from .common import ToolError
from .device_profile import inspect


class DeviceProfileTest(unittest.TestCase):
    def test_discovery_never_opens_a_port(self):
        env = Mock()
        env.doctor.return_value = {'devices': [{'port': 'COM7'}]}
        self.assertEqual(inspect(env)['ports'], [{'port': 'COM7'}])
        self.assertTrue(any('7 times' in step and '系统版本' in step for step in inspect(env)['connection_steps']))
        env.device.assert_not_called()

    def test_old_firmware_unknown_is_not_no_sensors(self):
        env = Mock()
        env.device.side_effect = ['target=esp32s31;board=esp-mosaico', ToolError('unsupported'),ToolError('unsupported')]
        profile = inspect(env, 'COM7')
        self.assertTrue(profile['connected'])
        self.assertIsNone(profile['sensors']['count'])
        self.assertIsNone(profile['display']['width'])
        self.assertFalse(profile['physical_controls_confirmed'])

    def test_live_dimensions_and_sensor_semantics(self):
        env = Mock()
        env.device.side_effect = ['target=esp32s3;width=412;height=412;sensor_count=1;sensors=ambient-light',
                                  '{"source":"legacy-info","services_complete":false}',
                                  'width=412;height=412;controller_mask=4095']
        profile = inspect(env, '/dev/ttyACM1')
        self.assertEqual(profile['display']['width'], 412)
        self.assertEqual(profile['sensors']['semantics'], ['ambient-light'])
        self.assertFalse(profile['physical_controls_confirmed'])
        self.assertEqual(profile['compatibility']['status'],'unknown')
        self.assertTrue(all(call.args[0][0] in ('info', 'input','runtime-info') for call in env.device.call_args_list))

    def test_disabled_pxadb_has_actionable_recovery(self):
        env = Mock()
        env.device.side_effect = ToolError('timeout')
        profile = inspect(env, 'COM7')
        self.assertFalse(profile['connected'])
        self.assertGreater(len(profile['connection_steps']), 2)
        self.assertEqual(env.device.call_count, 1)

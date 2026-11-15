import json
from pathlib import Path
import unittest
from unittest.mock import patch
from unittest.mock import Mock
from .common import ToolError
from .device_matrix import connections, inspect_many, parse_connections
from .cli import parser


class DeviceMatrixTest(unittest.TestCase):
    def test_keeps_devices_and_transport_policies_independent(self):
        descriptors = {
            'COM7': {'display': {'width': 240, 'height': 240},
                     'physical': {'touch': False, 'simultaneous': False,
                                  'exclusive_groups': [['up', 'boot', 'down']]},
                     'modes': [{'id': 'three-key', 'kind': 'focus'}],
                     'reserved': [{'control': 'boot', 'event': 'hold', 'action': 'power.off', 'threshold_ms': 8000}]},
            'COM8': {'display': {'width': 320, 'height': 240},
                     'physical': {'touch': True, 'simultaneous': True},
                     'modes': [{'id': 'touch', 'kind': 'touch'}, {'id': 'pad', 'kind': 'controller'}]},
        }
        def read(arguments, **kwargs):
            port = arguments[arguments.index('--port') + 1]
            if port == 'COM9': raise ToolError('unreachable')
            command = arguments[0]
            return {'info': 'target=esp32s3;board=' + port,
                    'runtime-info': '{}',
                    'interaction-info': json.dumps({'descriptor': descriptors[port]}),
                    'input': 'touch=true;controller=true'}[command]
        env = Mock();env.device.side_effect = read
        report = inspect_many(env, [{'port': 'COM7', 'baud': 115200, 'serial_profile': 'mz79'},
                                    {'port': 'COM8'}, {'port': 'COM9'}])
        self.assertFalse(report['all_connected']);self.assertFalse(report['hardware_verified'])
        first, second, missing = [item['profile'] for item in report['devices']]
        self.assertFalse(first['physical_controls']['touch'])
        self.assertFalse(first['physical_controls']['simultaneous'])
        self.assertEqual(first['system_reservations'][0]['threshold_ms'], 8000)
        self.assertTrue(second['physical_controls']['touch']);self.assertFalse(missing['connected'])
        self.assertEqual(first['compatibility']['status'], 'unknown')
        self.assertFalse(second['physical_controls_confirmed'])
        for call in env.device.call_args_list:
            args = call.args[0];self.assertIn(args[0], ('info', 'runtime-info', 'interaction-info', 'input'))
            if 'COM7' in args:
                self.assertIn('mz79', args);self.assertEqual(args[args.index('--baud') + 1], '115200')
            else:self.assertNotIn('--serial-profile', args)

    def test_validates_whole_request_before_reading_any_device(self):
        bad = [[], [{}], [{'port': 'COM7'}, {'port': 'com7'}], [{'port': '-bad'}],
               [{'port': 'COM7', 'baud': True}], [{'port': 'COM7', 'serial_profile': 'guess'}],
               [{'port': 'COM7', 'erase': True}], [{'port': 'COM7', 'label': 'bad\nlabel'}],
               [{'port': 'COM'+str(i)} for i in range(9)]]
        for values in bad:
            env = Mock()
            with self.subTest(values=values), self.assertRaises(ToolError):inspect_many(env, values)
            env.device.assert_not_called()

    def test_missing_package_is_rejected_before_opening_ports(self):
        env=Mock();env.project.return_value=Mock(exists=lambda:False)
        with self.assertRaises(ToolError):inspect_many(env,[{'port':'COM7'}],package='missing.pxa')
        env.device.assert_not_called()

    def test_json_and_repeated_cli_ports(self):
        values=connections([{'port':'/dev/ttyACM2'},{'port':'/dev/ttyacm2','baud':115200}])
        self.assertEqual(len(values),2)
        self.assertEqual(parse_connections(json.dumps(values)),values)
        for raw in ('{','{}','x'*16385):
            with self.assertRaises(ToolError):parse_connections(raw)
        args=parser().parse_args(['device-profiles','--port','COM7','--port','COM8'])
        self.assertEqual(args.port,['COM7','COM8'])
        self.assertEqual(parser().parse_args(['device-profile','--port','COM7']).port,'COM7')


class DeviceMatrixMCPTest(unittest.IsolatedAsyncioTestCase):
    async def test_batch_is_read_only_and_returns_a_bounded_host_job(self):
        from .mcp_server import create_server
        from mcp.server.mcpserver.exceptions import ToolError as MCPToolError
        env=Mock(workspace=Path('/workspace'))
        with patch('pxa_agent.mcp_server.Jobs') as jobs:
            jobs.return_value.start.return_value={'job_id':'matrix-job','running':True}
            server=create_server(env)
            tools={tool.name:tool for tool in await server.list_tools()}
            self.assertTrue(tools['pxa_device_profiles'].annotations.read_only_hint)
            result=await server.call_tool('pxa_device_profiles',{'devices':[
                {'port':'COM7'},{'port':'COM8','serial_profile':'mz79','baud':115200}]})
            self.assertFalse(result.is_error)
            args=jobs.return_value.start.call_args
            self.assertEqual(args.kwargs['timeout'],900);self.assertTrue(args.kwargs['host'])
            command=args.args[0]
            connections_arg=json.loads(command[command.index('--devices-json')+1])
            self.assertEqual(connections_arg[1]['baud'],115200)
            self.assertEqual(connections_arg[1]['serial_profile'],'mz79')
            jobs.return_value.start.reset_mock()
            with self.assertRaises(MCPToolError):
                await server.call_tool('pxa_device_profiles',{'devices':[{'port':'COM7'},{'port':'COM7'}]})
            jobs.return_value.start.assert_not_called()

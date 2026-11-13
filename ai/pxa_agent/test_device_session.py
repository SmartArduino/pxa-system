from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from .common import ToolError
from .device_session import install_connection


class DeviceSessionTest(unittest.TestCase):
    def fixture(self, info='target=esp32p4;board=espp4-a;serial=unavailable'):
        class ProtocolError(Exception):
            pass
        state = {'open': 0, 'closed': 0}
        def request(command, **kwargs):
            self.assertEqual(state['open'] - state['closed'], 1)
            if command == 'INFO':
                return [SimpleNamespace(kind='OK', payload=info)]
            if command == 'PACKAGES':
                return [SimpleNamespace(kind='PKG', payload='root:app\tName\t1\tactive=1'),
                        SimpleNamespace(kind='OK', payload='')]
            return [SimpleNamespace(kind='OK', payload='queued')]
        client = Mock()
        client.request.side_effect = request
        @contextmanager
        def opened(*args, **kwargs):
            state['open'] += 1
            try:
                yield client
            finally:
                state['closed'] += 1
        tools = SimpleNamespace(PxaDbError=ProtocolError,
                                open_client=Mock(side_effect=opened),
                                package_compatibility=Mock(return_value={'status': 'compatible'}),
                                build_parser=Mock(return_value=Mock()),
                                command_package_install=Mock())
        return tools, client, state

    def test_install_and_activation_borrow_one_connection_with_explicit_baud(self):
        tools, client, state = self.fixture()
        with patch('pxa_agent.device_session.reader', return_value=tools):
            with install_connection(None, 'COM7', 2000000, {'target': 'esp32p4'}) as device:
                self.assertIn('compatible', device('compatibility', Path('app.pxa'), '--report-only'))
                self.assertIn('root:app', device('package', 'list'))
                device('package', 'stop', 'root:app')
                device('package', 'install', Path('app.pxa'), '--yes', '--strict-compatibility', timeout=300)
                device('package', 'run', 'root:app')
                self.assertIn('active=1', device('package', 'list'))
                tools.command_package_install.assert_called_once_with(
                    tools.build_parser.return_value.parse_args.return_value,
                    connection=client, quiet=True)
                self.assertEqual(state, {'open': 1, 'closed': 0})
            tools.open_client.assert_called_once_with('COM7', 10, baudrate=2000000)
        self.assertEqual(state, {'open': 1, 'closed': 1})

    def test_changed_device_is_closed_before_any_install_or_stop(self):
        for field, value in [('target', 'esp32s3'), ('board', 'other-board'), ('serial', '00:11')]:
            tools, client, state = self.fixture()
            with self.subTest(field=field), patch('pxa_agent.device_session.reader', return_value=tools):
                with self.assertRaisesRegex(ToolError, 'changed while building'):
                    with install_connection(None, 'COM7', 2000000, {field: value}):
                        self.fail('Changed device entered the installation workflow')
                client.request.assert_called_once_with('INFO')
                tools.command_package_install.assert_not_called()
                self.assertEqual(state, {'open': 1, 'closed': 1})

    def test_uncertain_commit_is_not_retried_and_connection_is_closed(self):
        tools, client, state = self.fixture()
        tools.command_package_install.side_effect = tools.PxaDbError('uncertain write')
        with patch('pxa_agent.device_session.reader', return_value=tools):
            with self.assertRaisesRegex(ToolError, 'uncertain write'):
                with install_connection(None, 'COM7', 2000000, {}) as device:
                    device('package', 'install', Path('app.pxa'), '--yes', timeout=300)
        tools.command_package_install.assert_called_once()
        self.assertEqual(state, {'open': 1, 'closed': 1})

    def test_scope_does_not_expose_arbitrary_protocol_commands(self):
        tools, client, state = self.fixture()
        with patch('pxa_agent.device_session.reader', return_value=tools):
            with install_connection(None, 'COM7', 2000000, {}) as device:
                for arguments in [(), ('fs', 'rmtree', '/'), ('PACKAGE uninstall', 'other')]:
                    with self.subTest(arguments=arguments), self.assertRaises(ToolError):
                        device(*arguments)
        client.request.assert_called_once_with('INFO')
        self.assertEqual(state, {'open': 1, 'closed': 1})

    def test_denied_install_preserves_diagnostic_and_never_retries_or_runs(self):
        for diagnostic in ('install_status=-4', 'install_status=-4;stage=publisher'):
            tools, client, state = self.fixture()
            tools.command_package_install.side_effect = tools.PxaDbError(diagnostic)
            with self.subTest(diagnostic=diagnostic), patch('pxa_agent.device_session.reader', return_value=tools):
                with self.assertRaises(ToolError) as caught:
                    with install_connection(None, 'COM7', 2000000, {}) as device:
                        device('package', 'install', Path('app.pxa'), '--yes', timeout=300)
                        device('package', 'run', 'root:app')
                self.assertIn('device-bound', str(caught.exception))
                self.assertIn('Preserve existing code and saves', str(caught.exception))
                self.assertEqual(str(caught.exception.__cause__), diagnostic)
                tools.command_package_install.assert_called_once()
                client.request.assert_called_once_with('INFO')
                self.assertEqual(state, {'open': 1, 'closed': 1})

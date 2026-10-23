"""Meaningful boundary, language/context and board-preflight regression tests."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from . import boards, context, scope
from .common import KIT, ToolError


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.ws = self.root / 'workspace'
        self.ws.mkdir()
        self.sdk = self.root / 'sdk'
        (self.sdk / 'sdk/guest-c/include').mkdir(parents=True)
        (self.sdk / 'VERSION').write_text('0.5.0')
        (self.sdk / 'sdk/guest-c/include/pxa_ui.h').write_text('int pxa_ui_show(void);\n')
        (self.ws / 'AGENTS.md').write_text('Existing user rules')
        self.app = self.ws / 'apps/game'
        self.app.mkdir(parents=True)
        (self.app / 'main.c').write_text('original')

    def tearDown(self):
        self.tmp.cleanup()

    def start(self):
        return scope.begin(self.ws, mode='app', project='apps/game', sdk=self.sdk)

    def test_app_edits_and_artifacts_allowed_but_sdk_edits_detected(self):
        self.start()
        (self.app / 'main.c').write_text('updated')
        (self.ws / 'local/artifacts/test').mkdir(parents=True)
        (self.ws / 'local/artifacts/test/log').write_text('evidence')
        self.assertTrue(scope.check(self.ws)['passed'])
        path = self.sdk / 'sdk/guest-c/include/pxa_ui.h'
        path.write_text('int invented(void);')
        result = scope.check(self.ws)
        self.assertFalse(result['passed'])
        self.assertIn(str(path), result['violations'])
        with self.assertRaises(ToolError):
            self.start()  # A second begin cannot hide a violation.
        self.assertEqual(path.read_text(), 'int invented(void);')
        with self.assertRaises(ToolError):
            scope.finish(self.ws)

    def test_finish_archives_only_a_passing_scope(self):
        self.start()
        result = scope.finish(self.ws)
        self.assertTrue(Path(result['archive']).is_file())
        self.assertFalse(scope.check(self.ws)['active'])

    def test_dirty_baseline_is_preserved_and_later_edit_detected(self):
        subprocess.run(['git', 'init', '-q', self.ws], check=True)
        subprocess.run(['git', '-C', self.ws, 'add', '.'], check=True)
        (self.ws / 'AGENTS.md').write_text('Already edited by user')
        self.start()
        self.assertTrue(scope.check(self.ws)['passed'])
        (self.ws / 'AGENTS.md').write_text('Changed again')
        self.assertFalse(scope.check(self.ws)['passed'])

    def test_new_file_deletion_and_timestamp_restoration(self):
        self.start()
        old = (self.ws / 'AGENTS.md').stat()
        (self.ws / 'AGENTS.md').write_text('Other user rule xxxx')
        os.utime(self.ws / 'AGENTS.md', ns=(old.st_atime_ns,old.st_mtime_ns))
        self.assertFalse(scope.check(self.ws)['passed'])
        (self.ws / 'AGENTS.md').write_text('Existing user rules')
        self.assertTrue(scope.check(self.ws)['passed'])
        (self.ws / 'new-platform.c').write_text('not app code')
        self.assertFalse(scope.check(self.ws)['passed'])
        (self.ws / 'new-platform.c').unlink()
        (self.ws / 'AGENTS.md').unlink()
        self.assertFalse(scope.check(self.ws)['passed'])

    def test_app_cannot_point_to_platform_or_escape(self):
        for project in ('../outside', '.', 'sdk/my-app', 'firmware/boards/test'):
            with self.subTest(project=project), self.assertRaises(ToolError):
                scope.begin(self.ws, mode='app', project=project)
        (self.ws / 'apps/link').symlink_to(self.sdk, target_is_directory=True)
        with self.assertRaises(ToolError):
            scope.begin(self.ws, mode='app', project='apps/link')

    def test_build_postcheck_rejects_platform_edits_without_reverting(self):
        from .environment import Environment
        self.start()
        env = Environment(self.ws, {'engine': 'native', 'sdk': str(self.sdk)})
        target = self.sdk / 'sdk/guest-c/include/pxa_ui.h'
        def compiler(*args, **kwargs):
            target.write_text('unexpected SDK edit')
            return 'compiler exit 0'
        with patch('pxa_agent.environment.run', compiler), self.assertRaises(ToolError):
            env.call(['build', str(self.app)])
        self.assertEqual(target.read_text(), 'unexpected SDK edit')

    def test_executable_permission_change_is_detected(self):
        self.start()
        path = self.ws / 'AGENTS.md'
        path.chmod(0o755)
        self.assertFalse(scope.check(self.ws)['passed'])

    def test_allowed_directory_replaced_with_symlink_is_detected(self):
        self.start()
        (self.app / 'main.c').unlink()
        self.app.rmdir()
        self.app.symlink_to(self.sdk, target_is_directory=True)
        self.assertFalse(scope.check(self.ws)['passed'])

    def board_workspace(self):
        api = self.ws / 'firmware/components/pxa_board_api/include'
        api.mkdir(parents=True)
        (api / 'pxa_board_api.h').write_text('pxa_board_port_t pxa_board_register initialize display_profile')
        (self.ws / 'tools').mkdir()
        (self.ws / 'tools/firmware.sh').write_text('#!/bin/sh\nexit 0\n')
        return api

    def test_board_scaffold_keeps_api_and_is_not_falsely_buildable(self):
        api = self.board_workspace()
        original = (api / 'pxa_board_api.h').read_bytes()
        result = boards.initialize(self.ws, 'test-board', target='esp32s3')
        self.assertFalse(result['buildable'])
        status = boards.inspect(self.ws, 'test-board')
        self.assertFalse(status['static_ready'])
        self.assertTrue(status['bringup_gates'])
        self.assertIn('display.rotation', status['unknown_hardware'])
        self.assertTrue(status['scope']['passed'])
        self.assertEqual(original, (api / 'pxa_board_api.h').read_bytes())
        with self.assertRaises(ToolError):
            boards.initialize(self.ws, 'test-board', target='esp32s3')
        with self.assertRaises(ToolError):
            boards.build(self.ws, 'test-board')
        (api / 'pxa_board_api.h').write_text('modified contract')
        self.assertFalse(scope.check(self.ws)['passed'])

    def test_board_changes_do_not_allow_other_boards_or_sdk(self):
        self.board_workspace()
        boards.initialize(self.ws, 'new-board', target='esp32s31')
        (self.ws / 'firmware/boards/new-board/driver.cc').write_text('new driver')
        self.assertTrue(scope.check(self.ws)['passed'])
        other = self.ws / 'firmware/boards/other'
        other.mkdir()
        (other / 'driver.cc').write_text('different board')
        self.assertFalse(scope.check(self.ws)['passed'])

    def test_context_exact_sdk_declaration_and_bounded_output(self):
        value = context.sdk_context(self.sdk, 'pxa_ui_show', language='c', limit=1)
        self.assertEqual(value['release'], '0.5.0')
        self.assertEqual(len(value['matches']), 1)
        self.assertEqual(value['matches'][0]['line'], 1)
        self.assertIn('int pxa_ui_show(void);', value['matches'][0]['excerpt'])
        self.assertEqual(context.sdk_context(self.sdk, 'unavailable')['matches'], [])
        with self.assertRaises(ValueError):
            context.sdk_context(self.sdk, 'ui', limit=1000)

    def test_controller_scenario_validation(self):
        spec = importlib.util.spec_from_file_location('verify_app', KIT / 'skills/pxa-debug/scripts/verify_app.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        dimensions = {'pai-touch': (296,240)}
        valid = {'pai-touch': [{'name':'confirm','action':'controller','buttons':16,'ms':100},
                              {'name':'release','action':'cancel'}]}
        module.validate_scenario(valid, dimensions)
        for buttons, duration in ((4096,100),(-1,100),(True,100),(16,0),(16,3001)):
            with self.assertRaises(ValueError):
                module.validate_scenario({'pai-touch':[{'name':'bad','action':'controller',
                                          'buttons':buttons,'ms':duration}]}, dimensions)

    def test_controller_adapter_releases_all_keys_after_partial_failure(self):
        from . import simulator_input
        commands = []
        def request(path, command):
            commands.append(command)
            if command == 'KEY_DOWN X':
                raise RuntimeError('injected failure')
        with patch.object(simulator_input, 'request', request), self.assertRaises(RuntimeError):
            simulator_input.inject('pai-touch@owned-123', 48, 100, socket_root=self.ws)
        self.assertIn('KEY_DOWN Z', commands)
        for key in simulator_input.KEYS:
            self.assertIn('KEY_UP ' + key, commands)
        self.assertEqual(commands[-1], 'TOUCH CANCEL_ALL')

    def test_controller_adapter_bounds_and_mask_mapping(self):
        from . import simulator_input
        with patch.object(simulator_input, 'request') as request, patch.object(simulator_input.time, 'sleep'):
            simulator_input.inject('pai-touch@owned-123', 17, 100, socket_root=self.ws)
            self.assertEqual([call.args[1] for call in request.call_args_list[:2]], ['KEY_DOWN UP', 'KEY_DOWN Z'])
            for selector, mask in (('../escape',16), ('valid',4096), ('valid',True)):
                with self.assertRaises(ValueError):
                    simulator_input.inject(selector, mask, 100, socket_root=self.ws)


if __name__ == '__main__':
    unittest.main()

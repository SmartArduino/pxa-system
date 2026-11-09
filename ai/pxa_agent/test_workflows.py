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

    def test_adopt_resumes_original_app_and_retains_failed_audit(self):
        self.start()
        probe = self.ws / 'apps/probe'
        probe.mkdir()
        scope.begin(self.ws, mode='app', project='apps/probe', sdk=self.sdk)
        source = self.app / 'main.c'
        source.write_text('resumed edits')
        with self.assertRaises(ToolError):
            self.start()
        result = scope.begin(self.ws, mode='app', project='apps/game', sdk=self.sdk, adopt=True)
        audit = json.loads(Path(result['previous_archive']).with_name('check.json').read_text())
        self.assertFalse(audit['passed'])
        self.assertIn(str(source), audit['violations'])
        self.assertTrue(scope.check(self.ws)['passed'])
        self.assertEqual(source.read_text(), 'resumed edits')

    def test_adopt_cannot_hide_sdk_changes_and_archive_keeps_violations(self):
        self.start()
        source = self.sdk / 'sdk/guest-c/include/pxa_ui.h'
        source.write_text('platform edit')
        with self.assertRaises(ToolError):
            scope.begin(self.ws, mode='app', project='apps/game', sdk=self.sdk, adopt=True)
        result = scope.finish(self.ws, allow_violations=True)
        self.assertTrue(result['ended'])
        self.assertFalse(result['passed'])
        self.assertIn(str(source), result['violations'])
        self.assertTrue(Path(result['archive']).is_file())
        self.assertEqual(source.read_text(), 'platform edit')

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

    def test_unicode_app_names_keep_distinct_publisher_scoped_ids(self):
        from .environment import Environment
        from .projects import initialize
        env = Environment(self.ws, {'engine': 'native', 'sdk': str(self.sdk)})
        ids = []
        for name in ['贪吃蛇', '俄罗斯方块', '猫咪game', '狗狗game']:
            result = initialize(env, 'apps/' + name, language='c')
            metadata = json.loads((Path(result['project']) / 'package.json').read_text())
            self.assertEqual(metadata['name'], name)
            ids.append(metadata['id'])
            self.assertTrue(scope.check(self.ws)['passed'])
        self.assertEqual(len(set(ids)), 4)

    def test_p4_board_scaffold_uses_existing_backend_and_preserves_bringup_gate(self):
        api = self.board_workspace()
        before = (api / 'pxa_board_api.h').read_bytes()
        result = boards.initialize(self.ws, 'p4-custom', target='esp32p4')
        self.assertFalse(result['buildable'])
        folder = self.ws / 'firmware/boards/p4-custom'
        self.assertIn('PXA_BOARD_TARGET "esp32p4"', (folder / 'board.cmake').read_text())
        self.assertFalse(boards.inspect(self.ws, 'p4-custom')['static_ready'])
        self.assertEqual((api / 'pxa_board_api.h').read_bytes(), before)

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

    def test_context_uses_project_locked_release_and_refuses_missing_sdk(self):
        (self.app / 'pxa.lock').write_text('{"sdk":{"release":"0.4.0"}}')
        releases = self.root / 'releases'
        with self.assertRaisesRegex(ValueError, 'Locked SDK 0.4.0 is missing'):
            context.locked_sdk(self.sdk, self.app, releases)
        selected = releases / '0.4.0'
        (selected / 'bin').mkdir(parents=True)
        (selected / 'bin/pxa').touch()
        (selected / 'VERSION').write_text('0.4.0')
        self.assertEqual(context.locked_sdk(self.sdk, self.app, releases), selected)
        (selected / 'VERSION').write_text('0.3.0')
        with self.assertRaisesRegex(ValueError, 'missing'):
            context.locked_sdk(self.sdk, self.app, releases)
        (self.app / 'pxa.lock').write_text('{"sdk":{"release":"../../outside"}}')
        with self.assertRaisesRegex(ValueError, 'Invalid SDK release'):
            context.locked_sdk(self.sdk, self.app, releases)

    def test_extensions_are_searchable_and_protected_by_app_scope(self):
        header = self.sdk / 'extensions/peripherals/sdk/include/pxa-ext/vision.hpp'
        header.parent.mkdir(parents=True)
        header.write_text('struct FrameReader {};\n')
        example = self.sdk / 'extensions/peripherals/examples/camera/main.cpp'
        example.parent.mkdir(parents=True)
        example.write_text('// Camera example')
        value = context.sdk_context(self.sdk, 'FrameReader')
        self.assertEqual(value['matches'][0]['path'], 'extensions/peripherals/sdk/include/pxa-ext/vision.hpp')
        self.assertIn('extensions/peripherals/examples/camera/main.cpp', context.sdk_context(self.sdk, 'camera')['examples'])
        self.assertEqual(context.sdk_context(self.sdk, 'FrameReader', language='c')['matches'], [])
        self.start()
        header.write_text('struct ChangedContract {};\n')
        self.assertIn(str(header), scope.check(self.ws)['violations'])

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

    def test_context_prioritizes_exact_symbol_and_reads_current_headers(self):
        headers = self.sdk / 'sdk/guest-c/include'
        (headers / 'aaa.h').write_text('int pxa_ui_show_legacy(void);\n' * 20)
        value = context.sdk_context(self.sdk, 'pxa_ui_show', language='c', limit=1)
        self.assertTrue(value['matches'][0]['path'].endswith('/pxa_ui.h'))
        (headers / 'pxa_ui.h').write_text('int pxa_ui_show(int flags);\n')
        self.assertIn('int flags', context.sdk_context(self.sdk, 'pxa_ui_show', 'c', 1)['matches'][0]['excerpt'])
        inventory = context.sdk_context(self.sdk, language='c')
        self.assertEqual(inventory['matches'], [])
        self.assertEqual(inventory['header_count'], 2)
        self.assertEqual(len(inventory['headers']), 2)

    def test_context_finds_symbol_uses_in_unrelated_example_directory(self):
        example = self.sdk / 'sdk/guest-cpp/examples/timer/main.cpp'
        example.parent.mkdir(parents=True)
        example.write_text('// Button is a comment\nButton("Start").on_click(start);\n')
        result = context.sdk_context(self.sdk, 'pxa::ui::Button')
        self.assertIn(example.relative_to(self.sdk).as_posix(), result['examples'])
        self.assertEqual(result['example_matches'][0]['line'], 2)

    def test_verify_diagnostics_and_explicit_blank_viewport_check(self):
        spec = importlib.util.spec_from_file_location('verify_app', KIT / 'skills/pxa-debug/scripts/verify_app.py')
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        tree = {'schema': 'pxa-ui-inspection-1', 'complete': True, 'nodes': []}
        self.assertFalse(helper.ui_health(tree)['passed'])
        tree['nodes'] = [{'surface': 1, 'id': 2, 'type': 'text', 'text': 'Top-only app',
                          'visible': True, 'clip': {'width': 30, 'height': 10}}]
        self.assertTrue(helper.ui_health(tree)['passed'])
        tree['nodes'][0]['visible'] = False
        self.assertFalse(helper.ui_health(tree)['passed'])
        tree['nodes'][0].update(type='canvas', text=None, visible=True)
        self.assertTrue(helper.ui_health(tree)['passed'])
        self.assertFalse(helper.ui_health(tree)['heuristic'])
        self.assertEqual(helper.ui_diagnostics('guest PXA_UI_ERROR phase=mount code=-3\n'),
                         [{'phase': 'mount', 'code': -3}])
        record = {'steps': []}
        with self.assertRaisesRegex(RuntimeError, 'transport failure'):
            with helper.recorded_step(record, {'name': 'tap-start', 'action': 'tap'}):
                raise RuntimeError('transport failure')
        self.assertFalse(record['steps'][0]['passed'])
        self.assertEqual(record['steps'][0]['error'], 'transport failure')
        self.assertGreaterEqual(record['steps'][0]['elapsed_ms'], 0)

    def test_semantic_ui_query_flags_and_scenario_validation(self):
        from .ui import query_arguments
        self.assertIn('--text=-1 中文', query_arguments('tap', {'type': 'button', 'text': '-1 中文'}))
        values = query_arguments('wait-for', {'id': 4}, {'value': 0, 'enabled': False}, 1000)
        self.assertIn('false', values)
        self.assertIn('1000', values)
        for args in [('tap', {}, {}), ('expect', {'id': True}, {}),
                     ('expect', {'text': 'x'}, {'enabled': 'false'}),
                     ('expect', {'type': 'button'}, {'count': -1})]:
            with self.assertRaises(ToolError):
                query_arguments(*args)
        spec = importlib.util.spec_from_file_location('verify_ui_steps', KIT / 'skills/pxa-debug/scripts/verify_app.py')
        helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
        helper.validate_scenario({'pai-touch': [
            {'name': 'start', 'action': 'tap-node', 'selector': {'type': 'button', 'text': 'Start'}},
            {'name': 'done', 'action': 'wait-for', 'selector': {'text': 'Done'}, 'expected': {}, 'timeout_ms': 2000},
            {'name': 'zero', 'action': 'expect', 'selector': {'type': 'progress'}, 'expected': {'value': 0}}
        ]}, {'pai-touch': (296, 240)})

    def test_ui_inspection_pages_keep_the_complete_snapshot(self):
        from .ui import inspection_page
        tree = {'schema': 'pxa-ui-inspection-1', 'complete': True, 'renderer': 'lvgl',
                'viewport': {'width': 296, 'height': 240}, 'nodes': [{'id': n} for n in range(70)]}
        path = self.root / 'tree.json'
        first = inspection_page(tree, path)
        self.assertEqual(len(first['nodes']), 64)
        self.assertFalse(first['view_complete'])
        self.assertEqual(len(json.loads(path.read_text())['nodes']), 70)
        last = inspection_page(tree, path, offset=64)
        self.assertEqual(len(last['nodes']), 6)
        self.assertEqual(first['sha256'], last['sha256'])

    def test_verify_flags_preserve_definitions(self):
        from .cli import parser
        args = parser().parse_args(['verify', 'apps/timer', '--define', 'ACCELERATED=8', '--require-ui'])
        self.assertEqual(args.define, ['ACCELERATED=8'])
        self.assertTrue(args.require_ui)

    def test_doctor_distinguishes_simulator_from_hardware_validation(self):
        from .environment import Environment
        env = Environment(self.ws, {'engine': 'native', 'sdk': str(self.sdk), 'sdk_version': '0.9.0'})
        with patch('serial.tools.list_ports.comports', return_value=[]), patch.object(env, 'call', return_value='PXA 0.9.0'):
            result = env.doctor()
        self.assertEqual(result['devices'], [])
        self.assertFalse(result['physical_device_tested'])
        self.assertIn('simulator-only', result['device_guidance'])

    def test_qualified_cpp_query_deduplicates_adjacent_declarations(self):
        header = self.sdk / 'sdk/guest-cpp/include/pxa/ui.hpp'
        header.parent.mkdir(parents=True)
        header.write_text('class Button {\n Button();\n Button(int);\n};\n')
        result = context.sdk_context(self.sdk, 'pxa::ui::Button')
        self.assertEqual(len(result['matches']), 1)
        self.assertEqual(result['matches'][0]['line'], 1)
        self.assertEqual(result['matches'][0]['excerpt_start_line'], 1)

    def test_device_tools_use_current_kit_while_builds_keep_sdk_runtime(self):
        spec = importlib.util.spec_from_file_location('verify_app', KIT / 'skills/pxa-debug/scripts/verify_app.py')
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        pxa = self.sdk / 'bin/pxa'
        env = {'PXA_SIMULATOR_SOCKET_ROOT': '/tmp/owned', 'PYTHONPATH': '/extra'}
        args, variables = helper.device_command(pxa, ['device', 'info'], env)
        self.assertTrue(args[1].endswith('/pxadb.py'))
        self.assertEqual(args[2:], ['info'])
        current_client=args[1]
        for name in ('runtime/python/bin/python3', 'tools/pxadb/pxadb.py'):
            file = self.sdk / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.touch()
        args, variables = helper.device_command(pxa, ['device', 'info'], env)
        self.assertEqual(args[1],current_client)
        self.assertNotEqual(args[0],str(self.sdk/'runtime/python/bin/python3'))
        self.assertEqual(args[2:], ['info'])
        self.assertEqual(variables['PXA_SIMULATOR_SOCKET_ROOT'], '/tmp/owned')
        self.assertNotIn('PYTHONHOME', env)
        self.assertEqual(helper.device_command(pxa, ['build', 'app'], env)[0],
                         [str(self.sdk/'runtime/python/bin/python3'), '-m', 'pxa_tools', 'build', 'app'])

    def test_headless_container_job_passes_environment_inside_container(self):
        from .environment import Environment
        from .workbench import Jobs
        env = Environment(self.ws, {'engine': 'docker', 'image': 'sha256:test'})
        with patch.object(env, 'invocation', return_value=['docker', 'run', '--rm', 'sha256:test', 'run']), \
             patch('pxa_agent.workbench.subprocess.Popen') as popen, \
             patch('pxa_agent.workbench.threading.Thread'), \
             patch.object(Jobs, 'status', return_value={}):
            Jobs(env).start(['run'], headless=True)
        command = popen.call_args.args[0]
        self.assertLess(command.index('SDL_VIDEODRIVER=dummy'), command.index('sha256:test'))
        self.assertLess(command.index('SDL_AUDIODRIVER=dummy'), command.index('sha256:test'))

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

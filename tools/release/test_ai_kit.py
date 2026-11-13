"""Behavioral checks for optional AI-kit installation and evidence validation."""
from __future__ import annotations

import importlib.util
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tarfile
import tomllib
import unittest
from unittest.mock import patch

SYSTEM = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


installer = load('pxa_ai_install', SYSTEM / 'ai/install.py')
debug = load('pxa_ai_debug', SYSTEM / 'ai/skills/pxa-debug/scripts/verify_app.py')
sys.path.insert(0, str(SYSTEM / 'tools/release'))
release = load('pxa_ai_release', SYSTEM / 'tools/release/build_ai_kit.py')


class ReleaseInputs(unittest.TestCase):
    def test_ignored_virtualenv_is_excluded_and_tracked_symlinks_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'system'
            (source / 'ai').mkdir(parents=True)
            (source / 'ai/plugin.json').write_text('{"version":"0.7.0"}')
            (source / 'ai/agent.py').write_text('# public source\n')
            (source / 'VERSION').write_text('0.11.0\n')
            (source / 'LICENSE').write_text('MIT\n')
            (source / '.gitignore').write_text('/ai/.venv/\n')
            tools = root / 'tools/pxadb'
            tools.mkdir(parents=True)
            for name in ('pxadb.py', 'pxa_local_socket.py', 'ui_query.py', 'package_compatibility.py', 'interaction_contract.py'):
                (tools / name).write_text('# host client\n')
            (root / 'tools/simulator_pxadb.py').write_text('# simulator client\n')
            (root / '.gitignore').write_text('/system/\n')
            def git(repo, *args):
                subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
            for repo in (root, source):
                git(repo, 'init', '-q')
                git(repo, 'add', '.')
                git(repo, '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture')
            outside = root / 'private-python'
            outside.write_text('private environment content')
            virtualenv = source / 'ai/.venv/bin'
            virtualenv.mkdir(parents=True)
            (virtualenv / 'python').symlink_to(outside)
            key = root / 'distribution.pem'
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt',
                            'ec_paramgen_curve:P-256', '-out', str(key)], check=True, capture_output=True)
            output = root / 'release'
            manifest = release.build(source, output, key, workspace=root)
            self.assertFalse(manifest['source']['dirty'])
            with tarfile.open(output / manifest['archive']['file']) as archive:
                names = archive.getnames()
                self.assertTrue(any(name.endswith('/agent.py') for name in names))
                self.assertFalse(any('.venv' in name or 'private-python' in name for name in names))
                self.assertFalse(any(member.issym() or member.islnk() for member in archive.getmembers()))
            from assemble import copy_tracked_tree
            sdk_ai = root / 'sdk-ai'
            copy_tracked_tree(source, 'ai', sdk_ai)
            self.assertEqual({p.name for p in sdk_ai.iterdir()}, {'plugin.json', 'agent.py'})
            (source / 'ai/shortcut').symlink_to(outside)
            git(source, 'add', 'ai/shortcut')
            git(source, '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'tracked link')
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                release.build(source, root / 'rejected', key, workspace=root)
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                copy_tracked_tree(source, 'ai', root / 'rejected-sdk')


class Installation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='pxa-ai-tests-')
        self.project = Path(self.temporary.name) / 'Project with spaces'
        self.project.mkdir()
        (self.project / 'AGENTS.md').write_text('Existing project guidance')
        (self.project / '.codex').mkdir()
        (self.project / '.codex/config.toml').write_text('model = "user-choice"\n')

    def tearDown(self):
        self.temporary.cleanup()

    def invoke(self, action, *arguments, expected=0):
        command = [sys.executable, str(SYSTEM / 'ai/install.py'), action, '--project', str(self.project), *arguments]
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, expected, result.stderr)
        return json.loads(result.stdout) if result.returncode == 0 else result.stderr

    def test_selected_skill_and_idempotent_install(self):
        self.invoke('install', '--skill', 'pxa-debug')
        self.assertEqual([p.name for p in (self.project / '.agents/skills').iterdir()], ['pxa-debug'])
        self.assertEqual(self.invoke('install', '--skill', 'pxa-debug')['changed'], [])
        self.assertEqual((self.project / 'AGENTS.md').read_text(), 'Existing project guidance')
        self.assertEqual((self.project / '.codex/config.toml').read_text(), 'model = "user-choice"\n')

    def test_dry_run_and_agents(self):
        self.invoke('install', '--agents', '--dry-run')
        self.assertFalse((self.project / '.agents').exists())
        self.invoke('install', '--agents')
        agents = list((self.project / '.codex/agents').glob('*.toml'))
        self.assertEqual({agent.stem for agent in agents},
                         {'pxa-developer', 'pxa-tester', 'pxa-board-developer'})
        for agent in agents:
            definition = tomllib.loads(agent.read_text())
            self.assertIn('developer_instructions', definition)
            self.assertNotIn('model', definition)
            self.assertNotIn('sandbox_mode', definition)

    def test_conflict_prevents_partial_install(self):
        path = self.project / '.agents/skills/pxa-release/SKILL.md'
        path.parent.mkdir(parents=True)
        path.write_text('User-owned skill')
        self.invoke('install', '--agents', expected=1)
        self.assertEqual(path.read_text(), 'User-owned skill')
        self.assertFalse((self.project / '.agents/skills/pxa-app-dev').exists())
        self.assertFalse((self.project / installer.RECEIPT).exists())

    def test_local_edits_survive_update_and_remove(self):
        self.invoke('install')
        path = self.project / '.agents/skills/pxa-debug/SKILL.md'
        path.write_text('User edit')
        self.invoke('install', '--update', expected=1)
        self.invoke('remove', expected=1)
        self.assertEqual(path.read_text(), 'User edit')
        self.assertTrue((self.project / '.agents/skills/pxa-app-dev/SKILL.md').exists())

    def test_owned_old_content_requires_explicit_update(self):
        self.invoke('install', '--skill', 'pxa-debug')
        name = '.agents/skills/pxa-debug/SKILL.md'
        path = self.project / name
        path.write_bytes(b'Previous unmodified kit')
        receipt_path = self.project / installer.RECEIPT
        receipt = json.loads(receipt_path.read_text())
        receipt['files'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
        receipt_path.write_text(json.dumps(receipt))
        self.invoke('install', '--skill', 'pxa-debug', expected=1)
        self.assertEqual(path.read_bytes(), b'Previous unmodified kit')
        self.invoke('install', '--skill', 'pxa-debug', '--update')
        self.assertEqual(path.read_bytes(), (SYSTEM / 'ai/skills/pxa-debug/SKILL.md').read_bytes())

    def test_remove_preserves_unrelated_files(self):
        self.invoke('install', '--agents')
        note = self.project / '.agents/skills/pxa-debug/my-note.txt'
        note.write_text('User note')
        self.invoke('remove')
        self.assertEqual(note.read_text(), 'User note')
        self.assertFalse((self.project / installer.RECEIPT).exists())
        self.assertTrue((self.project / '.codex/config.toml').exists())

    def test_symlink_and_malicious_receipt_rejected(self):
        outside = Path(self.temporary.name) / 'outside'
        outside.mkdir()
        (self.project / '.agents').symlink_to(outside, target_is_directory=True)
        self.invoke('install', expected=1)
        self.assertEqual(list(outside.iterdir()), [])
        (self.project / '.agents').unlink()
        receipt = self.project / installer.RECEIPT
        receipt.parent.mkdir()
        receipt.write_text(json.dumps({'files': {'../outside/user.txt': 'bad'}}))
        self.invoke('remove', expected=1)

    def test_write_failure_rolls_back(self):
        first = '.agents/skills/pxa-debug/first.txt'
        second = '.agents/skills/pxa-debug/second.txt'
        path = installer.destination(self.project, first)
        path.parent.mkdir(parents=True)
        path.write_bytes(b'old')
        original = installer.atomic_write
        def fail_second(target, data):
            if target.name == 'second.txt':
                raise OSError('simulated disk error')
            original(target, data)
        with patch.object(installer, 'atomic_write', side_effect=fail_second):
            with self.assertRaises(OSError):
                installer.apply(self.project, {first: b'new', second: b'other'})
        self.assertEqual(path.read_bytes(), b'old')
        self.assertFalse((self.project / second).exists())


class Scenarios(unittest.TestCase):
    dimensions = {name: (296, 240) for name in debug.PROFILES}

    def test_inputs(self):
        debug.validate_scenario({'pai-touch': [
            {'name': 'tap', 'action': 'tap', 'x': 148, 'y': 140},
            {'name': 'swipe', 'action': 'swipe', 'x': 10, 'y': 10, 'x2': 20, 'y2': 20, 'ms': 100},
            {'name': 'wait', 'action': 'wait', 'ms': 0},
            {'name': 'shot', 'action': 'screenshot'}]}, self.dimensions)

    def test_unsafe_or_unbounded_inputs(self):
        cases = [[], {'unknown': []}, {'pai-touch': [{'name': '../escape', 'action': 'screenshot'}]},
                 {'pai-touch': [{'name': 'before', 'action': 'screenshot'}]},
                 {'pai-touch': [{'name': 'tap', 'action': 'tap', 'x': 296, 'y': 0}]},
                 {'pai-touch': [{'name': 'tap', 'action': 'tap', 'x': True, 'y': 0}]},
                 {'pai-touch': [{'name': 'wait', 'action': 'wait', 'ms': 5001}]},
                 {'pai-touch': [{'name': 'run', 'action': 'shell', 'command': 'anything'}]},
                 {'pai-touch': [{'name': 'shot', 'action': 'screenshot', 'output': '/tmp/other'}]}]
        for scenario in cases:
            with self.subTest(scenario=scenario), self.assertRaises(ValueError):
                debug.validate_scenario(scenario, self.dimensions)


class ScreenProfile(unittest.TestCase):
    def test_store_capture_uses_rectangle_without_changing_profile(self):
        sys.path.insert(0, str(SYSTEM / 'tools'))
        from pxa_tools import run
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            profiles = root / 'share/pxa/profiles'
            profiles.mkdir(parents=True)
            path = profiles / 'test.toml'
            for shape in ('corner_radius=58', 'round=true'):
                with self.subTest(shape=shape):
                    original = ('[desktop]\nwidth=296\nheight=240\ndensity_dpi=240\n'
                                'safe_insets=[8,10,8,10]\ngestures=true\n' + shape + '\n')
                    path.write_text(original)
                    with patch.object(run, 'SDK_ROOT', root):
                        normal = run.profile_options('test')
                        capture = run.profile_options('test', store_screenshots=True)
                    self.assertIn('--gestures', normal)
                    self.assertNotIn('--no-navigation-bar', normal)
                    self.assertNotIn('--round', capture)
                    self.assertNotIn('--gestures', capture)
                    self.assertIn('--no-navigation-bar', capture)
                    for option, value in (('--width', '296'), ('--height', '240'),
                                          ('--density-dpi', '240'), ('--corner-radius', '0'),
                                          ('--safe-insets', '0,0,0,0')):
                        self.assertEqual(capture[capture.index(option) + 1], value)
                    self.assertEqual(path.read_text(), original)

    def test_store_capture_flag_and_device_rejection(self):
        sys.path.insert(0, str(SYSTEM / 'tools'))
        from pxa_tools.cli import parser
        from pxa_tools.common import ToolError
        from pxa_tools.run import run
        from types import SimpleNamespace
        self.assertTrue(parser().parse_args(['run', '--store-screenshots']).store_screenshots)
        self.assertFalse(parser().parse_args(['run']).store_screenshots)
        with self.assertRaisesRegex(ToolError, 'real PXA simulator'):
            run(None, SimpleNamespace(target='simulator', device='/dev/test', store_screenshots=True))

    def test_verifier_forwards_store_flag_and_disables_profiling(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = root / 'app'
            project.mkdir()
            (project / 'package.json').write_text('{"id":"example"}')
            pxa = root / 'sdk/bin/pxa'
            pxa.parent.mkdir(parents=True)
            pxa.touch()
            profiles = root / 'sdk/share/pxa/profiles'
            profiles.mkdir(parents=True)
            for profile in debug.PROFILES:
                (profiles / (profile + '.toml')).write_text('[desktop]\nwidth=296\nheight=240\n')
            output = root / 'evidence'
            prepared=project/'dist/example.pxa';prepared.parent.mkdir()
            prepared.write_bytes(b'prepared-package')
            (project/'dist/example.pxa.build.json').write_text(json.dumps({
                'schema':'pxa-build-2','sha256':hashlib.sha256(prepared.read_bytes()).hexdigest(),
                'configuration':{'type':'Debug','definitions':[]},'targets':['simulator']}))
            args = ['verify_app.py', str(project), '--pxa', str(pxa), '--output', str(output),
                    '--profiles', 'pai-touch', '--store-screenshots']
            completed = subprocess.CompletedProcess([], 0, 'PXA test --no-build --simulator-state --expected-package-sha256', '')
            with patch.object(sys, 'argv', args), patch.dict(os.environ, PXA_SIMULATOR_PERF='1'), \
                 patch.object(debug.subprocess, 'run', return_value=completed), \
                 patch.object(debug.subprocess, 'Popen', side_effect=RuntimeError('test launch boundary')) as launch:
                self.assertEqual(debug.main(), 1)
            self.assertIn('--store-screenshots', launch.call_args.args[0])
            self.assertNotIn('PXA_SIMULATOR_PERF', launch.call_args.kwargs['env'])
            self.assertEqual(launch.call_args.kwargs['env']['PXA_SIM_NAV_MODE'], 'gesture-no-bar')
            result = json.loads((output / 'result.json').read_text())
            self.assertEqual(result['capture']['display_shape'], 'rectangle')
            self.assertEqual(result['capture']['source'], 'real-pxa-simulator')
            self.assertFalse(result['capture']['post_processed'])

    def test_theme_is_passed_to_native_simulator(self):
        sys.path.insert(0, str(SYSTEM / 'tools'))
        from pxa_tools.run import profile_options
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            profiles = root / 'share/pxa/profiles'
            profiles.mkdir(parents=True)
            path = profiles / 'test.toml'
            from pxa_tools import run
            with patch.object(run, 'SDK_ROOT', root):
                path.write_text('[desktop]\nwidth=296\nheight=240\ntheme="dark"\n')
                self.assertIn('--dark', profile_options('test'))
                path.write_text('[desktop]\nwidth=296\nheight=240\ntheme="light"\n')
                self.assertNotIn('--dark', profile_options('test'))


if __name__ == '__main__':
    unittest.main()

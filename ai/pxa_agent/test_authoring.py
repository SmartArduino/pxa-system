"""Guidance availability and saved-state verification across app relaunch."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from .authoring import authoring_context
from .common import KIT
from .context import project_context


spec = importlib.util.spec_from_file_location('verify_authoring', KIT / 'skills/pxa-debug/scripts/verify_app.py')
debug = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debug)


class AuthoringTest(unittest.TestCase):
    def test_project_context_provides_readable_guidance_without_changing_project(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            sdk, app = root / 'sdk', root / 'app'
            (sdk / 'sdk/guest-c/include').mkdir(parents=True)
            (sdk / 'VERSION').write_text('0.10.0')
            app.mkdir()
            metadata = b'{"id":"pxa-example","version":"1.0.0"}'
            (app / 'package.json').write_bytes(metadata)
            env = SimpleNamespace(native=True, config={'sdk': sdk}, project=lambda path: app)
            result = project_context(env, 'app', language='c')
            self.assertEqual(result['authoring'], authoring_context())
            for path in result['authoring']['references'].values():
                self.assertTrue(Path(path).is_relative_to(KIT / 'skills'))
                self.assertTrue(Path(path).read_text().strip())
            self.assertEqual(list(app.iterdir()), [app / 'package.json'])
            self.assertEqual((app / 'package.json').read_bytes(), metadata)

    def test_relaunch_preserves_saved_progress_and_selects_only_verified_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            save = root / 'state/tutorial.json'
            save.parent.mkdir()
            save.write_text('{"movement":true}')
            state = {'hint_visible': False, 'active': True}
            calls = []

            def command(directory, name, arguments, **kwargs):
                calls.append(arguments)
                if arguments[1:3] == ['package', 'stop']:
                    state.update(active=False, hint_visible=True)
                elif arguments[1:3] == ['package', 'run']:
                    state.update(active=True, hint_visible=not json.loads(save.read_text())['movement'])
                else:
                    return 0, 'publisher:pxa-game\tactive=1\nother:pxa-other\tactive=0\n'
                return 0, 'accepted'

            result = debug.relaunch_guest(command, root / 'reopen', 'pai-touch@owned',
                                         'publisher:pxa-game', variables={})
            self.assertTrue(state['active'])
            self.assertFalse(state['hint_visible'])
            self.assertTrue(json.loads(save.read_text())['movement'])
            self.assertFalse(result['data_cleared'])
            self.assertEqual([args[2] for args in calls], ['stop', 'run', 'list'])
            self.assertTrue(all(args[-2:] == ['--simulator', 'pai-touch@owned'] for args in calls))
            self.assertTrue(all(args[3] == 'publisher:pxa-game' for args in calls[:2]))

    def test_relaunch_errors_do_not_retry_or_run_after_failed_stop(self):
        with tempfile.TemporaryDirectory() as folder:
            calls = []

            def fail(directory, name, arguments, **kwargs):
                calls.append(arguments)
                raise RuntimeError('transport failure')

            with self.assertRaisesRegex(RuntimeError, 'transport failure'):
                debug.relaunch_guest(fail, Path(folder) / 'reopen', 'pai-touch@owned',
                                     'publisher:pxa-game', variables={})
            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][2], 'stop')

    def test_ambiguous_inactive_or_changed_identity_cannot_pass(self):
        for packages in ('publisher:pxa-game\tactive=0', 'publisher:pxa-game\tactive=10',
                         'publisher:pxa-game\tactive=1\nother:pxa-game\tactive=1'):
            with self.subTest(packages=packages), self.assertRaises(RuntimeError):
                debug.active_identity(packages, 'pxa-game')
        with tempfile.TemporaryDirectory() as folder:
            def command(*args, **kwargs):
                return 0, 'other:pxa-game\tactive=1'
            with self.assertRaisesRegex(RuntimeError, 'changed the active package'):
                debug.relaunch_guest(command, Path(folder) / 'reopen', 'pai-touch@owned',
                                     'publisher:pxa-game', variables={})

    def test_relaunch_cannot_target_an_external_package_or_override_state(self):
        step = {'name': 'reopen', 'action': 'relaunch'}
        dimensions = {'pai-touch': (296, 240)}
        debug.validate_scenario({'pai-touch': [step]}, dimensions)
        for fields in ({'identity': 'other-app'}, {'state': '/other'}, {'clear_data': True}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                debug.validate_scenario({'pai-touch': [{**step, **fields}]}, dimensions)
        from .simulator_batch import validate
        with self.assertRaises(ValueError):
            validate([step], 296, 240)

    def test_recipe_journeys_are_accepted_by_the_verifier(self):
        from .recipes import create
        from .test_recipes import RecipeEnvironment
        with tempfile.TemporaryDirectory() as folder:
            env = RecipeEnvironment(Path(folder))
            for recipe in ('counter', 'timer', 'reaction'):
                created = create(env, 'apps/' + recipe, recipe)
                journey = json.loads(Path(created['scenario']).read_text())
                debug.validate_scenario(journey, {profile: (480, 480) for profile in journey})


if __name__ == '__main__':
    unittest.main()

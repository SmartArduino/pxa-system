import json
from pathlib import Path
import tempfile
import unittest

from .common import ToolError
from .recipes import create


class RecipeEnvironment:
    def __init__(self, root):
        self.workspace = root / 'workspace'
        self.workspace.mkdir()
        sdk = root / 'sdk'
        sdk.mkdir()
        (sdk / 'VERSION').write_text('0.10.0')
        self.config = {'engine': 'native', 'sdk': str(sdk), 'sdk_version': '0.10.0'}
        self.calls = []

    def project(self, path):
        path = Path(path)
        return (self.workspace / path).resolve() if not path.is_absolute() else path

    def call(self, arguments):
        self.calls.append(arguments)
        path = Path(arguments[1])
        path.mkdir(parents=True, exist_ok=True)
        (path / 'package.json').write_text(json.dumps({'id': arguments[3], 'components': [{}]}))
        (path / 'main.cpp').write_text('base template')
        return 'created'


class RecipeTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.env = RecipeEnvironment(Path(self.temporary.name))

    def test_selected_old_sdk_rejected_without_files_or_migration(self):
        self.env.config['sdk_version'] = '0.9.0'
        with self.assertRaisesRegex(ToolError, 'explicit workspace migration'):
            create(self.env, 'apps/timer', 'timer')
        self.assertFalse(self.env.calls)
        self.assertFalse((self.env.workspace / 'apps').exists())

    def test_populated_app_and_lock_retained(self):
        path = self.env.project('apps/existing')
        path.mkdir(parents=True)
        for filename in ('main.cpp', 'pxa.lock', 'signer.pem'):
            (path / filename).write_text('original')
        with self.assertRaises(ToolError):
            create(self.env, 'apps/existing', 'counter')
        self.assertTrue(all(p.read_text() == 'original' for p in path.iterdir()))

    def test_parameter_validation_before_initializing(self):
        for recipe, arguments in [('timer', {'minutes': 0}), ('timer', {'minutes': True}),
                                  ('counter', {'minutes': 5}), ('timer', {'title': 'bad\nname'})]:
            with self.subTest(recipe=recipe, arguments=arguments), self.assertRaises(ToolError):
                create(self.env, 'apps/new', recipe, **arguments)
        self.assertFalse(self.env.calls)

    def test_escaped_title_services_and_journey_are_generated_without_clock_acceleration(self):
        title = '计时 "A" \\ B'
        result = create(self.env, 'apps/timer', 'timer', title=title, minutes=3, break_minutes=1)
        path = self.env.project('apps/timer')
        code = (path / 'main.cpp').read_text()
        self.assertIn('Text(' + json.dumps(title, ensure_ascii=False) + ')', code)
        self.assertIn('rest ? 1 : 3', code)
        self.assertIn('timer.test.snapshot.v1', code)
        metadata = json.loads((path / 'package.json').read_text())
        self.assertIn('storage', metadata['components'][0]['services'])
        scenario = json.loads((path / 'tests/journey.json').read_text())
        self.assertIn('espp4-a', scenario)
        self.assertFalse(result['runtime_verified'])
        self.assertEqual(metadata['name'], title)
        self.assertNotIn('clock-scale', json.dumps(scenario))


if __name__ == '__main__':
    unittest.main()

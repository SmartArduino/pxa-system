"""Listing preflight and publication preserve exact packages and media choices."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image
from . import environment, store
from .common import ToolError

SPEC = {'schema': 'pxa-publishing-spec-1',
        'taxonomy': {'kinds': [{'value': 'game'}, {'value': 'app'}],
                     'categories': [{'kind': 'game', 'value': 'puzzle'}]},
        'media': {'formats': ['PNG', 'JPEG', 'WEBP'], 'max_pixels': 20000000,
                  'icon': {'recommended_width': 512, 'recommended_height': 512, 'max_bytes': 1000000},
                  'cover': {'recommended_width': 1280, 'recommended_height': 960, 'max_bytes': 1000000},
                  'screenshot': {'max_count': 8, 'max_bytes': 1000000}}}


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.project = self.root / 'game'
        self.project.mkdir()
        self.env = environment.Environment(self.root, {'engine': 'native', 'sdk': '/unused'})

    def tearDown(self):
        self.tmp.cleanup()

    def metadata(self, value):
        (self.project / 'store.json').write_text(json.dumps(value))

    @patch.object(store, 'api', return_value=SPEC)
    def test_provided_cover_skip_icon_and_normalized_taxonomy(self, api):
        self.metadata({'kind': ' Game ', 'category': 'puzzle', 'tags': ['益智', '#PXA', 'pxa'],
                       'cover': 'cover.png', 'icon': None})
        Image.new('RGB', (1280, 960)).save(self.project / 'cover.png')
        prepared = store.prepare(self.env, self.project)
        self.assertEqual(prepared['listing']['tags'], ['益智', 'PXA'])
        self.assertEqual(prepared['omitted'], ['icon'])
        self.assertEqual(prepared['needs_choice'], [])
        self.assertEqual(prepared['images'][0]['kind'], 'cover')
        self.assertEqual(prepared['warnings'], [])
        self.assertEqual(api.call_count, 1)  # GET only, no publication or upload.

    @patch.object(store, 'api', return_value=SPEC)
    def test_missing_choices_and_skip_conflict(self, api):
        self.assertEqual(store.prepare(self.env, self.project)['needs_choice'], ['icon', 'cover'])
        self.assertEqual(store.prepare(self.env, self.project, skip_icon=True, skip_cover=True)['omitted'], ['icon', 'cover'])
        with self.assertRaisesRegex(ToolError, 'both upload and skip'):
            store.prepare(self.env, self.project, icon='icon.png', skip_icon=True)

    @patch.object(store, 'api', return_value=SPEC)
    def test_all_media_decode_and_scope_checked_before_writes(self, api):
        good = self.project / 'icon.png'
        Image.new('RGB', (512, 512)).save(good)
        bad = self.project / 'bad.png'
        bad.write_bytes(b'invalid image')
        with patch.object(store, 'upload') as upload:
            with self.assertRaisesRegex(ToolError, 'decode'):
                store.prepare(self.env, self.project, icon=good, cover=bad)
            upload.assert_not_called()
        with self.assertRaisesRegex(ToolError, 'inside'):
            store.prepare(self.env, self.project, cover=self.root.parent / 'escape.png')

    @patch.object(store, 'api', return_value=SPEC)
    def test_invalid_tags_category_and_screenshot_count(self, api):
        for value in [{'tags': ['x' * 25]}, {'tags': ['a\nb']}, {'kind': 'app', 'category': 'puzzle'}]:
            self.metadata(value)
            with self.assertRaises(ToolError):
                store.prepare(self.env, self.project)
        self.metadata({})
        with self.assertRaisesRegex(ToolError, 'Too many'):
            store.prepare(self.env, self.project, screenshots=['screen.png'] * 9)

    @patch.object(store, 'api', return_value=SPEC)
    def test_partial_listing_does_not_replace_existing_game_kind(self, api):
        self.metadata({'category': 'puzzle'})
        prepared = store.prepare(self.env, self.project, skip_icon=True, skip_cover=True)
        self.assertEqual(prepared['listing'], {'category': 'puzzle'})


if __name__ == '__main__':
    unittest.main()

"""A release upgrade must preserve the old device admission contract."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import metadata
from metadata import outputs


class MetadataTests(unittest.TestCase):
    def test_browser_admission_uses_registered_versions_and_keeps_backend_features(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root/'VERSION').write_text('0.10.0\n')
            (root/'idf_component.yml').write_text('version: "0.10.0"\n')
            browser = root/'simulator/browser/capabilities.json'
            browser.parent.mkdir(parents=True)
            browser.write_text(json.dumps({'version': '0.10.1', 'compatibility': {
                'services': [{'service_id': 1, 'major': 2, 'minor': 2, 'features': 0},
                             {'service_id': 2, 'major': 0, 'minor': 4, 'features': 0},
                             {'service_id': 9, 'major': 0, 'minor': 6, 'features': 512}],
                'permissions': ['net.socket']}}))
            with patch.object(metadata, 'ROOT', root):
                generated = json.loads(outputs()[browser])
            self.assertEqual(generated['version'], '0.10.1')
            services = {s['service_id']: s for s in generated['compatibility']['services']}
            self.assertEqual((services[1]['major'], services[1]['minor']), (0, 2))
            self.assertEqual((services[2]['major'], services[2]['minor']), (0, 5))
            self.assertEqual((services[9]['major'], services[9]['minor']), (0, 8))
            self.assertEqual(services[9]['features'], 512)
            self.assertEqual(generated['compatibility']['permissions'], ['net.socket'])

    def test_common_ui_capabilities_include_shipped_system_glyphs(self):
        with tempfile.TemporaryDirectory() as td:
            backend = Path(td)
            document = json.loads(outputs(backend=backend)[backend/'app/pxa-capabilities.json'])
            ui = next(service for service in document['services'] if service['service_id'] == 3)
            self.assertGreaterEqual((ui['major'], ui['minor']), (0, 10))
            self.assertTrue(ui['features'] & (1 << 15))

    def test_common_game_capabilities_include_shipped_borrowed_blit(self):
        with tempfile.TemporaryDirectory() as td:
            backend=Path(td)
            result=outputs(backend=backend)
            document=json.loads(result[backend/'app/pxa-capabilities.json'])
            game=next(service for service in document['services'] if service['service_id']==18)
            self.assertEqual(game['features'],17)

    def test_previous_baseline_is_archived_unchanged(self):
        with tempfile.TemporaryDirectory() as td:
            backend = Path(td)
            current = backend / 'app/pxa-capabilities.json'
            current.parent.mkdir()
            previous = {'schema': 'pxa-host-baseline-1', 'release': '0.1.0',
                        'core': [2, 0], 'services': [{'service_id': 5, 'features': 65}]}
            current.write_text(json.dumps(previous))
            result = outputs(backend=backend)
            snapshot = backend / 'app/pxa-host-baselines/0.1.0.json'
            self.assertEqual(json.loads(result[snapshot]), previous)
            self.assertNotEqual(json.loads(result[current])['release'], '0.1.0')
            snapshot.parent.mkdir()
            snapshot.write_text(json.dumps({**previous, 'core': [99, 0]}))
            with self.assertRaisesRegex(ValueError, 'historical Host capabilities'):
                outputs(backend=backend)


if __name__ == '__main__':
    unittest.main()

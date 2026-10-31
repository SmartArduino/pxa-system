"""A release upgrade must preserve the old device admission contract."""
import json
from pathlib import Path
import tempfile
import unittest
from metadata import outputs


class MetadataTests(unittest.TestCase):
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

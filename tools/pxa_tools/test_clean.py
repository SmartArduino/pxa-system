import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from .clean import clean
from .common import ToolError, identity


class CleanTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = SimpleNamespace(path=self.root, metadata={'id': 'pxa-counter'})

    def put(self, name, content=b'preserve'):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def test_current_receipt_and_aliases_retained_even_keep_zero(self):
        content, alias_content = 'a' * 64, 'b' * 64
        configuration = {'type': 'Debug', 'definitions': []}
        current = self.put('.pxa/build/' + identity(configuration)[:20] + '/compiled.o')
        package = self.put('.pxa/cache/packages/' + content + '/package/main.wasm')
        alias = self.put('dist/.pxa-counter-' + alias_content[:20] + '/main.wasm')
        (self.root / 'dist/pxa-counter').symlink_to(alias.parent.name, target_is_directory=True)
        old = self.put('dist/.pxa-counter-' + 'c' * 20 + '/main.wasm')
        old_build = self.put('.pxa/build/' + 'd' * 20 + '/compiled.o')
        old_package = self.put('.pxa/cache/packages/' + 'e' * 64 + '/package/main.wasm')
        self.put('dist/pxa-counter.pxa.build.json', json.dumps({
            'content_id': content, 'configuration': configuration}).encode())
        saved = [self.put(name) for name in ('main.cpp', 'pxa.lock', 'signing.pem',
            '.pxa/simulator/pai-touch/save.dat', '.pxa/build/user-notes/keep.txt', 'dist/pxa-counter.pxa')]
        preview = clean(self.project, keep=0, dry_run=True)
        self.assertTrue(preview['entries'])
        self.assertTrue(all(path.is_file() for path in (old, old_build, old_package)))
        clean(self.project, keep=0)
        self.assertTrue(all(path.read_bytes() == b'preserve' for path in (current, package, alias, *saved)))
        self.assertTrue(all(not path.exists() for path in (old, old_build, old_package)))

    def test_keeps_recent_configurations(self):
        paths = [self.put('.pxa/build/' + char * 20 + '/file') for char in 'abc']
        for index, path in enumerate(paths):
            os.utime(path.parent, ns=(index + 1, index + 1))
        clean(self.project, keep=2)
        self.assertFalse(paths[0].exists())
        self.assertTrue(paths[1].exists() and paths[2].exists())

    def test_malformed_receipt_stops_before_deletion(self):
        old = self.put('.pxa/build/' + 'a' * 20 + '/file')
        self.put('dist/counter.pxa.build.json', b'broken')
        with self.assertRaises(ToolError):
            clean(self.project, keep=0)
        self.assertTrue(old.is_file())

    def test_linked_cache_is_not_followed(self):
        outside = self.put('outside/keep.txt')
        (self.root / '.pxa').mkdir()
        (self.root / '.pxa/cache').symlink_to(outside.parent, target_is_directory=True)
        with self.assertRaises(ToolError):
            clean(self.project, keep=0)
        self.assertEqual(outside.read_bytes(), b'preserve')

    def test_aot_cleanup_is_opt_in_and_keeps_lock_and_other_files(self):
        aot = self.put('.pxa/cache/aot/' + 'a' * 64 + '.aot')
        lock = self.put('.pxa/cache/aot/' + 'a' * 64 + '.lock')
        notes = self.put('.pxa/cache/aot/user.aot')
        clean(self.project, keep=0)
        self.assertTrue(aot.is_file())
        clean(self.project, keep=0, aot=True)
        self.assertFalse(aot.exists())
        self.assertTrue(lock.is_file() and notes.is_file())


if __name__ == '__main__':
    unittest.main()

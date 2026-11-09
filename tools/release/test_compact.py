from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import assemble
from compact import (compact_libraries, compact_python, compact_toolchain,
                     deduplicate_binaries, payload_size, remove_payload)


class CompactTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def put(self, relative, value=b'payload'):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)
        return path

    def toolchain(self):
        for directory in ('include/c++/v1', 'include/wasm32-wasip1', 'lib/wasm32-wasip1'):
            self.put('share/wasi-sysroot/' + directory + '/keep')
        for name in ('clang', 'clang++', 'clang.cfg', 'clang++.cfg', 'wasm-ld',
                     'llvm-ar', 'llvm-ranlib', 'llvm-nm', 'llvm-objdump', 'llvm-size',
                     'llvm-objcopy', 'llvm-strip', 'llvm-symbolizer'):
            self.put('bin/' + name)
        (self.root / 'lib').mkdir()

    def test_preview1_notices_and_aliases_survive(self):
        self.toolchain()
        self.put('bin/clang-23')
        (self.root / 'bin/clang').unlink()
        (self.root / 'bin/clang').symlink_to('clang-23')
        removed = self.put('share/wasi-sysroot/lib/wasm32-wasip2/libc.a')
        notice = self.put('share/wasi-sysroot/lib/wasm32-wasip2/LICENSE.txt', b'upstream notice')
        self.put('bin/clang-tidy')
        result = compact_toolchain(self.root)
        self.assertFalse(removed.exists())
        self.assertEqual(notice.read_bytes(), b'upstream notice')
        self.assertFalse((self.root / 'bin/clang-tidy').exists())
        self.assertEqual((self.root / 'bin/clang').read_bytes(), b'payload')
        self.assertEqual(result['targets'], ['wasm32-wasip1'])
        self.assertTrue((self.root / 'share/wasi-sysroot/lib/wasm32-wasip1/keep').is_file())

    def test_full_preserves_optional_tools(self):
        self.toolchain()
        path = self.put('bin/clang-tidy', b'optional')
        compact_toolchain(self.root, profile='full')
        self.assertEqual(path.read_bytes(), b'optional')

    def test_linux_assembler_refuses_existing_release_assets(self):
        self.put('workspace/deps/pxa-system/VERSION', b'1.2.3\n')
        output = self.root / 'output'
        sentinel = self.put('output/pxa-toolchain-1.2.3-linux-x86_64.tar.gz', b'immutable release')
        arguments = ['assemble.py', '--workspace', str(self.root / 'workspace'),
                     '--output', str(output), '--simulator', 'unused', '--wamrc', 'unused',
                     '--wasi', 'unused', '--signing-key', 'unused']
        with patch('sys.argv', arguments), patch.object(assemble, 'run', return_value=''):
            with self.assertRaisesRegex(RuntimeError, 'already exist'):
                assemble.main()
        self.assertEqual(sentinel.read_bytes(), b'immutable release')

    def test_python_keeps_runtime_and_sysconfig(self):
        discarded = self.put('lib/python3.12/config-test/libpython3.12.a')
        retained = [self.put(name) for name in ('lib/python3.12/encodings/utf_8.py',
            'lib/python3.12/lib-dynload/_ssl.so', 'lib/python3.12/_sysconfigdata_test.py',
            'lib/python3.12/config-test/Makefile', 'lib/python3.12/config-test/LICENSE')]
        compact_python(self.root)
        self.assertFalse(discarded.exists())
        self.assertTrue(all(path.read_bytes() == b'payload' for path in retained))

    def test_hardlink_failure_keeps_complete_files(self):
        left = self.put('bin/a', b'identical')
        right = self.put('bin/b', b'identical')
        with patch('compact.os.link', side_effect=OSError('unsupported')):
            deduplicate_binaries(self.root / 'bin')
        self.assertEqual(left.read_bytes(), right.read_bytes())
        self.assertEqual(sorted(p.name for p in left.parent.iterdir()), ['a', 'b'])
        deduplicate_binaries(self.root / 'bin')
        self.assertEqual(payload_size(left.parent)['unique_file_bytes'], len(b'identical'))

    @unittest.skipUnless(shutil.which('cc') and shutil.which('readelf'), 'native ELF tools needed')
    def test_real_elf_transitive_dependencies_and_unused_symlinks(self):
        (self.root / 'lib').mkdir()
        (self.root / 'bin').mkdir()
        leaf = self.put('leaf.c', b'int leaf(void) { return 7; }')
        middle = self.put('middle.c', b'int leaf(void); int middle(void) { return leaf(); }')
        main = self.put('main.c', b'int middle(void); int main(void) { return middle() == 7 ? 0 : 1; }')
        lib = self.root / 'lib'
        subprocess.run(['cc', '-shared', '-fPIC', str(leaf), '-o', str(lib/'libleaf.so')], check=True)
        subprocess.run(['cc', '-shared', '-fPIC', str(middle), '-L'+str(lib), '-lleaf',
                        '-Wl,-rpath,$ORIGIN', '-o', str(lib/'libmiddle.so')], check=True)
        subprocess.run(['cc', str(main), '-L'+str(lib), '-lmiddle', '-Wl,-rpath,$ORIGIN/../lib',
                        '-Wl,-rpath-link,'+str(lib), '-o', str(self.root/'bin/app')], check=True)
        shutil.copy2(lib/'libleaf.so', lib/'unused.so.1')
        (lib/'unused.so').symlink_to('unused.so.1')
        compact_libraries(self.root, windows=False, objdump='objdump', readelf='readelf')
        self.assertFalse((lib/'unused.so').exists())
        self.assertFalse((lib/'unused.so.1').exists())
        subprocess.run([str(self.root/'bin/app')], check=True)


if __name__ == '__main__':
    unittest.main()

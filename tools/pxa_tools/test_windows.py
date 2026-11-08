"""Host portability regressions; also runnable with native Windows Python."""
import os
import io
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import tarfile
import unittest
from unittest.mock import patch

from .common import ToolError, digest, executable, locked
from .distribution import install
from .project import TARGETS, aot_flags, resolve_tools
from .run import simulator_bin


class WindowsToolsTest(unittest.TestCase):
    def test_windows_distribution_uses_cmd_and_does_not_require_glibc(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            archive=root/'sdk.tar.gz'
            with tarfile.open(archive,'w:gz') as tar:
                content=b'@echo off\r\n'
                member=tarfile.TarInfo('bin/pxa.cmd');member.size=len(content)
                tar.addfile(member,io.BytesIO(content))
            document={'schema':'pxa-distribution-1','version':'0.8.0','platform':'windows-x86_64',
                      'components':[{'file':archive.name,'size':archive.stat().st_size,'sha256':digest(archive)}]}
            manifest=root/'distribution.json';manifest.write_text(json.dumps(document))
            Path(str(manifest)+'.sig').write_bytes(b'signature verification tested separately')
            windows=SimpleNamespace(system=lambda:'Windows',machine=lambda:'AMD64')
            with patch('pxa_tools.distribution.platform',windows), \
                    patch('pxa_tools.distribution.sys',SimpleNamespace(getwindowsversion=lambda:SimpleNamespace(build=19045))), \
                    patch('pxa_tools.distribution.data_root',return_value=root/'data'), \
                    patch('pxa_tools.distribution.verify_manifest',return_value=document):
                installed=install('0.8.0',str(manifest),offline=True)
            self.assertTrue((installed/'bin/pxa.cmd').is_file())
            self.assertEqual((installed/'distribution.json').read_bytes(),manifest.read_bytes())

    def test_windows_rejects_a_linux_distribution_before_extracting(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);manifest=root/'distribution.json'
            manifest.write_text('{}');Path(str(manifest)+'.sig').write_bytes(b'test')
            windows=SimpleNamespace(system=lambda:'Windows',machine=lambda:'AMD64')
            with patch('pxa_tools.distribution.platform',windows), \
                    patch('pxa_tools.distribution.data_root',return_value=root/'data'), \
                    patch('pxa_tools.distribution.verify_manifest',return_value={'version':'0.8.0','platform':'linux-x86_64'}), \
                    patch('pxa_tools.distribution.extract',side_effect=AssertionError('must not extract')):
                with self.assertRaisesRegex(ToolError,'platform mismatch'):
                    install('0.8.0',str(manifest),offline=True)

    def test_windows_offline_toolchain_requires_no_shell_bootstrap(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for name in ('toolchains/wasi-sdk/bin/clang++.exe', 'bin/wamrc.exe'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            windows = SimpleNamespace(name='nt', environ={})
            with patch('pxa_tools.common.os', windows), patch('pxa_tools.project.os', windows), \
                    patch('pxa_tools.project.command', side_effect=AssertionError('must stay offline')):
                wasi, wamrc = resolve_tools(root, offline=True)
            self.assertEqual(wasi, root / 'toolchains/wasi-sdk')
            self.assertEqual(wamrc, root / 'bin/wamrc.exe')

    def test_windows_simulator_lookup_uses_exe_and_keeps_spaces(self):
        with tempfile.TemporaryDirectory(prefix='pxa native tools ') as td:
            path = Path(td) / 'pxsys_desktop_simulator.exe'
            path.touch()
            windows = SimpleNamespace(name='nt', environ={'PXA_SIMULATOR_DIR': td})
            with patch('pxa_tools.common.os', windows), patch('pxa_tools.run.os', windows):
                self.assertEqual(simulator_bin('pxsys_desktop_simulator'), path)
                self.assertEqual(executable(path), path)

    def test_simulator_abis_are_distinct_and_bounds_are_explicit_on_windows(self):
        self.assertEqual(TARGETS['windows-simulator'][0], 'windows-x86_64')
        self.assertEqual(TARGETS['simulator'][0], 'linux-x86_64')
        self.assertIn('--target-abi=msvc', aot_flags({}, 'windows-simulator'))
        self.assertIn('--target-abi=gnu', aot_flags({}, 'simulator'))
        self.assertIn('--bounds-checks=1', aot_flags({}, 'windows-simulator'))
        self.assertIn('--target-abi=gnu', aot_flags({}, 'esp32s3'))

    def test_exception_releases_native_build_lock(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'compiler.lock'
            with self.assertRaisesRegex(RuntimeError, 'interrupted'):
                with locked(path):
                    raise RuntimeError('interrupted')
            # A second acquisition would hang on Windows if the byte lock leaked.
            with locked(path):
                self.assertTrue(path.is_file())


if __name__ == '__main__':
    unittest.main()

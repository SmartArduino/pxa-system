import json
import shlex
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from .build_state import source_snapshot, dependency_snapshot, check_inputs, _ninja_input_paths
from .common import ToolError, digest


class BuildStateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.app = self.root / 'apps/counter'
        self.app.mkdir(parents=True)
        (self.app / 'main.cpp').write_text('int main(){}')
        (self.app / 'package.json').write_text('{"id":"counter"}')
        self.project = SimpleNamespace(path=self.app, app_id='counter', metadata={})

    def tearDown(self):
        self.temp.cleanup()

    def test_source_assets_common_and_new_files_invalidate_but_outputs_do_not(self):
        common = self.app.parent / 'common'
        common.mkdir(); (common/'shared.hpp').write_text('first')
        before = source_snapshot(self.project)
        dist = self.app / 'dist'; dist.mkdir()
        (dist/'counter.pxa').write_bytes(b'package')
        (dist/'counter.pxa.build.json').write_text('{}')
        generation = dist/'.counter-deadbeef'; generation.mkdir()
        (generation/'manifest.pxm').write_bytes(b'manifest')
        state = self.app/'.pxa'; state.mkdir(); (state/'cache').write_bytes(b'generated')
        (self.app/'main.cpp').touch()
        self.assertEqual(source_snapshot(self.project), before)
        (common/'shared.hpp').write_text('second')
        self.assertNotEqual(source_snapshot(self.project), before)
        stable = source_snapshot(self.project)
        sources = self.app/'build'; sources.mkdir(); (sources/'new.cpp').write_text('code')
        self.assertNotEqual(source_snapshot(self.project), stable)

    def test_recorded_external_dependency_change_and_missing_file_fail(self):
        external = self.root/'external.hpp'; external.write_text('header')
        receipt = {'inputs': {'source': source_snapshot(self.project),
                             'dependencies': {str(external): digest(external)}}}
        check_inputs(self.project, receipt)
        external.write_text('changed')
        with self.assertRaisesRegex(ToolError, 'dependency changed'):
            check_inputs(self.project, receipt)
        external.unlink()
        with self.assertRaises(ToolError):
            check_inputs(self.project, receipt)

    def test_cmake_and_current_ninja_objects_are_recorded(self):
        build = self.app/'.pxa/build/test'; reply=build/'.cmake/api/v1/reply'; reply.mkdir(parents=True)
        script = self.root/'Shared.cmake'; script.write_text('# module')
        header = self.root/'external.hpp'; header.write_text('// header')
        generator_input = self.root/'external generator input.txt'; generator_input.write_text('input')
        (reply/'index-1.json').write_text(json.dumps({'objects':[{'kind':'cmakeFiles','jsonFile':'files.json'}]}))
        (reply/'files.json').write_text(json.dumps({'paths':{'source':str(self.app)},'inputs':[
            {'path':str(script)}, {'path':str(build/'generated.cmake'),'isGenerated':True}]}))
        def query(arguments, **kwargs):
            if arguments[-1]=='compdb':
                return json.dumps([{'output':'main.o'},{'output':'main.wasm'}])
            if 'inputs' in arguments:
                self.assertEqual(arguments[-2:], ['inputs', 'main'])
                return shlex.quote(str(generator_input))+'\nmain.o\n.\n'
            self.assertEqual(arguments[-2:], ['deps','main.o'])
            return 'main.o: #deps 1\n    '+str(header)+'\n'
        with patch('pxa_tools.build_state.command',side_effect=query):
            self.assertEqual(dependency_snapshot(build,'ninja',['main']),{
                str(script):digest(script),str(header):digest(header),
                str(generator_input):digest(generator_input)})

    def test_posix_inventory_preserves_quoted_paths_without_running_shell(self):
        names = ['中文 input.txt', "owner's.hpp", 'line\nbreak', r'back\slash',
                 '$(touch SHOULD_NOT_EXIST)', ' trailing ', '\t', 'a;b']
        graph = '\n'.join(shlex.quote(name) for name in names) + '\n'
        self.assertEqual(_ninja_input_paths(graph, windows=False), names)
        with self.assertRaisesRegex(ToolError, 'Invalid Ninja'):
            _ninja_input_paths("'unterminated", windows=False)

    def test_windows_inventory_preserves_native_separators_and_quotes(self):
        graph = ('"C:\\中文 hello\\owner\'s.hpp"\r\n'
                 'C:\\SDK\\header.hpp\r\n'
                 '"C:\\folder with spaces\\\\"\r\n'
                 '"C:\\file & name.h"\r\n')
        self.assertEqual(_ninja_input_paths(graph, windows=True), [
            "C:\\中文 hello\\owner's.hpp", r'C:\SDK\header.hpp',
            'C:\\folder with spaces\\', 'C:\\file & name.h'])
        for malformed in ['"unclosed', '"C:\\folder \\"']:
            with self.subTest(malformed=malformed), self.assertRaises(ToolError):
                _ninja_input_paths(malformed, windows=True)


if __name__ == '__main__':
    unittest.main()

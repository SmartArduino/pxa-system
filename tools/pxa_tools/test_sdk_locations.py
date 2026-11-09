"""Pinned SDK lookup across native/Agent installs without rewriting locks."""
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import subprocess
import sys
import unittest
from unittest.mock import patch

from .common import ToolError
from . import project


class SDKLocationsTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.current=self.root/'selected/current'
        self.distribution=self.root/'data/pxa'
        self.agent=self.root/'agent'
        self.patches=[patch('pxa_tools.distribution.data_root',return_value=self.distribution),
                      patch.dict(os.environ,{'PXA_AGENT_HOME':str(self.agent)})]
        for item in self.patches:item.start()

    def tearDown(self):
        for item in reversed(self.patches):item.stop()
        self.temp.cleanup()

    def sdk(self, path, launcher='pxa'):
        (path/'bin').mkdir(parents=True);(path/'bin'/launcher).touch()
        (path/'VERSION').write_text('0.8.0')
        return path

    def test_agent_and_selected_sdk_siblings_are_found(self):
        cached=self.sdk(self.agent/'sdks/0.8.0')
        self.assertEqual(project.installed_sdk('0.8.0',root=self.current),cached)
        sibling=self.sdk(self.current.parent/'0.8.0')
        self.assertEqual(project.installed_sdk('0.8.0',root=self.current),sibling)

    def test_standalone_priority_and_version_mismatch_never_selects_wrong_sdk(self):
        selected=self.sdk(self.distribution/'releases/0.8.0')
        self.assertEqual(project.installed_sdk('0.8.0',root=self.current),selected)
        (selected/'VERSION').write_text('0.9.0')
        with self.assertRaisesRegex(ToolError,'Locked SDK 0.8.0 is missing'):
            project.installed_sdk('0.8.0',root=self.current)
        with self.assertRaisesRegex(ToolError,'Invalid SDK release'):
            project.installed_sdk('../../outside',root=self.current)

    def test_windows_cmd_launcher_in_agent_cache(self):
        cached=self.sdk(self.agent/'sdks/0.8.0',launcher='pxa.cmd')
        with patch.object(project,'os',SimpleNamespace(name='nt',environ={'PXA_AGENT_HOME':str(self.agent)})):
            self.assertEqual(project.installed_sdk('0.8.0',root=self.current),cached)

    @unittest.skipIf(os.name == 'nt','POSIX exec routing; Windows launcher lookup is checked separately')
    def test_cli_redirects_to_agent_sdk_and_retains_lock(self):
        release='8.0.0'
        cached=self.agent/'sdks'/release;(cached/'bin').mkdir(parents=True)
        (cached/'VERSION').write_text(release)
        launcher=cached/'bin/pxa'
        launcher.write_text('#!/usr/bin/env python3\nimport os,sys\nprint(os.environ["PXA_SDK_REDIRECT"])\nprint(sys.argv[1])\n')
        launcher.chmod(0o755)
        app=self.root/'app';app.mkdir();original='{"sdk":{"release":"8.0.0"}}'
        (app/'pxa.lock').write_text(original)
        environment=dict(os.environ,PYTHONPATH=str(project.SDK_ROOT/'tools'))
        result=subprocess.run([sys.executable,'-m','pxa_tools','build',str(app)],env=environment,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.splitlines(),[release,'build'])
        self.assertEqual((app/'pxa.lock').read_text(),original)


if __name__ == '__main__':unittest.main()

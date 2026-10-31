"""Batch build and bounded polling behavior."""
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from .common import ToolError
from .projects import build_targets
from .workbench import Jobs


class EfficiencyTests(unittest.TestCase):
    def test_targets_share_one_build_and_reject_invalid_requests(self):
        self.assertEqual(build_targets(targets=['esp32s3','esp32s31','simulator','wasm']),
                         'esp32s3,esp32s31,simulator,wasm')
        self.assertEqual(build_targets('wasm'), 'wasm')
        self.assertEqual(build_targets(targets=['wasm','wasm']), 'wasm')
        for targets in [[], ['--bad'], 'wasm', ['wasm']*5]:
            with self.subTest(targets=targets), self.assertRaises(ToolError):
                build_targets(targets=targets)

    def test_status_only_poll_skips_logs_and_elapsed_stops_at_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            log=root/'run.log';log.write_text('build output')
            jobs=Jobs(SimpleNamespace(workspace=root))
            process=SimpleNamespace(poll=lambda:0,returncode=0)
            jobs.jobs['job']={'process':process,'directory':root,'started':10,'finished':12,
                'done':True,'timed_out':False,'scope':{'violations':[]},'discover_selector':False}
            with patch.object(Path,'open',side_effect=AssertionError('unexpected log read')):
                result=jobs.status('job',include_log=False)
            self.assertTrue(result['passed']);self.assertEqual(result['elapsed_seconds'],2)
            self.assertEqual(jobs.status('job')['log_tail'],'build output')
            with patch.object(Path,'open',side_effect=AssertionError('unchanged log reread')):
                self.assertEqual(jobs.status('job')['log_tail'],'build output')
            log.write_text('new output with failure')
            self.assertEqual(jobs.status('job')['log_tail'],'new output with failure')

    def test_quiet_poll_still_discovers_simulator(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'run.log').write_text('PXADB: pxa device info --simulator pai-touch@test\n')
            jobs=Jobs(SimpleNamespace(workspace=root))
            jobs.jobs['job']={'process':SimpleNamespace(poll=lambda:None),'directory':root,'started':0,
                             'done':False,'timed_out':False,'discover_selector':True}
            self.assertEqual(jobs.status('job',include_log=False)['simulator'],'pai-touch@test')
            with patch.object(Path,'open',side_effect=AssertionError('selector already known')):
                self.assertEqual(jobs.status('job',include_log=False)['simulator'],'pai-touch@test')

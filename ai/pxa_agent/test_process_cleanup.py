"""Timeouts must stop owned compiler descendants without affecting other jobs."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from .common import ToolError, run


class ProcessCleanupTests(unittest.TestCase):
    def test_output_and_failure_diagnostics_are_preserved(self):
        self.assertEqual(run([sys.executable, '-c', 'print("ready")']), 'ready\n')
        with self.assertRaisesRegex(ToolError, r'Command failed \(7\).*'):
            run([sys.executable, '-c', 'print("compiler diagnostic");raise SystemExit(7)'])

    @unittest.skipUnless(os.name == 'posix' and Path('/proc').is_dir(), 'Linux process-group integration')
    def test_timeout_kills_stubborn_grandchild_and_preserves_unrelated_job(self):
        unrelated = subprocess.Popen([sys.executable, '-c', 'import time;time.sleep(60)'],
                                     start_new_session=True)
        self.addCleanup(lambda: (unrelated.kill(), unrelated.wait()) if unrelated.poll() is None else None)
        with tempfile.TemporaryDirectory() as directory:
            pidfile = Path(directory) / 'grandchild.pid'
            descendant = ('import os,signal,time;from pathlib import Path;'
                          'signal.signal(signal.SIGTERM,signal.SIG_IGN);'
                          f'Path({str(pidfile)!r}).write_text(str(os.getpid()));time.sleep(60)')
            parent = ('import subprocess,sys,time;'
                      f'subprocess.Popen([sys.executable,"-c",{descendant!r}]);time.sleep(60)')
            with self.assertRaises(subprocess.TimeoutExpired):
                run([sys.executable, '-c', parent], timeout=2)
            self.assertTrue(pidfile.is_file(), 'The compiler descendant must actually start')
            process = Path('/proc') / pidfile.read_text() / 'stat'
            if process.exists():
                self.assertEqual(process.read_text().rsplit(')', 1)[1].split()[0], 'Z')
            self.assertIsNone(unrelated.poll(), 'Only the owned process group may be terminated')


if __name__ == '__main__':
    unittest.main()

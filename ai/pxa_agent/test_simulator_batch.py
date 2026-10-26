import json
import signal
import subprocess
import threading
import time
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

from . import simulator_batch as batch
from .common import ToolError
from .environment import Environment
from .workbench import Jobs


class BatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        profiles = self.root / 'sdk/share/pxa/profiles'
        profiles.mkdir(parents=True)
        (profiles / 'pai-touch.toml').write_text('[desktop]\nwidth=296\nheight=240\n')
        self.output = self.root / 'evidence'
        self.client = MagicMock()
        self.client.__enter__.return_value = self.client
        self.client.request.return_value = [SimpleNamespace(payload='kind=simulator')]
        self.module = MagicMock()
        self.module.open_client.return_value = self.client
        self.module.write_screenshot.side_effect = lambda capture,path: path.write_bytes(b'original')
        self.steps = [{'name':'tap','action':'tap','x':100,'y':140},
                      {'name':'after','action':'screenshot'}]

    def tearDown(self):
        self.temporary.cleanup()

    def run_batch(self, steps=None):
        return batch.execute(self.root/'sdk', 'pai-touch@app-owned', steps or self.steps, self.output)

    def test_one_sdk_session_for_ordered_steps_and_original_capture(self):
        with patch.object(batch, 'load_sdk', return_value=self.module), patch.object(batch, 'inject') as release:
            result = self.run_batch()
        self.module.open_client.assert_called_once()
        self.assertEqual([call.args[0] for call in self.client.request.call_args_list],
                         ['INFO','INPUT TAP 100 140','SCREENSHOT'])
        self.assertTrue(result['runtime_passed'])
        self.assertTrue(result['input_released'])
        self.assertFalse(result['screenshots'][0]['post_processed'])
        self.assertEqual((self.output/'after.png').read_bytes(), b'original')
        release.assert_called_once()
        self.client.__exit__.assert_called_once()

    def test_preflight_rejects_bad_later_step_before_any_input(self):
        with patch.object(batch, 'load_sdk') as load:
            with self.assertRaises(ValueError):
                self.run_batch([*self.steps, {'name':'bad','action':'tap','x':296,'y':1}])
            load.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_failed_write_is_not_replayed_and_releases_input(self):
        def request(command, **kwargs):
            if command.startswith('INPUT'):
                raise RuntimeError('response lost')
            return [SimpleNamespace(payload='kind=simulator')]
        self.client.request.side_effect = request
        with patch.object(batch, 'load_sdk', return_value=self.module), patch.object(batch, 'inject') as release:
            with self.assertRaisesRegex(RuntimeError, 'response lost'):
                self.run_batch()
        self.assertEqual(self.client.request.call_count, 2)
        release.assert_called_once()
        result = json.loads((self.output/'result.json').read_text())
        self.assertFalse(result['runtime_passed'])
        self.assertTrue(result['input_released'])
        self.assertEqual(result['screenshots'], [])
        self.client.__exit__.assert_called_once()

    def test_limits_and_names(self):
        invalid = [[], self.steps*17,
                   [{'name':'../escape','action':'screenshot'}],
                   [{'name':'tap','action':'tap','x':True,'y':1}],
                   [{'name':'a','action':'controller','buttons':4096,'ms':10}],
                   [{'name':f'shot-{i}','action':'screenshot'} for i in range(9)],
                   [{'name':f'wait-{i}','action':'wait','ms':5000} for i in range(4)]]
        for steps in invalid:
            with self.subTest(steps=steps), self.assertRaises(ValueError):
                batch.validate(steps, 296,240)
        with self.assertRaises(ValueError):
            batch.execute(self.root/'sdk', '/dev/ttyUSB0', self.steps, self.output)

    def test_cancel_before_start_opens_no_session(self):
        cancel = self.root/'cancel'
        cancel.touch()
        with patch.object(batch, 'load_sdk', return_value=self.module), patch.object(batch, 'inject') as release:
            with self.assertRaises(batch.BatchCancelled):
                batch.execute(self.root/'sdk','pai-touch@app-owned',self.steps,self.output,cancel_file=cancel)
        self.module.open_client.assert_not_called()
        release.assert_not_called()
        result = json.loads((self.output/'result.json').read_text())
        self.assertTrue(result['cancelled'])
        self.assertFalse(result['runtime_passed'])

    def test_cancel_interrupts_wait_and_never_executes_later_input(self):
        cancel = self.root/'cancel'
        timer = threading.Timer(.15, cancel.touch)
        previous = signal.getsignal(signal.SIGINT)
        timer.start()
        started = time.monotonic()
        try:
            with patch.object(batch, 'load_sdk', return_value=self.module), patch.object(batch, 'inject') as release:
                with self.assertRaises(batch.BatchCancelled):
                    batch.execute(self.root/'sdk','pai-touch@app-owned',[
                        {'name':'waiting','action':'wait','ms':5000}, *self.steps],self.output,cancel_file=cancel)
            release.assert_called_once()
        finally:
            timer.join()
        self.assertLess(time.monotonic()-started, 2)
        self.assertEqual(signal.getsignal(signal.SIGINT), previous)
        self.assertEqual([c.args[0] for c in self.client.request.call_args_list], ['INFO'])
        result = json.loads((self.output/'result.json').read_text())
        self.assertTrue(result['cancelled'])
        self.assertTrue(result['input_released'])
        self.assertFalse(result['runtime_passed'])

    def test_cleanup_cannot_be_interrupted_by_late_cancellation(self):
        cancel = self.root/'cancel'
        def release(*args, **kwargs):
            cancel.touch()
            time.sleep(.1)
        with patch.object(batch,'load_sdk',return_value=self.module), patch.object(batch,'inject',side_effect=release):
            result = batch.execute(self.root/'sdk','pai-touch@app-owned',self.steps,self.output,cancel_file=cancel)
        self.assertTrue(result['input_released'])

    def test_unresponsive_worker_stops_only_its_owned_simulator(self):
        jobs = Jobs(Environment(self.root, {'engine':'podman','image':'test'}))
        process = MagicMock()
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired('exec',35)
        owner = {'container':'owned-simulator'}
        job = {'process':process,'cancel_file':self.root/'cancel','batch_owner':'owner'}
        jobs.jobs.update(owner=owner,batch=job)
        with patch.object(jobs,'_terminate') as terminate, patch.object(jobs,'status',return_value={}):
            jobs.stop('batch')
        self.assertTrue(job['cancel_file'].exists())
        self.assertEqual([c.args[0] for c in terminate.call_args_list], [owner,job])
        self.assertTrue(owner['cancel_requested'])
        self.assertIn('cancellation_error',job)

    def test_finished_status_includes_batch_and_cancellation_never_passes(self):
        jobs = Jobs(Environment(self.root, {'engine':'podman','image':'test'}))
        self.output.mkdir()
        result = {'runtime_passed':True,'input_released':True,'cancelled':False,'steps':[]}
        (self.output/'result.json').write_text(json.dumps(result))
        process = MagicMock(returncode=0)
        process.poll.return_value = 0
        job = {'process':process,'done':True,'directory':self.output,'batch_output':self.output,
               'started':time.monotonic(),'timed_out':False,'scope':{'violations':[]}}
        jobs.jobs['batch'] = job
        state = jobs.status('batch')
        self.assertTrue(state['passed'])
        self.assertEqual(state['batch'],result)
        job['cancel_requested'] = True
        state = jobs.status('batch')
        self.assertFalse(state['passed'])
        self.assertTrue(state['cancelled'])

    def test_overlapping_input_is_rejected_and_only_owned_container_is_used(self):
        env = Environment(self.root, {'engine':'podman','image':'test'})
        jobs = Jobs(env)
        jobs.jobs['owner'] = {'container':'owned-container'}
        jobs.jobs['batch'] = {}
        def status(identity, **kwargs):
            return {'running':True,'simulator':'pai-touch@app-owned'}
        with patch.object(jobs,'status',side_effect=status), patch.object(jobs,'start',return_value={'job_id':'batch'}) as start:
            result = jobs.interact('owner', self.steps)
            command = start.call_args.args[0]
            self.assertEqual(command[:3], ['podman','exec','owned-container'])
            self.assertIn('/pxa-agent-kit/pxa_agent/simulator_batch.py',command)
            self.assertTrue(result['visual_review_required'])
            with self.assertRaises(ToolError):
                jobs.interact('owner', self.steps)
            with self.assertRaises(ToolError):
                jobs.simulator('owner',['info'])
            start.assert_called_once()


if __name__ == '__main__':
    unittest.main()

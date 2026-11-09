"""Actual install ordering: preflight before writes, including ESP32-P4."""
import json
from contextlib import nullcontext
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock,patch

from .common import ToolError
from .workbench import install_app


class CompatibilityWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.project=Path(self.temp.name)/'app';(self.project/'dist').mkdir(parents=True)
        (self.project/'package.json').write_text('{"id":"pxa-test"}')
        (self.project/'dist/pxa-test.pxa').touch()
        self.env=Mock();self.env.project.side_effect=Path
        self.identity='aa'*32+':pxa-test'
        self.host_reader=patch('pxa_agent.host_tools.reader',return_value=SimpleNamespace(
            package_composite_identity=lambda _:self.identity))
        self.host_reader.start();self.addCleanup(self.host_reader.stop)
        def device(*arguments, timeout=60):
            return self.env.device([*arguments, '--port', '/dev/P4', '--baud', '2000000', '--timeout', '10'], timeout=timeout)
        session=patch('pxa_agent.device_session.install_connection',side_effect=lambda *args:nullcontext(device))
        session.start();self.addCleanup(session.stop)
        self.report={'schema':'pxa-package-compatibility-1','status':'compatible','checks':[]}

    def tearDown(self):self.temp.cleanup()

    def responses(self, status):
        self.report.update(status=status,checks=[] if status=='compatible' else [
            {'subject':'core','status':status,'required':[2,1],'actual':[2,0],'message':'Needs Core 2.1'}])
        self.env.device.side_effect=[
            'target=esp32p4;board=espp4-a;pxa_core=2.1',json.dumps(self.report),
            '', 'installed','accepted',self.identity+'\tTest\t1\tactive=1']

    def test_p4_build_and_preflight_use_delivered_package_before_install(self):
        self.responses('compatible')
        result=install_app(self.env,self.project,port='/dev/P4',strict_compatibility=True)
        self.assertEqual(result['target'],'esp32p4')
        self.assertEqual(result['compatibility']['status'],'compatible')
        self.env.call.assert_any_call(['build',self.project,'--offline','--target','esp32p4','--aot-only'],timeout=600)
        actions=[call.args[0] for call in self.env.device.call_args_list]
        self.assertEqual(actions[1][0],'compatibility')
        self.assertEqual(actions[1][1],self.project/'dist/pxa-test.pxa')
        self.assertEqual(actions[3][:2],['package','install'])
        self.assertIn('--strict-compatibility',actions[3])
        self.assertEqual(actions[4][:2],['package','run'])
        self.assertEqual(actions[4][2],self.identity)

    def test_known_incompatibility_and_strict_unknown_never_write(self):
        for status in ('incompatible','unknown'):
            with self.subTest(status=status):
                self.env.reset_mock();self.responses(status)
                with self.assertRaisesRegex(ToolError,'nothing was uploaded'):
                    install_app(self.env,self.project,port='/dev/P4',strict_compatibility=True)
                self.assertEqual([call.args[0][0] for call in self.env.device.call_args_list],['info','compatibility'])

    def test_legacy_unknown_is_reported_without_claiming_compatibility(self):
        self.responses('unknown')
        result=install_app(self.env,self.project,port='/dev/P4')
        self.assertEqual(result['compatibility']['status'],'unknown')
        self.assertTrue(result['active_verified'])

    def test_running_exact_identity_is_stopped_and_waited_before_update(self):
        row=lambda active:self.identity+'\tTest\t1\tactive='+str(active)
        self.env.device.side_effect=[
            'target=esp32p4',json.dumps(self.report),row(1),'stop accepted',row(0),
            'installed','run accepted',row(1)]
        result=install_app(self.env,self.project,port='/dev/P4')
        actions=[call.args[0] for call in self.env.device.call_args_list]
        self.assertEqual(actions[3][:3],['package','stop',self.identity])
        self.assertEqual(actions[4][:2],['package','list'])
        self.assertEqual(actions[5][:2],['package','install'])
        self.assertTrue(result['stopped_running_target'])

    def test_same_app_id_other_publisher_is_never_stopped_or_counted_active(self):
        other='bb'*32+':pxa-test\tOther\t1\tactive=1'
        self.env.device.side_effect=[
            'target=esp32p4',json.dumps(self.report),other,'installed','run accepted',
            other+'\n'+self.identity+'\tTest\t1\tactive=1']
        result=install_app(self.env,self.project,port='/dev/P4')
        self.assertFalse(result['stopped_running_target'])
        self.assertFalse(any(call.args[0][:2]==['package','stop']for call in self.env.device.call_args_list))

    def test_delayed_stop_never_uploads_and_keeps_running_target(self):
        row=self.identity+'\tTest\t1\tactive=1'
        self.env.device.side_effect=['target=esp32p4',json.dumps(self.report),row,'stop accepted',*([row]*31)]
        with patch('pxa_agent.workbench.time.sleep'),self.assertRaisesRegex(ToolError,'no package was uploaded'):
            install_app(self.env,self.project,port='/dev/P4')
        actions=[call.args[0] for call in self.env.device.call_args_list]
        self.assertFalse(any(action[:2]==['package','install']for action in actions))
        self.assertFalse(any(action[:2]==['package','run']for action in actions))

    def test_failed_update_reactivates_original_identity_without_retrying_install(self):
        row=lambda active:self.identity+'\tTest\t1\tactive='+str(active)
        self.env.device.side_effect=[
            'target=esp32p4',json.dumps(self.report),row(1),'stop accepted',row(0),
            ToolError('connection lost after write'),row(0),'run accepted',row(1)]
        with self.assertRaisesRegex(ToolError,'connection lost after write'):
            install_app(self.env,self.project,port='/dev/P4')
        actions=[call.args[0] for call in self.env.device.call_args_list]
        self.assertEqual(sum(action[:2]==['package','install']for action in actions),1)
        self.assertEqual([action[2]for action in actions if action[:2]==['package','run']],[self.identity])

    def test_package_changed_during_preflight_never_stops_running_target(self):
        package=self.project/'dist/pxa-test.pxa'
        def mutate(arguments, **kwargs):
            if arguments[0]=='info':return 'target=esp32p4'
            if arguments[0]=='compatibility':
                package.write_bytes(b'changed')
                return json.dumps(self.report)
            self.fail('Mutation before verified package check: '+str(arguments))
        self.env.device.side_effect=mutate
        with self.assertRaisesRegex(ToolError,'Verified package changed'):
            install_app(self.env,self.project,port='/dev/P4')


class FirmwareStatusTest(unittest.TestCase):
    def test_status_reads_only_info_and_signed_metadata_without_guessing_partition_match(self):
        from .firmware import status
        env=Mock();env.device.return_value='board=espp4-a;target=esp32p4;pxa_version=0.8.0;pxa_core=2.0'
        with patch('pxa_agent.firmware.signed_document',return_value=([
                {'board':'espp4-a','version':'0.9.0','chip':'esp32p4'}],b'raw',b'sig')) as manifest, \
             patch('pxa_agent.firmware.artifact') as image, \
             patch('pxa_agent.firmware.subprocess.run') as command:
            result=status(env,port='/dev/P4',version='0.9.0',manifest='offline-manifest.json')
            manifest.assert_called_once_with('offline-manifest.json')
            image.assert_not_called();command.assert_not_called()
        self.assertEqual(result['status'],'update_available')
        self.assertEqual(result['app_only']['partition_match'],'unknown')
        self.assertEqual(result['firmware_origin'],'unverified')
        self.assertFalse(result['image_downloaded']);self.assertFalse(result['flash_completed'])
        env.device.assert_called_once_with(['info','--port','/dev/P4','--baud','2000000'],timeout=12)

    def test_unrecognized_or_disagreeing_board_never_fetches_or_flashes(self):
        from .firmware import status
        env=Mock();env.device.return_value='board=custom;target=esp32p4;pxa_version=0.9.0'
        with patch('pxa_agent.firmware.release_descriptor') as release:
            self.assertFalse(status(env,port='/dev/P4')['recognized_board'])
            with self.assertRaisesRegex(ToolError,'requested espp4-a'):
                status(env,port='/dev/P4',board='espp4-a')
            release.assert_not_called()


class ContextSDKLocationTest(unittest.TestCase):
    def test_windows_agent_cache_matches_locked_sdk_without_changing_lock(self):
        from . import context
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);sdk=root/'current';sdk.mkdir();(sdk/'VERSION').write_text('0.9.0')
            app=root/'app';app.mkdir();lock=app/'pxa.lock';original='{"sdk":{"release":"0.8.0"}}';lock.write_text(original)
            cached=root/'agent/sdks/0.8.0';(cached/'bin').mkdir(parents=True)
            (cached/'bin/pxa.cmd').touch();(cached/'VERSION').write_text('0.8.0')
            with patch.object(context,'os',SimpleNamespace(name='nt',environ={'PXA_AGENT_HOME':str(root/'agent')})):
                self.assertEqual(context.locked_sdk(sdk,app,root/'native/releases'),cached)
            self.assertEqual(lock.read_text(),original)


if __name__ == '__main__':unittest.main()

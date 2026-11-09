"""Explicit SDK migration preserves app data and audit history, including rollback."""
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

from .common import atomic_json,ToolError
from .environment import Environment
from .migration import migrate
from . import scope
from .workbench import Jobs


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.workspace=self.root/'workspace';self.workspace.mkdir()
        self.old=self.sdk('old','0.8.0');self.new=self.sdk('new','0.9.0')
        self.config={'schema':'pxa-agent-workspace-1','engine':'native','sdk':str(self.old),
                     'sdk_version':'0.8.0','custom_setting':'keep'}
        self.path=self.workspace/'.pxa/agent.json';atomic_json(self.path,self.config)
        self.app=self.workspace/'apps/game';self.app.mkdir(parents=True)
        (self.app/'main.c').write_text('app')
        (self.app/'pxa.lock').write_text('{"sdk":{"release":"0.8.0"}}')
        (self.app/'signer.pem').write_text('user-owned-test-identity')
        scope.begin(self.workspace,mode='app',project=self.app,sdk=self.old)
        self.original={p:p.read_bytes() for p in self.app.iterdir()}

    def sdk(self,name,version):
        path=self.root/name;(path/'sdk/guest-c/include').mkdir(parents=True)
        (path/'sdk/guest-c/include/api.h').write_text('original')
        (path/'bin').mkdir();(path/'bin/pxa').touch();(path/'VERSION').write_text(version)
        return path

    def tearDown(self):self.temp.cleanup()

    def unchanged_app(self):
        self.assertEqual({p:p.read_bytes() for p in self.app.iterdir()},self.original)

    def test_migration_retains_locks_identity_custom_settings_and_rearms_scope(self):
        with patch.object(Environment,'call',return_value='PXA 0.9.0\n'):
            result=migrate(self.workspace,version='0.9.0',sdk=self.new)
        self.unchanged_app()
        configuration=json.loads(self.path.read_text())
        self.assertEqual(configuration['sdk'],str(self.new))
        self.assertEqual(configuration['custom_setting'],'keep')
        self.assertTrue(Path(result['previous_scope']['archive']).is_file())
        self.assertTrue(scope.check(self.workspace)['passed'])
        (self.new/'sdk/guest-c/include/api.h').write_text('wrong platform edit')
        self.assertFalse(scope.check(self.workspace)['passed'])

    def test_invalid_sdk_or_failed_transition_retains_config_and_active_audit(self):
        configuration=self.path.read_bytes();audit=scope.state_path(self.workspace).read_bytes()
        with patch.object(Environment,'call',side_effect=ToolError('bad SDK')):
            with self.assertRaisesRegex(ToolError,'bad SDK'):
                migrate(self.workspace,version='0.9.0',sdk=self.new)
        self.assertEqual(self.path.read_bytes(),configuration)
        self.assertEqual(scope.state_path(self.workspace).read_bytes(),audit)
        with patch.object(Environment,'call',return_value='PXA 0.9.0'), \
             patch('pxa_agent.migration.scope.begin',side_effect=OSError('simulated state error')):
            with self.assertRaisesRegex(ToolError,'previous configuration/audit restored'):
                migrate(self.workspace,version='0.9.0',sdk=self.new)
        self.assertEqual(json.loads(self.path.read_text()),self.config)
        self.assertEqual(scope.state_path(self.workspace).read_bytes(),audit)
        self.unchanged_app()

    def test_existing_scope_violation_is_not_hidden_by_migration(self):
        (self.old/'sdk/guest-c/include/api.h').write_text('out-of-scope change')
        with patch.object(Environment,'call') as command:
            with self.assertRaisesRegex(ToolError,'Out-of-scope'):
                migrate(self.workspace,version='0.9.0',sdk=self.new)
            command.assert_not_called()
        self.assertEqual(json.loads(self.path.read_text()),self.config)
        self.assertFalse(scope.check(self.workspace)['passed'])

    def test_successful_mcp_job_reloads_its_persistent_environment(self):
        environment=Environment(self.workspace)
        updated={**self.config,'sdk':str(self.new),'sdk_version':'0.9.0'}
        atomic_json(self.path,updated)
        jobs=Jobs(environment);output=self.workspace/'local/artifacts/job';output.mkdir(parents=True)
        jobs.jobs['migration']={'process':SimpleNamespace(poll=lambda:0,returncode=0),
            'directory':output,'started':1,'finished':2,'done':True,'timed_out':False,
            'reload_environment':True,'scope':{'active':False},'discover_selector':False}
        self.assertTrue(jobs.status('migration')['environment_reloaded'])
        self.assertEqual(environment.config['sdk_version'],'0.9.0')


if __name__ == '__main__':unittest.main()

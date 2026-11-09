"""Device clients belong to the Kit, not an app's frozen compiler release."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from . import host_tools
from .environment import Environment


class HostToolsTests(unittest.TestCase):
    def test_bundled_client_wins_over_old_sdk_for_native_and_container(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);kit=root/'kit';sdk=root/'sdk'
            (kit/'host_tools').mkdir(parents=True);current=kit/'host_tools/pxadb.py';current.touch()
            (sdk/'tools/pxadb').mkdir(parents=True);(sdk/'tools/pxadb/pxadb.py').write_text('old')
            for config in ({'engine':'native','sdk':str(sdk)},
                           {'engine':'docker','image':'sha256:test','host_tools':str(sdk/'tools/pxadb')}):
                env=Environment(root,config)
                with patch.object(host_tools,'KIT',kit),patch.dict(os.environ,PXA_AGENT_HOST_TOOLS=''), \
                     patch('pxa_agent.environment.run',return_value='report') as call:
                    self.assertEqual(env.device(['runtime-info','--port','COM7']),'report')
                    self.assertEqual(Path(call.call_args.args[0][1]),current)
                    self.assertEqual((sdk/'tools/pxadb/pxadb.py').read_text(),'old')

    def test_container_mounts_current_clients_read_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);kit=root/'kit';(kit/'host_tools').mkdir(parents=True)
            (kit/'host_tools/pxadb.py').touch()
            env=Environment(root,{'engine':'docker','image':'sha256:test','host_tools':str(root/'old')})
            with patch.object(host_tools,'KIT',kit),patch.dict(os.environ,PXA_AGENT_HOST_TOOLS=''), \
                 patch('pxa_agent.environment.sdk_data',return_value=root/'data'), \
                 patch('pxa_agent.environment.data_root',return_value=root/'agent'):
                command=env.invocation(['--version'])
            self.assertIn(f'type=bind,source={kit}/host_tools,target=/pxa-host-tools,readonly',command)
            self.assertIn('PXA_AGENT_HOST_TOOLS=/pxa-host-tools',command)


if __name__ == '__main__':unittest.main()

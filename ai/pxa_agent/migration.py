"""Explicit SDK selection with retained project locks, identity and audit history."""
import json
from pathlib import Path
import re

from . import VERSION
from .common import ToolError, atomic_json, evidence, locked
from .environment import Environment, select_sdk
from . import scope


def migrate(workspace, *, version, sdk=None, engine='auto', manifest=None):
    workspace=Path(workspace).expanduser().resolve()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?',version):
        raise ToolError('Choose an explicit SDK version')
    path=workspace/'.pxa/agent.json'
    if path.is_symlink() or path.parent.is_symlink():
        raise ToolError('Refusing a symlink workspace configuration')
    with locked(workspace/'.pxa/migrate.lock'):
        previous=Environment(workspace)
        if previous.config.get('mode') == 'board':
            raise ToolError('SDK migration belongs to an app workspace; board work uses its ESP-IDF environment')
        scope.enforce(workspace)
        selected=select_sdk(previous.config,engine=engine,sdk=sdk,version=version,manifest=manifest)
        configuration={key:value for key,value in previous.config.items() if key not in
            {'engine','sdk','host_tools','image','verified_distribution','sdk_version','kit_version'}}
        configuration.update(schema='pxa-agent-workspace-1',kit_version=VERSION,sdk_version=version,**selected)
        candidate=Environment(workspace,configuration)
        cli_version=candidate.call(['--version'],timeout=60).strip()
        if cli_version != 'PXA '+version:
            raise ToolError('Selected SDK launcher reports '+cli_version+'; configuration was retained')
        scope.enforce(workspace)
        active=scope.state_path(workspace)
        saved_scope=json.loads(active.read_text()) if active.is_file() else None
        if saved_scope and candidate.native:
            sdk_path=Path(configuration['sdk']).resolve()
            if any(scope.inside(sdk_path,Path(allowed)) or scope.inside(Path(allowed),sdk_path)
                   for allowed in saved_scope['allowed']):
                raise ToolError('The SDK and allowed app/board paths must be separate')
        output=evidence(workspace,'pxa-sdk-migration')
        atomic_json(output/'before.json',previous.config)
        archived=None
        try:
            archived=scope.finish(workspace) if saved_scope else None
            atomic_json(path,configuration)
            if saved_scope:
                scope.begin(workspace,mode=saved_scope['mode'],project=saved_scope.get('project'),
                            board=saved_scope.get('board'),sdk=configuration.get('sdk'))
        except BaseException as error:
            atomic_json(path,previous.config)
            if saved_scope:atomic_json(active,saved_scope)
            atomic_json(output/'failed.json',{'error':str(error),'configuration_restored':True})
            if not isinstance(error,Exception):raise
            raise ToolError('SDK migration failed; previous configuration/audit restored: '+str(error)) from error
        atomic_json(output/'after.json',configuration)
        result={'previous_sdk':previous.config.get('sdk_version'),'selected_sdk':version,
                'engine':configuration['engine'],'cli_version':cli_version,'evidence':str(output),
                'project_locks_changed':False,'signing_identity_changed':False,
                'previous_scope':archived,'active_scope_preserved':bool(saved_scope),
                'next':'Existing projects keep their pinned SDK. To migrate an app explicitly, run sdk update APP, then rebuild and verify; do not delete its lock.'}
        atomic_json(output/'result.json',result)
        return result

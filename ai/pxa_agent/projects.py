"""Language selection without changing the installed SDK."""
import json
import re

from .common import KIT, ToolError, atomic_json
from . import scope


def build_targets(target='simulator', targets=None):
    values = [target] if targets is None else targets
    if not isinstance(values, list) or not 1 <= len(values) <= 4 or any(
            value not in ('simulator', 'esp32s3', 'esp32s31', 'wasm') for value in values):
        raise ToolError('Choose 1..4 PXA build targets')
    return ','.join(dict.fromkeys(values))


def initialize(env, project, app_id=None, language='cpp'):
    path = env.project(project)
    if language not in ('c', 'cpp'):
        raise ToolError('Choose c or cpp')
    if app_id is None:
        slug = re.sub(r'[^a-z0-9._-]+', '-', path.name.lower()).strip('.-_')[:60]
        app_id = 'pxa-' + (slug or 'app')
    if not re.fullmatch(r'[a-z][a-z0-9._-]{0,63}', app_id):
        raise ToolError('Choose a valid lowercase PXA application ID')
    if path.exists() and any(path.iterdir()):
        raise ToolError('init requires an empty directory; existing files were retained')
    scope.app(env, path)
    if language == 'cpp':
        log = env.call(['init', path, '--id', app_id])
    else:
        path.mkdir(parents=True, exist_ok=True)
        template = KIT / 'templates/c-app'
        for source in template.iterdir():
            (path / source.name).write_bytes(source.read_bytes())
        metadata = json.loads((path / 'package.json').read_text())
        metadata.update(id=app_id, name=path.name)
        atomic_json(path / 'package.json', metadata)
        log = 'Created C application with the installed guest-c SDK'
    scope.enforce(env.workspace)
    return {'project': str(path), 'language': language, 'log': log}

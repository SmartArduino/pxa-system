"""Build and submit independently signed target variants using existing SDK caches."""
import hashlib
import json
from pathlib import Path

from .common import ToolError, atomic_json, digest, evidence
from .projects import build_targets
from . import store

TARGETS = {'esp32s3': 'esp32-s3', 'esp32s31': 'esp32-s31', 'esp32p4': 'esp32-p4',
           'simulator': 'linux-x86_64', 'windows-simulator': 'windows-x86_64', 'wasm': None}


def check_variant(identity, target):
    if target not in TARGETS:
        raise ToolError('Unsupported store package target')
    expected = [] if target == 'wasm' else [TARGETS[target]]
    if identity.get('aot_targets') != expected or (target == 'wasm' and not identity.get('contains_wasm')):
        raise ToolError('Signed package does not contain exactly the selected target: ' + target)


def release_identity(identity):
    return tuple(identity.get(key) for key in ('app_id', 'root', 'spki', 'version', 'release_sequence'))


def build(environment, project, targets):
    selected = build_targets(targets=targets).split(',')
    selected = ['wasm', *(target for target in selected if target != 'wasm')]
    project = environment.project(project)
    environment.ensure_identity(project)
    metadata = json.loads((project / 'package.json').read_text())
    # Keep every published variant outside the source inventory. Custom sibling
    # outputs inside the app are legitimate inputs to the pinned SDK's checks.
    outputs = evidence(environment.workspace, 'pxa-store-build')
    manifest = outputs / 'packages.json'
    packages, identity_before = [], None
    for target in selected:
        prefix = outputs / (metadata['id'] + '-' + target)
        package = Path(str(prefix) + '.pxa')
        arguments = ['build', project, '--target', target, '--output', prefix, '--offline']
        if target != 'wasm':
            arguments.append('--aot-only')
        # The pinned SDK shares its Wasm/resource/AOT caches across these calls.
        # Distinct output prefixes retain every variant instead of overwriting.
        environment.call(arguments, timeout=600)
        identity = store.package_identity(environment, package)
        check_variant(identity, target)
        expected = release_identity(identity)
        if identity_before is None:
            identity_before = expected
        if expected != identity_before or identity['app_id'] != metadata['id']:
            raise ToolError('App identity, signer, version or sequence changed between target builds')
        environment.call(['publish', package, '--check-only'], timeout=120)
        packages.append({'target': target, 'path': package.relative_to(environment.workspace).as_posix(),
                         'sha256': digest(package), 'bytes': package.stat().st_size})
    document = {'schema': 'pxa-store-packages-1', 'app_id': identity['app_id'],
                'path_base': 'workspace', 'project': project.relative_to(environment.workspace).as_posix(),
                'version': identity['version'], 'release_sequence': identity['release_sequence'],
                'publisher_key_id': hashlib.sha256(identity['spki']).hexdigest(),
                'lineage_root_key_id': identity['root'], 'packages': packages}
    atomic_json(manifest, document)
    return {**document, 'manifest': str(manifest), 'combined_package': False,
            'packages_verified': True, 'next': 'Verify each requested runtime, then store publish-targets PROJECT MANIFEST.'}


def publish(environment, project, manifest, *, server='https://app.doit.am', icon=None, cover=None,
            screenshots=(), skip_icon=False, skip_cover=False, changelog=''):
    project, manifest = environment.project(project), environment.project(manifest)
    document = json.loads(manifest.read_text())
    packages = document.get('packages')
    if (document.get('schema') != 'pxa-store-packages-1' or document.get('path_base') != 'workspace'
            or not isinstance(packages, list)
            or not 1 <= len(packages) <= len(TARGETS)):
        raise ToolError('Choose a store build packages.json containing 1..6 target packages')
    metadata = json.loads((project / 'package.json').read_text())
    if document.get('app_id') != metadata['id']:
        raise ToolError('Store package matrix belongs to another application')
    prepared, seen, common = [], set(), None
    # Validate the entire set before uploading metadata, media or any package.
    for item in packages:
        if not isinstance(item, dict):
            raise ToolError('Each store package must describe a target, relative path and digest')
        target, relative = item.get('target'), item.get('path')
        if (target not in TARGETS or target in seen or not isinstance(relative, str)
                or Path(relative).is_absolute() or '..' in Path(relative).parts):
            raise ToolError('Store package targets must be unique with workspace-relative paths')
        seen.add(target)
        path = environment.project(relative)
        if not path.is_relative_to(manifest.parent) or not path.is_file() or digest(path) != item.get('sha256'):
            raise ToolError('Store package is missing, changed or outside its build directory: ' + target)
        identity = store.package_identity(environment, path)
        check_variant(identity, target)
        current = release_identity(identity)
        if common is None:
            common = current
        if (current != common or identity['app_id'] != metadata['id']
                or identity['version'] != document.get('version')
                or identity['release_sequence'] != document.get('release_sequence')
                or hashlib.sha256(identity['spki']).hexdigest() != document.get('publisher_key_id')
                or identity['root'] != document.get('lineage_root_key_id')):
            raise ToolError('Store variants must share the app, signer, version, sequence and signing lineage')
        environment.call(['build', project, '--target', target, '--output', path.with_suffix(''),
                          '--check-existing', '--offline'], timeout=120)
        environment.call(['publish', path, '--check-only'], timeout=120)
        prepared.append((target, path, item['sha256']))
    if 'wasm' not in seen:
        raise ToolError('Store submission requires a separate Wasm package for the same release. Run store build again.')
    # Upload portable code first. A failed batch must not leave only native code.
    prepared.sort(key=lambda item: item[0] != 'wasm')
    completed = []
    for index, (target, path, expected) in enumerate(prepared):
        try:
            if digest(path) != expected:
                raise ToolError('Store package changed after preflight: ' + target)
            result = store.publish(environment, project, path, server=server,
                icon=icon if index == 0 else None, cover=cover if index == 0 else None,
                screenshots=screenshots if index == 0 else (),
                skip_icon=skip_icon if index == 0 else True, skip_cover=skip_cover if index == 0 else True,
                changelog=changelog, update_listing=index == 0)
            completed.append({'target': target, 'package': str(path), **result})
        except (ToolError, OSError) as error:
            return {'passed': False, 'completed': completed, 'failed_target': target,
                    'remaining_targets': [t for t, _, _ in prepared[index:]], 'error': str(error),
                    'next': 'Inspect store state before retrying only the unfinished targets; uploads are not atomic.'}
    return {'passed': True, 'status': 'pending_review', 'completed': completed, 'publicly_listed': False}

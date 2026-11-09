"""Local build input snapshots for explicit, checked artifact reuse."""
from __future__ import annotations

import json
import os
from pathlib import Path

from .common import ToolError, command, digest

IGNORED = {'.git', '.pxa', '__pycache__'}


def output_prefix(project, output=None):
    return (Path(output).absolute().parent.resolve() / Path(output).name if output
            else project.path / 'dist' / project.app_id)


def source_snapshot(project, output=None):
    prefix = output_prefix(project, output)
    roots = [project.path]
    common = project.path.parent / 'common'
    if common.is_dir():
        roots.append(common)
    source = (project.path / project.metadata.get('build', {}).get('source_dir', '.')).resolve()
    if source != project.path:
        roots.append(source)
    files, links = {}, {}
    for root in roots:
        visited = set()
        for directory, folders, names in os.walk(root, followlinks=True):
            here = Path(directory)
            real = here.resolve()
            if real in visited:
                folders[:] = []
                continue
            visited.add(real)
            # Build output may be a custom prefix inside the app. Exclude only
            # its published files/generations, not unrelated sibling sources.
            def generated(path):
                return path.parent == prefix.parent and (
                    path == prefix or path.name in {prefix.name + suffix for suffix in
                        ('.pxa', '.pxa.build.json', '.pxa.provenance.json')} or
                    path.name in {'.' + prefix.name + '.next', '.' + prefix.name + '.pxa.next'} or
                    path.name.startswith('.' + prefix.name + '-'))
            folders[:] = [name for name in folders if name not in IGNORED and not generated(here / name)]
            for name in folders:
                path = here / name
                if path.is_symlink():
                    links[str(path)] = str(path.resolve())
            for name in names:
                path = here / name
                if not generated(path) and path.is_file():
                    files[str(path)] = [str(path.resolve()), digest(path)]
            if len(files) > 200000:
                raise ToolError('Build source inventory exceeds 200000 files')
    return {'roots': sorted(str(p) for p in roots), 'files': files, 'directory_links': links}


def dependency_snapshot(build_dir, ninja, targets):
    """Read CMake scripts, declared build inputs and discovered compiler headers."""
    reply = build_dir / '.cmake/api/v1/reply'
    indices = sorted(reply.glob('index-*.json'), key=lambda p: p.stat().st_mtime_ns)
    if not indices:
        raise ToolError('CMake did not emit its dependency inventory')
    index = json.loads(indices[-1].read_text())
    reference = next(obj for obj in index['objects'] if obj['kind'] == 'cmakeFiles')
    document = json.loads((reply / reference['jsonFile']).read_text())
    source = Path(document['paths']['source'])
    paths = {(source / item['path']).resolve() for item in document['inputs'] if not item.get('isGenerated')}
    # Compiler depfiles alone omit external inputs of custom generators. Ask
    # Ninja for the transitive graph of exactly the targets we built. Raw NUL
    # delimiters preserve spaces and native Windows paths without shell parsing.
    graph = command([ninja, '-C', build_dir, '-t', 'inputs', '-0', '-E', *targets], capture=True, strip=False)
    paths.update((build_dir / name).resolve() for name in graph.split('\0') if name)
    database = json.loads(command([ninja, '-C', build_dir, '-t', 'compdb'], capture=True))
    objects = sorted({item['output'] for item in database if item.get('output', '').endswith(('.o', '.obj'))})
    # Query only objects still in the current build graph. Ninja's persistent
    # log can retain dependencies of sources deleted in an earlier revision.
    dependencies = command([ninja, '-C', build_dir, '-t', 'deps', *objects], capture=True) if objects else ''
    for line in dependencies.splitlines():
        if line.startswith('    '):
            paths.add((build_dir / line[4:]).resolve())
    result = {}
    for path in sorted(paths):
        if path.is_relative_to(build_dir.resolve()) or path.is_dir():
            continue  # Generated/cache files are not needed to run a built package.
        if not path.is_file():
            raise ToolError('Build dependency disappeared: ' + str(path))
        result[str(path)] = digest(path)
    return result


def check_inputs(project, receipt, output=None):
    inputs = receipt.get('inputs')
    if not isinstance(inputs, dict) or 'source' not in inputs or not isinstance(inputs.get('dependencies'), dict):
        raise ToolError('This build has no reusable input inventory; build it once with this SDK')
    if source_snapshot(project, output) != inputs['source']:
        raise ToolError('Source/resources changed since the build; rebuild before using --no-build')
    for name, expected in inputs['dependencies'].items():
        path = Path(name)
        if not path.is_file() or digest(path) != expected:
            raise ToolError('Build dependency changed or is missing: ' + name + '; rebuild before using --no-build')

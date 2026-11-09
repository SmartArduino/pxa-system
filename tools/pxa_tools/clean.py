"""Explicit project-cache cleanup, coordinated with the normal build lock."""
from __future__ import annotations

import json
from pathlib import Path
import re
import shutil

from .common import ToolError, identity, locked


def cache_bytes(root):
    return sum(path.stat().st_size for path in root.rglob('*')
               if path.is_file() and not path.is_symlink()) if root.is_dir() else root.stat().st_size


def clean(project, *, keep=2, dry_run=False, aot=False):
    if type(keep) is not int or not 0 <= keep <= 100:
        raise ToolError('Keep must be in 0..100')
    state, dist = project.path / '.pxa', project.path / 'dist'
    roots = (state, state / 'build', state / 'cache', state / 'cache/packages',
             state / 'cache/aot', dist)
    if any(path.is_symlink() for path in roots):
        raise ToolError('Cache cleanup does not follow linked state/output directories')
    protected_content, protected_builds, protected_dist = set(), set(), set()
    rows = []
    with locked(state / 'build.lock'):
        for receipt in dist.glob('*.pxa.build.json'):
            if receipt.is_symlink() or receipt.stat().st_size > 16 * 1024**2:
                raise ToolError('Inspect the linked or oversized build receipt before cleanup')
            try:
                record = json.loads(receipt.read_text())
                content, configuration = record['content_id'], record['configuration']
                if not re.fullmatch('[0-9a-f]{64}', content) or not isinstance(configuration, dict):
                    raise ValueError('invalid receipt')
                protected_content.add(content)
                protected_builds.add(identity(configuration)[:20])
            except (OSError, ValueError, KeyError, TypeError) as error:
                raise ToolError('Inspect the invalid build receipt before cleanup: ' + str(receipt)) from error
        for link in dist.iterdir() if dist.is_dir() else ():
            if link.is_symlink():
                target = link.resolve()
                if target.parent == dist.resolve():
                    protected_dist.add(target.name)

        def choose(directory, pattern, protected):
            candidates = [path for path in directory.glob('*')
                          if path.is_dir() and not path.is_symlink() and pattern.fullmatch(path.name)]
            candidates.sort(key=lambda path: (path.stat().st_mtime_ns, path.name), reverse=True)
            return [path for path in candidates[keep:] if path.name not in protected]

        generations = re.compile(re.escape('.' + project.metadata['id'] + '-') + '[0-9a-f]{20}')
        protected_dist.update('.' + project.metadata['id'] + '-' + content[:20]
                              for content in protected_content)
        # Retain package caches referenced by currently published symlinks too,
        # including projects built by a legacy SDK without modern receipts.
        for package in (state / 'cache/packages').glob('*'):
            if any(name.endswith('-' + package.name[:20]) for name in protected_dist):
                protected_content.add(package.name)
        candidates = [*choose(state / 'build', re.compile('[0-9a-f]{20}'), protected_builds),
                      *choose(state / 'cache/packages', re.compile('[0-9a-f]{64}'), protected_content),
                      *choose(dist, generations, protected_dist)]
        if aot:
            candidates += [path for path in (state / 'cache/aot').glob('*.aot')
                           if path.is_file() and not path.is_symlink() and
                           re.fullmatch('[0-9a-f]{64}\.aot', path.name)]
        for path in candidates:
            size = cache_bytes(path)
            if not dry_run:
                shutil.rmtree(path) if path.is_dir() else path.unlink()
            rows.append({'path': str(path.relative_to(project.path)), 'bytes': size})
    return {'dry_run': dry_run, 'keep': keep, 'aot_cache_included': aot,
            'bytes': sum(row['bytes'] for row in rows), 'entries': rows,
            'retained': 'published packages, referenced generations/configurations, source, locks, keys and simulator saves'}

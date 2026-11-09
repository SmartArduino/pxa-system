"""Explicit cache housekeeping; SDKs, applications, identities and saves stay put."""
from __future__ import annotations

import argparse
from pathlib import Path
import re

from .common import ToolError, data_root, locked, signed_document


def archive_candidates():
    known = set()
    skipped = []
    for receipt in sorted((data_root() / 'sdks').glob('*/distribution.json')):
        if receipt.is_symlink() or receipt.parent.is_symlink():
            skipped.append(str(receipt))
            continue
        try:
            document, _, _ = signed_document(receipt, schema='pxa-distribution-1')
            candidates = set()
            if document['version'] != receipt.parent.name:
                raise ToolError('SDK receipt version mismatch')
            for item in document['components']:
                sha = item['sha256']
                if not isinstance(sha, str) or not re.fullmatch('[0-9a-f]{64}', sha):
                    raise ToolError('Invalid SDK archive hash')
                candidates.add(sha)
            known.update(candidates)
        except (ToolError, OSError, ValueError, KeyError, TypeError):
            # A damaged receipt is a repair issue, not deletion authority.
            skipped.append(str(receipt))
    return known, skipped


def discard_archives(hashes, *, dry_run=False):
    rows = []
    for sha in sorted(set(hashes)):
        if not re.fullmatch('[0-9a-f]{64}', sha):
            raise ToolError('Invalid cache hash')
        path = data_root() / 'downloads' / sha
        if path.is_symlink() or not path.is_file():
            continue
        with locked(path.with_suffix('.lock')):
            if path.is_symlink() or not path.is_file():
                continue
            size = path.stat().st_size
            if not dry_run:
                path.unlink()
            rows.append({'sha256': sha, 'bytes': size})
    return {'dry_run': dry_run, 'archive_count': len(rows),
            'bytes': sum(row['bytes'] for row in rows), 'archives': rows}


def usage():
    known, skipped = archive_candidates()
    rows = []
    for path in sorted((data_root() / 'downloads').glob('*')):
        if re.fullmatch('[0-9a-f]{64}', path.name) and path.is_file() and not path.is_symlink():
            rows.append({'sha256': path.name, 'bytes': path.stat().st_size,
                         'installed_sdk_archive': path.name in known})
    return {'download_bytes': sum(row['bytes'] for row in rows),
            'prunable_sdk_bytes': sum(row['bytes'] for row in rows if row['installed_sdk_archive']),
            'archives': rows, 'unverified_receipts_skipped': skipped,
            'scope': 'download archives only; installed SDKs, firmware images and project data retained'}


def command(environment, arguments):
    parser = argparse.ArgumentParser(prog='pxa sdk')
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('cache', help='show downloaded archive usage')
    prune = commands.add_parser('prune', help='remove archives referenced by verified installed SDK receipts')
    prune.add_argument('--dry-run', action='store_true')
    repair = commands.add_parser('repair', help='explicitly reinstall a signed native SDK, preserving project locks')
    repair.add_argument('--sdk-version')
    repair.add_argument('--manifest')
    repair.add_argument('--keep-archives', action='store_true')
    repair.add_argument('--offline', action='store_true')
    args = parser.parse_args(arguments)
    if args.action == 'cache':
        return usage()
    if args.action == 'prune':
        hashes, skipped = archive_candidates()
        return {**discard_archives(hashes, dry_run=args.dry_run),
                'unverified_receipts_skipped': skipped}
    if environment.config.get('engine') != 'native':
        raise ToolError('SDK repair currently applies to signed native releases; container images use engine load/setup')
    version = args.sdk_version or environment.config.get('sdk_version')
    if not args.sdk_version and Path(environment.config.get('sdk', '')).resolve() != (
            data_root() / 'sdks' / str(version)).resolve():
        raise ToolError('This workspace uses a custom SDK. Repair a managed release with --sdk-version; the selected SDK stays unchanged')
    from .environment import install_native
    sdk = install_native(version, args.manifest, keep_archives=args.keep_archives, repair=True, offline=args.offline)
    return {'sdk': str(sdk), 'version': version, 'repaired': True,
            'workspace_configuration_changed': False, 'project_locks_changed': False}

"""Small, local, version-matched SDK excerpts. No model/network dependency."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


def sdk_context(root, query='', language='cpp', limit=12):
    root = Path(root).resolve()
    if language not in ('c', 'cpp') or not 1 <= limit <= 30 or len(query) > 160:
        raise ValueError('Choose c/cpp, limit 1..30 and a query up to 160 characters')
    sdk = root / 'sdk' / ('guest-c' if language == 'c' else 'guest-cpp')
    terms = [t.casefold() for t in re.split(r'\s+|::', query.strip()) if t]
    candidates, examples, headers = [], [], []
    for path in sorted((sdk / 'include').rglob('*')):
        if not path.is_file() or path.is_symlink() or path.stat().st_size > 1024**2:
            continue
        relative = path.relative_to(root).as_posix()
        if path.suffix not in ('.h', '.hpp', '.hh', '.hxx'):
            continue
        headers.append(relative)
        if not terms:
            continue
        lines = path.read_text(errors='replace').splitlines()
        for index, line in enumerate(lines):
            if not all(t in (relative + ' ' + line).casefold() for t in terms):
                continue
            stripped = line.strip()
            if not stripped or stripped.startswith(('//', '*', '/*', '#include', '#ifndef', '#endif', '#pragma')):
                continue
            tokens = set(re.findall(r'[a-z_][a-z_0-9]*', line.casefold()))
            # Exact symbols beat substrings; declarations beat callers. Path
            # matches still make queries like "ui Button" useful in C++ headers.
            exact = sum(term in tokens for term in terms)
            declaration = bool(re.search(r'\b(class|struct|enum|using|typedef)\b|#define|\)\s*(?:const\s*)?(?:\{|;)', stripped))
            score = 100 * exact + 20 * declaration + 5 * sum(term in line.casefold() for term in terms)
            if 'generated' in relative or '/detail/' in relative:
                score -= 10
            candidates.append((score, relative, index, lines))
    matches = []
    for score, relative, index, lines in sorted(candidates, key=lambda item: (-item[0], item[1], item[2])):
        # One excerpt already includes adjacent declarations; do not waste the
        # context budget returning the same lines several times.
        if any(item['path'] == relative and abs(item['line'] - index - 1) < 4 for item in matches):
            continue
        start = max(0, index - 1)
        matches.append({'path': relative, 'line': index + 1, 'excerpt_start_line': start + 1,
                        'excerpt': '\n'.join(lines[start:index + 5])[:1000]})
        if len(matches) == limit:
            break
    for path in sorted((sdk / 'examples').glob('*/main.*')):
        if not terms or any(t in path.parent.name.casefold() for t in terms):
            examples.append(path.relative_to(root).as_posix())
    return {'sdk_root': str(root), 'release': (root / 'VERSION').read_text().strip(),
            'language': language, 'query': query, 'matches': matches, 'examples': examples[:12],
            'headers': [name for name in headers if not terms or any(term in name.casefold() for term in terms)][:limit],
            'header_count': len(headers),
            'include_root': str(sdk / 'include'), 'read_only': True,
            'next': 'Read the matched declaration and nearby usage before editing the app. No match is not proof of an unsupported API.'}


def project_context(env, project, query='', language='cpp', limit=12):
    from .common import run
    path = env.project(project)
    package = json.loads((path / 'package.json').read_text())
    if env.native:
        sdk = sdk_context(env.config['sdk'], query, language, limit)
    else:
        command = env.invocation(['/pxa-agent-kit/pxa_agent/context.py', '/opt/pxa',
                                  '--query', query, '--language', language, '--limit', str(limit)],
                                 entrypoint='/opt/pxa/runtime/python/bin/python3')
        sdk = json.loads(run(command, cwd=env.workspace))
    lock = path / 'pxa.lock'
    return {'project': str(path), 'app_id': package.get('id'), 'version': package.get('version'),
            'components': package.get('components'), 'build': package.get('build'),
            'lock': json.loads(lock.read_text()) if lock.exists() else None, 'sdk': sdk,
            'workflow': ['scope check', 'build ' + str(path), 'verify ' + str(path) + ' --profiles pai-touch'],
            'boundaries': 'Edit this app only. SDK, ABI, runtime and board sources are read-only.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root')
    parser.add_argument('--query', default='')
    parser.add_argument('--language', choices=('c', 'cpp'), default='cpp')
    parser.add_argument('--limit', type=int, default=12)
    args = parser.parse_args()
    print(json.dumps(sdk_context(args.root, args.query, args.language, args.limit), ensure_ascii=False))

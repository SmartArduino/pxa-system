#!/usr/bin/env python3
"""Reject prohibited/unknown SPDX licenses and unreviewed copyleft notices.

This is a build gate, not a provenance detector. Files without an SPDX tag
inherit their upstream license; dependency license selection and attribution
must also be reviewed when adding/updating a dependency.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

ALLOWED = frozenset({'MIT','MIT-0','BSD-2-Clause','BSD-2-Clause-FreeBSD','BSD-3-Clause','BSD-4-Clause',
    'Apache-2.0','ISC','Zlib','BSL-1.0','CC0-1.0','Unlicense','WTFPL','FTL',
    'libpng-2.0','Libpng','IJG','OpenSSL','SSLeay','Unicode-DFS-2016',
    'Unicode-3.0','LicenseRef-Espressif-Modified-MIT'})
# Exact combinations only. GCC approval additionally requires a trusted
# toolchain path; an arbitrary GPL implementation cannot claim this exception.
EXCEPTIONS = {('Apache-2.0','LLVM-exception')}
COPYLEFT = re.compile(r'\b(?:[AL]?GPL|MPL|EPL|CDDL|EUPL|OSL|SSPL)(?:[- /\d]|\b)|'
    r'GNU\s+(?:(?:Affero|Lesser|Library)\s+)?General\s+Public\s+License|'
    r'Mozilla\s+Public\s+License',re.I)
SPDX = re.compile(r'SPDX-License-Identifier:\s*([^\r\n]+)')


def allowed_expression(expression: str, runtime: bool = False) -> bool:
    """Parse SPDX WITH > AND > OR. Unknown identifiers fail closed."""
    tokens=re.findall(r'\(|\)|[A-Za-z0-9.+_-]+',expression)
    if ''.join(tokens) != re.sub(r'\s+','',expression):return False
    cursor=0
    def atom():
        nonlocal cursor
        if cursor>=len(tokens):raise ValueError('missing operand')
        item=tokens[cursor];cursor+=1
        if item=='(':
            result=disjunction()
            if cursor>=len(tokens) or tokens[cursor]!=')':raise ValueError('parenthesis')
            cursor+=1;return result
        if item in ('AND','OR','WITH',')'):raise ValueError('operand')
        if cursor<len(tokens) and tokens[cursor]=='WITH':
            cursor+=1
            if cursor>=len(tokens):raise ValueError('exception')
            exception=tokens[cursor];cursor+=1
            return (item,exception) in EXCEPTIONS or (runtime and
                item in ('GPL-3.0-only','GPL-3.0-or-later') and exception=='GCC-exception-3.1')
        return item in ALLOWED
    def conjunction():
        nonlocal cursor
        result=atom()
        while cursor<len(tokens) and tokens[cursor]=='AND':
            cursor+=1;other=atom();result=result and other
        return result
    def disjunction():
        nonlocal cursor
        result=conjunction()
        while cursor<len(tokens) and tokens[cursor]=='OR':
            cursor+=1;other=conjunction();result=result or other
        return result
    try:return disjunction() and cursor==len(tokens)
    except ValueError:return False


def inspect(path: Path, reviewed: dict, toolchain_roots: list[Path]) -> str | None:
    text=path.read_text(errors='replace')
    runtime=any(path.is_relative_to(root) for root in toolchain_roots)
    expressions=[s.strip().removesuffix('*/').strip() for s in SPDX.findall(text)]
    if expressions and not all(allowed_expression(s,runtime) for s in expressions):
        return 'prohibited or unreviewed SPDX expression: '+', '.join(expressions)
    # Only an explicit SPDX OR selection permits a copyleft alternative.
    # A nested MPL notice inside a generally MIT file must still be rejected.
    without_spdx=SPDX.sub('',text)
    if COPYLEFT.search(without_spdx):
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        entry=reviewed.get(digest)
        if entry and allowed_expression(entry['selected']):return None
        normalized=re.sub(r'(?m)^\s*(?://|\*+)\s*','',text)
        if runtime and re.search(r'GCC Runtime Library Exception, version\s+3\.1',normalized):return None
        return 'unreviewed copyleft notice (do not strip/relabel; remove or review the alternative license)'
    return None


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands',type=Path)
    parser.add_argument('--source-root',type=Path,action='append',default=[])
    parser.add_argument('--dependencies',type=Path,help='ninja -t deps text, including compiled headers')
    parser.add_argument('--ninja-dependencies',action='store_true',help='Inspect active Ninja header dependencies directly')
    parser.add_argument('--toolchain-root',type=Path,action='append',default=[])
    parser.add_argument('--reviewed',type=Path,default=Path(__file__).with_name('reviewed-alternatives.json'))
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    files=set(); active_objects=set()
    if args.compile_commands:
        for row in json.loads(args.compile_commands.read_text()):
            path=Path(row['file']);files.add(path.resolve() if path.is_absolute() else (Path(row['directory'])/path).resolve())
            tokens=row.get('arguments') or shlex.split(row['command'])
            if '-o' in tokens:active_objects.add(tokens[tokens.index('-o')+1])
    for root in args.source_root:
        for name in subprocess.check_output(['git','-C',str(root),'ls-files','--cached','--others','--exclude-standard'],text=True).splitlines():
            path=root/name
            if path.suffix.lower() in ('.c','.cc','.cpp','.h','.hpp','.s') and path.exists():files.add(path.resolve())
    dependency_text=args.dependencies.read_text() if args.dependencies else ''
    if args.ninja_dependencies:
        if not args.compile_commands:parser.error('--ninja-dependencies requires --compile-commands')
        dependency_text=subprocess.check_output(['ninja','-C',str(args.compile_commands.parent),'-t','deps'],text=True)
    if dependency_text:
        active=False
        for line in dependency_text.splitlines():
            if line and not line.startswith(' '):active=line.split(': #deps',1)[0] in active_objects
            if active and line.startswith('    '):
                path=Path(line.strip())
                if not path.is_absolute():path=args.compile_commands.parent/path
                files.add(path)
    reviewed={row['sha256']:row for row in json.loads(args.reviewed.read_text())}
    failures=[];checked=0
    for path in sorted({p.resolve() for p in files}):
        if not path.is_file():failures.append({'path':str(path),'reason':'missing source'});continue
        reason=inspect(path,reviewed,[p.resolve() for p in args.toolchain_root]);checked+=1
        if reason:failures.append({'path':str(path),'reason':reason})
    report={'checked_files':checked,'failures':failures,'scope':'compiled inputs / requested source roots; provenance review also required'}
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    for failure in failures:print(f"LICENSE ERROR: {failure['path']}: {failure['reason']}")
    print(f'System license gate: {checked} files, {len(failures)} violations')
    return int(bool(failures) or not checked)

if __name__=='__main__':raise SystemExit(main())

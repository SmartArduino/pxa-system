"""Project-local AI adapters. Preserve user rules, other servers and permissions."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import sys
import tempfile
import tomllib

from .common import KIT, ToolError, atomic_json, run

START, END = '<!-- PXA AGENT KIT START -->', '<!-- PXA AGENT KIT END -->'
TOML_START, TOML_END = '# PXA AGENT KIT START', '# PXA AGENT KIT END'


def checked(workspace, name):
    path = workspace / name
    current = path
    while current != workspace:
        if current.is_symlink():
            raise ToolError('Refusing a symlink integration target: ' + str(current))
        current = current.parent
    return path


def replace_block(text, body, previous, *, markers=(START, END)):
    begin, end = markers
    if text.count(begin) != text.count(end) or text.count(begin) > 1:
        raise ToolError('Invalid PXA integration markers; existing content was retained')
    block = begin + '\n' + body.strip() + '\n' + end
    existing = re.search(re.escape(begin) + r'.*?' + re.escape(end), text, re.S)
    if existing:
        old = existing.group()
        if old != block and hashlib.sha256(old.encode()).hexdigest() != previous:
            raise ToolError('The PXA integration block was edited locally; existing content was retained')
        return text[:existing.start()] + block + text[existing.end():], hashlib.sha256(block.encode()).hexdigest()
    return text.rstrip() + ('\n\n' if text.strip() else '') + block + '\n', hashlib.sha256(block.encode()).hexdigest()


def install(workspace, *, platforms='all', mcp=True):
    workspace = Path(workspace).resolve()
    uv = shutil.which('uv')
    if not uv:
        raise ToolError('Install uv using https://docs.astral.sh/uv/getting-started/installation/')
    command = [str(Path(uv).resolve()), 'run', '--python', '3.12', '--locked', '--project', str(KIT),
               str(KIT / 'agent.py'), '--workspace', str(workspace)]
    receipt_path = checked(workspace, '.pxa/agent-integration.json')
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {'blocks': {}, 'files': {}, 'servers': {}}
    changes, blocks, files, servers = {}, dict(receipt['blocks']), dict(receipt['files']), dict(receipt['servers'])
    instruction = f'''PXA development is configured in this workspace. No ESP-IDF or model API key is needed.
Use the local launcher `.pxa/pxa` (Windows: `.pxa/pxa.cmd`) from this workspace.
Start with `doctor`; read the installed pxa-app-dev and pxa-debug skills for implementation and verification.
Create apps in `apps/<name>` with `init`; preserve existing app IDs, signing identity, SDK locks and saves.
Implement the user's goal, build it, run the real Guest, exercise the relevant controls and inspect screenshots.
Continue fixing observed failures and incorporate user feedback; a successful build alone does not complete the goal.
Use `verify <app>` for repeatable multi-screen checks. Runtime checks do not replace visual review or prove hardware FPS.
Write test evidence under ignored `local/artifacts/`. Keep private signing keys and store tokens outside the project.
For a connected device, discover ports and read INFO before selecting its CPU, board or package. Keep existing device data.
Use `firmware prepare` before an authorized firmware upgrade. App-only flashing requires the actual partition layout to match.
Full flashing erases data and requires explicit data-loss authorization. A generic app-development request does not authorize it.
Use `store authorize` for browser-assisted, app-scoped upload access. User-authorized publishing can proceed without asking again.
`store publish` submits a signed package for review; it does not bypass store review or automatically make it public.
Use existing user authorization for matching device tests, upgrades and publishing; do not add repeated confirmation steps.'''
    targets = ['AGENTS.md']
    if platforms in ('claude', 'all'):
        targets.append('CLAUDE.md')
    for name in targets:
        path = checked(workspace, name)
        text, blocks[name] = replace_block(path.read_text() if path.exists() else '', instruction, blocks.get(name))
        changes[name] = text
    if mcp and platforms in ('codex', 'all'):
        name = '.codex/config.toml'
        path = checked(workspace, name)
        text = path.read_text() if path.exists() else ''
        parsed = tomllib.loads(text)
        if 'pxa' in parsed.get('mcp_servers', {}) and TOML_START not in text:
            raise ToolError('An existing Codex MCP server named pxa was retained')
        body = '[mcp_servers.pxa]\ncommand = ' + json.dumps(command[0]) + '\nargs = ' + json.dumps(command[1:] + ['mcp'])
        text, blocks[name] = replace_block(text, body, blocks.get(name), markers=(TOML_START, TOML_END))
        tomllib.loads(text)
        changes[name] = text
    if mcp and platforms in ('claude', 'all'):
        name = '.mcp.json'
        path = checked(workspace, name)
        config = json.loads(path.read_text()) if path.exists() else {}
        entry = {'command': command[0], 'args': command[1:] + ['mcp']}
        old = config.setdefault('mcpServers', {}).get('pxa')
        if old is not None and old != entry and hashlib.sha256(json.dumps(old, sort_keys=True).encode()).hexdigest() != servers.get(name):
            raise ToolError('An existing Claude MCP server named pxa was retained')
        config['mcpServers']['pxa'] = entry
        servers[name] = hashlib.sha256(json.dumps(entry, sort_keys=True).encode()).hexdigest()
        changes[name] = json.dumps(config, ensure_ascii=False, indent=2) + '\n'
    # DSH discovers the shared .agents skills directly. Provide the official
    # Cordis entry without rewriting a user's global deployment composition.
    if platforms in ('dsh', 'all'):
        name = '.pxa/dsh-mcp.yaml'
        changes[name] = ('- id: mcp-pxa\n  name: "@deepseek-ai/dsh-mcp-client"\n  config:\n'
                         '    serverName: pxa\n    transport: stdio\n    command: ' + json.dumps(command[0])
                         + '\n    args: ' + json.dumps(command[1:] + ['mcp']) + '\n')
    posix = '#!/bin/sh\nexec ' + ' '.join(shlex.quote(arg) for arg in command) + ' "$@"\n'
    powershell = '& ' + ' '.join("'" + arg.replace("'", "''") + "'" for arg in command) + ' @args\nexit $LASTEXITCODE\n'
    batch = '@echo off\r\nsetlocal DisableDelayedExpansion\r\n' + ' '.join('"' + arg.replace('%', '%%') + '"' for arg in command) + ' %*\r\nexit /b %ERRORLEVEL%\r\n'
    changes.update({'.pxa/pxa': posix, '.pxa/pxa.ps1': powershell, '.pxa/pxa.cmd': batch})
    for name in ['.pxa/pxa', '.pxa/pxa.ps1', '.pxa/pxa.cmd', '.pxa/dsh-mcp.yaml']:
        if name not in changes:
            continue
        path = checked(workspace, name)
        expected = hashlib.sha256(changes[name].encode()).hexdigest()
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() not in (files.get(name), expected):
            raise ToolError('Locally edited PXA launcher retained: ' + name)
        files[name] = expected
    ignore = checked(workspace, '.gitignore')
    text = ignore.read_text() if ignore.exists() else ''
    local_configs = [name for name in ('.codex/config.toml', '.mcp.json') if name in changes and not (workspace / name).exists()]
    for entry in ('.pxa/', 'local/artifacts/', *local_configs):
        if entry not in text.splitlines():
            text = text.rstrip() + '\n' + entry + '\n'
    changes['.gitignore'] = text
    # Check every destination before mutating any user configuration.
    for name in changes:
        checked(workspace, name)
    run([sys.executable, KIT / 'install.py', 'install', '--project', workspace,
         '--platform', platforms, '--agents', '--update', '--dry-run'])
    run([sys.executable, KIT / 'install.py', 'install', '--project', workspace,
         '--platform', platforms, '--agents', '--update'])
    for name, text in changes.items():
        path = workspace / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != text.encode():
            descriptor, temporary = tempfile.mkstemp(prefix='.pxa-ai-', dir=path.parent)
            try:
                with os.fdopen(descriptor, 'wb') as stream:
                    stream.write(text.encode())
                os.chmod(temporary, 0o755 if name == '.pxa/pxa' else 0o644)
                os.replace(temporary, path)
            finally:
                Path(temporary).unlink(missing_ok=True)
    atomic_json(receipt_path, {'blocks': blocks, 'files': files, 'servers': servers})
    return {'platforms': platforms, 'mcp': mcp, 'launcher': str(workspace / '.pxa/pxa'),
            'skills': str(workspace / '.agents/skills'), 'global_settings_changed': False}

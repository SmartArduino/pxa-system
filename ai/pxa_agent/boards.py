"""Board-local scaffolding and static preflight against the current board port."""
from __future__ import annotations

import json
from pathlib import Path
import re

from .common import ToolError, atomic_json, evidence, run


def setup(workspace, platform='all'):
    from . import VERSION
    from .integration import install
    root, _ = contract(workspace)
    path = root / '.pxa/agent.json'
    if path.exists() and json.loads(path.read_text()).get('mode') != 'board':
        raise ToolError('An app environment already exists here; use a separate firmware worktree')
    from .scope import finish
    finish(root)
    integration = install(root, platforms=platform, mode='board')
    atomic_json(path, {'schema': 'pxa-agent-workspace-1', 'mode': 'board', 'engine': 'native',
                      'kit_version': VERSION, 'sdk': str(root / 'deps/pxa-system')})
    return {'mode': 'board', 'integration': integration, 'idf_installed': False,
            'next': 'Use board init or scope begin --mode board --board NAME before editing'}


def build(workspace, name):
    from .scope import enforce
    root, _ = contract(workspace)
    result = inspect(root, name)
    if not result['static_ready']:
        raise ToolError('Board preflight failed: ' + json.dumps(result, ensure_ascii=False))
    output = evidence(root, 'pxa-board-build')
    try:
        log = run([root / 'tools/firmware.sh', name, 'build'], cwd=root, timeout=1800)
    except ToolError as error:
        (output / 'build.log').write_text(str(error))
        raise
    finally:
        enforce(root)
    (output / 'build.log').write_text(log)
    return {'built': True, 'board': name, 'log': str(output / 'build.log'), 'hardware_tested': False}


def board_id(value):
    if not isinstance(value, str) or not re.fullmatch(r'[a-z][a-z0-9-]{1,47}', value):
        raise ToolError('Board ID must be 2..48 lowercase letters, digits or hyphens')
    return value


def contract(workspace):
    root = Path(workspace).resolve()
    api = root / 'firmware/components/pxa_board_api/include/pxa_board_api.h'
    if not api.is_file() or not (root / 'tools/firmware.sh').is_file():
        raise ToolError('Choose a PXA firmware workspace with the existing board-port API')
    text = api.read_text()
    for symbol in ('pxa_board_port_t', 'pxa_board_register', 'display_profile', 'initialize'):
        if symbol not in text:
            raise ToolError('Unsupported board-port contract; read its current API instead of modifying it')
    return root, api


def initialize(workspace, name, *, target, reference=None):
    from .scope import begin
    root, api = contract(workspace)
    name = board_id(name)
    if target not in ('esp32s3', 'esp32s31'):
        raise ToolError('This scaffold supports esp32s3 and esp32s31; a new CPU backend is separate platform work')
    destination = root / 'firmware/boards' / name
    if destination.exists() or destination.is_symlink():
        raise ToolError('The board directory already exists; inspect it instead of replacing it')
    if reference and not (root / 'firmware/boards' / board_id(reference) / 'board.cmake').is_file():
        raise ToolError('Reference board is missing')
    scope = begin(root, mode='board', board=name)
    spec = {'schema': 'pxa-board-port-1', 'board': name, 'target': target, 'reference': reference,
            'schematic': None, 'flash_mb': None, 'psram_mb': None,
            'display': {'controller': None, 'bus': None, 'width': None, 'height': None,
                        'rotation': None, 'rgb565_byte_order': None},
            'pins': {}, 'input': [], 'power': None, 'storage': None}
    files = {
        'board.cmake': f'set(PXA_BOARD_TARGET "{target}")\nset(PXA_BOARD_COMPONENT "{name}")\n',
        'CMakeLists.txt': 'idf_component_register(SRCS "src/board.cc" INCLUDE_DIRS "include"\n    PRIV_REQUIRES lvgl pxa_board_api)\n',
        'idf_component.yml': 'dependencies: {}\n# Add only this board\'s physical driver dependencies here.\n',
        'include/pxa_board.h': '#pragma once\n#include <stdbool.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\nbool pxa_board_register_selected(void);\n#ifdef __cplusplus\n}\n#endif\n',
        'src/board.cc': '''#include "pxa_board.h"
#include "pxa_board_api.h"

// Bring-up gate: no GPIO, power rail or flash layout is guessed.
#error PXA_BOARD_BRINGUP_REQUIRED: implement verified hardware before building

static bool initialize(void*) { return false; }
static lv_display_t* display(void*) { return nullptr; }
static void display_profile(void*, pxsys_display_profile_t*) {}

extern "C" bool pxa_board_register_selected(void) {
    static pxa_board_port_t port = {};
    port.struct_size = sizeof(port);
    port.initialize = initialize;
    port.display = display;
    port.display_profile = display_profile;
    return pxa_board_register(&port);
}
''',
        'board-port.json': json.dumps(spec, ensure_ascii=False, indent=2) + '\n',
        'sdkconfig.defaults': '# Set flash, PSRAM, storage label and mount point from verified hardware.\n',
        'partitions.csv': '# Define offsets and sizes after verifying actual flash capacity.\n',
        'AGENTS.md': f'''# {name} board port

Edit this board directory, its simulator/profiles/{name}.toml and the generated
firmware/dependencies.lock.{name} only. Do not edit PXA interfaces, SDK,
runtime, shared board components or another board.
Use the existing API at {api.relative_to(root)} read-only.
Read the installed pxa-board-port skill. Complete board-port.json from the
schematic/datasheets before implementing GPIO, power, display and storage.
Keep the compile-time bring-up gate until actual drivers and partitions exist.
Run board check and scope check before building and delivering.
'''
    }
    for relative, value in files.items():
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)
    return {'directory': str(destination), 'scope': scope, 'buildable': False,
            'next': 'Complete board-port.json, implement the existing callbacks, then board check. No firmware was flashed.'}


def inspect(workspace, name):
    from .scope import check
    root, api = contract(workspace)
    name = board_id(name)
    directory = root / 'firmware/boards' / name
    if directory.resolve() != directory or not directory.is_dir():
        raise ToolError('Choose an existing board directory without symlinks')
    required = ('board.cmake', 'CMakeLists.txt', 'idf_component.yml', 'include/pxa_board.h',
                'sdkconfig.defaults', 'partitions.csv')
    missing = [p for p in required if not (directory / p).is_file()]
    gates = []
    for path in (directory / 'src').glob('*'):
        if path.is_file() and 'PXA_BOARD_BRINGUP_REQUIRED' in path.read_text(errors='replace'):
            gates.append(str(path))
    spec = directory / 'board-port.json'
    unknown = []
    if spec.exists():
        data = json.loads(spec.read_text())
        for key in ('schematic', 'flash_mb', 'psram_mb', 'pins', 'input', 'power', 'storage'):
            if data.get(key) is None or data.get(key) == {}:
                unknown.append(key)
        for key in ('controller', 'bus', 'width', 'height', 'rotation', 'rgb565_byte_order'):
            if data.get('display', {}).get(key) is None:
                unknown.append('display.' + key)
    partitions = directory / 'partitions.csv'
    if partitions.is_file() and not any(line.strip() and not line.lstrip().startswith('#')
                                        for line in partitions.read_text().splitlines()):
        missing.append('partition entries')
    scope = check(root)
    scope_matches = scope.get('mode') == 'board' and str(directory) in scope.get('allowed', [])
    return {'board': name, 'contract': str(api), 'missing': missing, 'unknown_hardware': unknown,
            'bringup_gates': gates, 'scope': scope,
            'scope_matches_board': scope_matches,
            'static_ready': not (missing or gates or unknown) and scope.get('passed', False) and scope_matches,
            'hardware_tested': False, 'build_command': ['tools/firmware.sh', name, 'build'],
            'acceptance': ['boot/reset', 'RGB565 colors/rotation', 'display completion FPS',
                           'touch/controller/release', 'app start/stop', 'memory', 'storage persistence'],
            'note': 'Static preflight is not a firmware build or physical device acceptance.'}

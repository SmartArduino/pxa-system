"""Validated CLI arguments for the pinned SDK's semantic UI debugger."""
from .common import ToolError, atomic_json, digest

TYPES = {'root', 'box', 'scroll', 'text', 'image', 'button', 'toggle', 'slider',
         'text-input', 'select', 'progress', 'canvas', 'list', 'media-surface', 'unknown'}


def inspection_page(tree, path, *, limit=64, offset=0):
    if type(limit) is not int or not 1 <= limit <= 256 or type(offset) is not int or not 0 <= offset <= 4096:
        raise ToolError('UI inspection accepts limit 1..256 and offset 0..4096')
    if tree.get('schema') != 'pxa-ui-inspection-1' or tree.get('complete') is not True:
        raise ToolError('A complete real UI snapshot is required')
    atomic_json(path, tree)
    nodes = tree['nodes']
    return {'schema': 'pxa-ui-inspection-view-1', 'snapshot': str(path), 'sha256': digest(path),
            'snapshot_complete': True, 'view_complete': offset == 0 and len(nodes) <= limit,
            'renderer': tree['renderer'], 'viewport': tree['viewport'],
            'environment': tree.get('environment'), 'total_nodes': len(nodes),
            'offset': offset, 'nodes': nodes[offset:offset+limit]}


def query_arguments(action, selector, expected=None, timeout_ms=0):
    if action not in ('expect', 'wait-for', 'tap'):
        raise ToolError('Invalid UI query action')
    if not isinstance(selector, dict) or not selector or set(selector) - {'id', 'surface', 'type', 'text'}:
        raise ToolError('UI selector uses id, surface, type or exact text')
    if any(type(selector[k]) is not int or not 1 <= selector[k] <= 0xffffffff for k in ('id', 'surface') if k in selector):
        raise ToolError('UI node/surface IDs must be positive integers')
    if 'type' in selector and (not isinstance(selector['type'], str) or selector['type'] not in TYPES):
        raise ToolError('Unsupported UI node type')
    if 'text' in selector and (not isinstance(selector['text'], str) or len(selector['text'].encode('utf-8')) > 4096):
        raise ToolError('UI selector text must be a bounded string')
    expected = {} if expected is None else expected
    if not isinstance(expected, dict) or set(expected) - {'visible', 'enabled', 'value', 'count', 'within'}:
        raise ToolError('Invalid UI expectation fields')
    if action == 'tap' and expected:
        raise ToolError('Tap accepts a selector; assert the resulting state separately')
    if any(type(expected[k]) is not bool for k in ('visible', 'enabled') if k in expected):
        raise ToolError('UI visibility/enabled expectations must be boolean')
    if 'value' in expected and type(expected['value']) is not int:
        raise ToolError('UI value must be an integer')
    if 'count' in expected and (type(expected['count']) is not int or not 0 <= expected['count'] <= 4096):
        raise ToolError('UI count must be 0..4096')
    if 'within' in expected and (not isinstance(expected['within'], list) or len(expected['within']) != 4 or
            any(type(v) is not int for v in expected['within']) or min(expected['within'][2:]) < 0):
        raise ToolError('UI within requires [x,y,width,height]')
    if type(timeout_ms) is not int or not 0 <= timeout_ms <= 60000:
        raise ToolError('UI wait timeout must be 0..60000 ms')
    result = ['ui', action]
    for key, value in selector.items():
        # --text=value keeps a label beginning with '-' out of option parsing.
        result.append('--' + ('node' if key == 'id' else key) + '=' + str(value))
    for key, value in expected.items():
        result.append('--' + key)
        result += [str(v) for v in value] if key == 'within' else [str(value).lower() if type(value) is bool else str(value)]
    if action == 'wait-for':
        result += ['--timeout-ms', str(timeout_ms)]
    return result

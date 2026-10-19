#!/usr/bin/env python3
"""Generate identical Host/Guest span layouts from the GameRender schema."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WIDTHS = {'u8': 1, 'u16': 2, 'i16': 2, 'u32': 4, 'i32': 4}


def render(guest):
    schema = json.loads((ROOT / 'spec/draft/pxa-game-render.json').read_text())['indexed_spans']
    guard = 'PXA_' + ('GUEST_' if guest else '') + 'GAME_RENDER_SPANS_WIRE_GENERATED_H'
    lines = ['/* Generated from pxa-game-render.json. Do not edit. */',
             '#ifndef ' + guard, '#define ' + guard,
             '#include "' + ('pxa_core.h' if guest else 'pxa/wire.h') + '"',
             '#define PXA_GAME_RENDER_SPANS_MAGIC UINT32_C(%d)' % schema['magic'],
             '#define PXA_GAME_RENDER_SPANS_HEADER_BYTES %du' % schema['header_bytes'],
             '#define PXA_GAME_RENDER_SPAN_BYTES %du' % schema['span_bytes']]
    flags = 0
    for name, value in schema['flags'].items():
        lines.append('#define PXA_GAME_RENDER_SPANS_%s %du' % (name.upper(), value))
        flags |= value
    lines.append('#define PXA_GAME_RENDER_SPANS_KNOWN_FLAGS %du' % flags)
    lines += ['#ifndef PXA_GAME_RENDER_SPANS_WIRE_SHARED_H',
              '#define PXA_GAME_RENDER_SPANS_WIRE_SHARED_H']
    projected = schema['projected']
    lines += ['#define PXA_GAME_RENDER_PROJECTED_MAGIC UINT32_C(%d)' % projected['magic'],
              '#define PXA_GAME_RENDER_PROJECTION_BYTES %du' % projected['projection_bytes'],
              '#define PXA_GAME_RENDER_ROW_BYTES %du' % projected['row_bytes'],
              '#define PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES %du' % (schema['header_bytes'] + projected['projection_bytes']),
              '#define PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH 1u',
              '#define PXA_GAME_RENDER_PROJECTED_SEGMENT_PIXELS %du' % projected['segment_pixels']]
    layouts = [(schema['header'], 'spans_header', schema['header_bytes']),
               (schema['span'], 'span', schema['span_bytes']),
               (projected['projection'], 'projection', projected['projection_bytes']),
               (projected['row'], 'row', projected['row_bytes'])]
    for fields, typename, record_size in layouts:
        assert sum(WIDTHS[t] for _, t in fields) == record_size
        lines.append('typedef struct {')
        for name, kind in fields:
            lines.append('    %sint%d_t %s;' % ('' if kind[0] == 'i' else 'u', WIDTHS[kind] * 8, name))
        lines += ['} pxa_game_render_%s_wire_t;' % typename,
                  'static inline int pxa_game_render_%s_decode(const uint8_t *p, size_t n,' % typename,
                  '    pxa_game_render_%s_wire_t *v) {' % typename,
                  '    if (!p || !v || n < %du) return 0;' % record_size]
        offset = 0
        for name, kind in fields:
            width = WIDTHS[kind]
            expr = 'p[%d]' % offset if width == 1 else 'pxa_wire_generated_load_u%d(p + %d)' % (width * 8, offset)
            lines.append('    v->%s = (%sint%d_t)%s;' % (name, '' if kind[0] == 'i' else 'u', width * 8, expr))
            offset += width
        lines += ['    return 1;', '}',
                  'static inline int pxa_game_render_%s_encode(uint8_t *p, size_t n,' % typename,
                  '    const pxa_game_render_%s_wire_t *v) {' % typename,
                  '    if (!p || !v || n < %du) return 0;' % record_size]
        offset = 0
        for name, kind in fields:
            width = WIDTHS[kind]
            lines.append('    p[%d] = v->%s;' % (offset, name) if width == 1 else
                         '    pxa_wire_generated_store_u%d(p + %d, (uint%d_t)v->%s);' % (width * 8, offset, width * 8, name))
            offset += width
        lines += ['    return 1;', '}']
    return '\n'.join(lines + ['#endif /* shared Host/Guest definitions */', '#endif', ''])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for guest, path in [(False, ROOT / 'libpxa/include/pxa/game_render_spans_wire_generated.h'),
                        (True, ROOT / 'sdk/guest-c/include/pxa_game_render_spans_wire_generated.h')]:
        text = render(guest)
        if args.check:
            if not path.exists() or path.read_text() != text:
                raise SystemExit('Stale generated codec: ' + str(path))
        else:
            path.write_text(text)
    # C++ uses the same generated layouts, with its native byte/transport
    # helpers. The SDK stays freestanding and does not include the C SDK.
    text = render(True).replace('PXA_GUEST_GAME_RENDER_SPANS_WIRE_GENERATED_H',
        'PXA_CPP_GAME_RENDER_SPANS_WIRE_GENERATED_HPP')
    text = text.replace('PXA_GAME_RENDER_SPANS_WIRE_SHARED_H',
        'PXA_CPP_GAME_RENDER_SPANS_WIRE_SHARED_H')
    text = text.replace('#include "pxa_core.h"',
        '#include "core.hpp"\n#include <stdint.h>\nnamespace pxa::game::wire_spans {')
    text = text.replace('const uint8_t *p', 'const std::byte *p').replace('uint8_t *p', 'std::byte *p')
    import re
    text = re.sub(r'pxa_wire_generated_load_u(16|32)\(p \+ (\d+)\)',
        r'wire::get\1(p + \2)', text)
    text = re.sub(r'pxa_wire_generated_store_u(16|32)\(', r'wire::put\1(', text)
    text = re.sub(r'\(uint8_t\)p\[(\d+)\]', r'std::to_integer<uint8_t>(p[\1])', text)
    text = re.sub(r'p\[(\d+)\] = v->(\w+);', r'p[\1] = std::byte(v->\2);', text)
    text = re.sub(r'^(    (?:u?int\d+_t) \w+);$', r'\1 = {};', text, flags=re.MULTILINE)
    text = re.sub(r'typedef struct \{(.*?)\} (pxa_game_render_\w+);',
        r'struct \2 {\1};', text, flags=re.DOTALL)
    text = text.rsplit('\n#endif', 1)
    text = '\n} // namespace pxa::game::wire_spans\n#endif'.join(text)
    path = ROOT / 'sdk/guest-cpp/include/pxa/game_spans_wire_generated.hpp'
    if args.check:
        if not path.exists() or path.read_text() != text:
            raise SystemExit('Stale generated codec: ' + str(path))
    else:
        path.write_text(text)


if __name__ == '__main__':
    main()

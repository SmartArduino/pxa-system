"""Bounded MessageFormat subset: arguments, number, plural/selectordinal and select."""
import re
import struct
from plural_rules import CATEGORIES

NAME = re.compile(r'[a-z][a-z0-9_]*')
TYPES = {'string', 'u32', 'i32', 'u64', 'i64', 'number'}


class Parser:
    def __init__(self, text, parameters):
        if '\0' in text:
            raise ValueError('NUL is not allowed in a message')
        text.encode('utf-8')
        self.text, self.pos = text, 0
        self.parameters = sorted(parameters)
        self.types = parameters
        self.used = set()
        self.features = set()

    def whitespace(self):
        while self.pos < len(self.text) and self.text[self.pos].isspace():
            self.pos += 1

    def token(self):
        self.whitespace()
        start = self.pos
        while self.pos < len(self.text) and self.text[self.pos] not in ',{} \t\n\r':
            self.pos += 1
        return self.text[start:self.pos]

    def require(self, char):
        self.whitespace()
        if self.pos == len(self.text) or self.text[self.pos] != char:
            raise ValueError(f'expected {char!r} at character {self.pos}')
        self.pos += 1

    def block(self, depth=0, plural=None):
        if depth > 8:
            raise ValueError('message nesting exceeds 8 levels')
        result = []
        literal = []

        def flush():
            if literal:
                result.append(('literal', ''.join(literal)))
                literal.clear()

        while self.pos < len(self.text):
            c = self.text[self.pos]
            if c == '}':
                if not depth:
                    raise ValueError('unmatched closing brace')
                break
            if c == "'":
                self.pos += 1
                if self.pos < len(self.text) and self.text[self.pos] == "'":
                    literal.append("'"); self.pos += 1
                elif self.pos < len(self.text) and self.text[self.pos] in '{}#':
                    while self.pos < len(self.text):
                        c = self.text[self.pos]; self.pos += 1
                        if c == "'":
                            if self.pos < len(self.text) and self.text[self.pos] == "'":
                                literal.append("'"); self.pos += 1
                            else:
                                break
                        else:
                            literal.append(c)
                    else:
                        raise ValueError('unterminated apostrophe quotation')
                else:
                    literal.append("'")
                continue
            if c == '#' and plural is not None:
                flush(); result.append(('argument', plural, 1)); self.pos += 1; continue
            if c != '{':
                literal.append(c); self.pos += 1; continue
            flush(); self.pos += 1
            name = self.token()
            if name not in self.types:
                raise ValueError(f'undeclared placeholder {name!r}')
            self.used.add(name)
            index = self.parameters.index(name)
            self.whitespace()
            if self.pos < len(self.text) and self.text[self.pos] == '}':
                self.pos += 1; result.append(('argument', index, 0)); continue
            self.require(','); kind = self.token()
            if kind == 'number':
                if self.types[name] == 'string':
                    raise ValueError('number requires a numeric placeholder')
                self.require('}'); result.append(('argument', index, 1)); self.features.add(kind); continue
            if kind not in ('plural', 'selectordinal', 'select'):
                raise ValueError(f'unsupported format {kind!r}')
            if (kind == 'select') != (self.types[name] == 'string'):
                raise ValueError(f'{kind} has incompatible placeholder type')
            self.features.add(kind)
            self.require(',')
            branches = []
            selectors = set()
            while True:
                self.whitespace()
                if self.pos < len(self.text) and self.text[self.pos] == '}':
                    self.pos += 1; break
                selector = self.token()
                if not selector or selector in selectors:
                    raise ValueError('missing or duplicate selector')
                if kind != 'select':
                    if selector.startswith('='):
                        if not re.fullmatch(r'=-?\d+', selector) or not -(2**63) <= int(selector[1:]) < 2**64:
                            raise ValueError('exact plural selector must be a 64-bit integer')
                        selector = '=' + str(int(selector[1:]))
                    elif selector not in CATEGORIES:
                        raise ValueError(f'invalid plural selector {selector!r}')
                elif not NAME.fullmatch(selector):
                    raise ValueError('select requires lowercase identifier selectors')
                if selector in selectors:
                    raise ValueError('duplicate canonical selector')
                selectors.add(selector)
                self.require('{')
                body = self.block(depth + 1, index if kind != 'select' else plural)
                self.require('}')
                branches.append((selector, body))
                if len(branches) > 255:
                    raise ValueError('too many branches')
            if 'other' not in selectors:
                raise ValueError(f'{kind} requires an other branch')
            result.append((kind, index, branches))
        flush()
        return result

    def parse(self):
        if len(self.parameters) > 32 or any(not NAME.fullmatch(n) or t not in TYPES for n, t in self.types.items()):
            raise ValueError('invalid placeholder declarations (maximum 32)')
        result = self.block()
        if self.used != set(self.parameters):
            raise ValueError('placeholder declaration mismatch')
        return result


def compile_nodes(nodes):
    out = bytearray()
    for node in nodes:
        kind = node[0]
        if kind == 'literal':
            data = node[1].encode('utf-8')
            out.extend(struct.pack('<BH', 1, len(data))); out.extend(data)
        elif kind == 'argument':
            out.extend(bytes((2, node[1], node[2])))
        else:
            out.extend(bytes((4 if kind == 'select' else 3, node[1])))
            if kind != 'select':
                out.append(kind == 'selectordinal')
            out.append(len(node[2]))
            for selector, body in node[2]:
                data = compile_nodes(body)
                if kind == 'select':
                    key = selector.encode('ascii')
                    if len(key) > 255:
                        raise ValueError('select key too long')
                    out.append(len(key)); out.extend(key)
                elif selector.startswith('='):
                    value = int(selector[1:]); out.append(255)
                    out.extend(struct.pack('<BQ', value < 0, abs(value)))
                else:
                    out.append(CATEGORIES[selector])
                out.extend(struct.pack('<H', len(data))); out.extend(data)
        if len(out) > 65535:
            raise ValueError('compiled message exceeds 65535 bytes')
    return bytes(out)


def parse(text, parameters):
    parser = Parser(text, parameters)
    nodes = parser.parse()
    if not parameters:
        return ''.join(node[1] for node in nodes), parser.features
    return compile_nodes(nodes), parser.features

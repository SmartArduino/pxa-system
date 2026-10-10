"""Compile pinned Unicode CLDR rules into compact integer predicates (no runtime XML)."""
import functools
import pathlib
import re
import struct
import xml.etree.ElementTree as ET

CATEGORIES = {key: i for i, key in enumerate(('zero', 'one', 'two', 'few', 'many', 'other'))}
OPERANDS = {key: i for i, key in enumerate('nivwftec')}
DATA = pathlib.Path(__file__).with_name('cldr')


def compile_predicate(expression):
    expression = expression.split('@', 1)[0].strip()
    if not expression:
        return b''
    groups = re.split(r'\s+or\s+', expression)
    result = bytearray([len(groups)])
    for group in groups:
        relations = re.split(r'\s+and\s+', group)
        result.append(len(relations))
        for relation in relations:
            m = re.fullmatch(r'([nivwftec])(?:\s*%\s*(\d+))?\s*(!=|=)\s*([\d.,\s]+)', relation)
            if not m:
                raise ValueError(f'unsupported CLDR relation: {relation}')
            operand, modulus, op, values = m.groups()
            ranges = []
            for item in values.split(','):
                bounds = item.strip().split('..')
                low, high = int(bounds[0]), int(bounds[-1])
                if low > high:
                    raise ValueError('invalid CLDR range')
                ranges.append((low, high))
            result.extend(struct.pack('<BIBB', OPERANDS[operand], int(modulus or 0), op == '!=', len(ranges)))
            for low, high in ranges:
                result.extend(struct.pack('<II', low, high))
    return bytes(result)


@functools.lru_cache(maxsize=2)
def load_rules(kind):
    path = DATA / ('plurals.xml' if kind == 'cardinal' else 'ordinals.xml')
    result = {}
    for group in ET.parse(path).getroot().iter('pluralRules'):
        rules = [(rule.attrib['count'], compile_predicate(rule.text or ''))
                 for rule in group if rule.attrib['count'] != 'other']
        for locale in group.attrib['locales'].split():
            result[locale.replace('_', '-').lower()] = rules
    return result


def for_locale(locale, kind):
    rules = load_rules(kind)
    tag = locale.lower()
    parts = tag.split('-')
    regional = None
    if len(parts) > 2 and len(parts[1]) == 4 and (len(parts[2]) == 2 or parts[2].isdigit()):
        regional = parts[0] + '-' + parts[2]
    while tag:
        if tag in rules:
            return rules[tag]
        parent = tag.rpartition('-')[0]
        if '-' not in parent and regional and regional in rules:
            return rules[regional]
        tag = parent
    # CLDR root's rule is 'other'. Never invent an English rule for unknown languages.
    return rules.get('root', [])

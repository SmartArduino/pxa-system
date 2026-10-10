#!/usr/bin/env python3
"""Generator rejection tests plus execution of pinned CLDR samples in the C++ VM."""
import os
import pathlib
import re
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from compile_catalog import validate, emit, emit_cpp, cpp_bytes
from message_compiler import parse
from plural_rules import DATA, load_rules, for_locale

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'sdk/guest-cpp/tests/i18n'


class CatalogTests(unittest.TestCase):
    def test_rich_messages_and_errors(self):
        valid = [('{n}', {'n': 'i32'}), ("'{n}' {n}", {'n':'u32'}),
                 ('{n,plural, =01 {a} other {#}}', {'n':'u64'}),
                 ('{s,select, x {{n,plural,one {#} other {#}}} other {{n}}}', {'n':'i32','s':'string'})]
        for text, params in valid:
            self.assertIsInstance(parse(text, params)[0], bytes)
        invalid = [('{missing}', {}), ('{n}', {}), ('{n,plural, one {x}}', {'n':'u32'}),
                   ('{n,plural, one {x} one {y} other {z}}', {'n':'u32'}),
                   ('{n,plural, =01 {a} =1 {b} other {c}}', {'n':'u32'}),
                   ('{n,plural,offset:1 one {x} other {y}}', {'n':'i32'}),
                   ('{n,number}', {'n':'string'}), ('{n,select,other {x}}', {'n':'i32'}),
                   ('{n,plural,other {x}}', {'n':'string'}), ('{n,date}', {'n':'i32'}),
                   ('{n', {'n':'i32'}), ('hello}', {}), ('hello\0', {}),
                   ("'{unfinished}", {}), ('{n}', {'n':'float'}), ('{n}', {'n':'i32','x':'u32'})]
        for text, params in invalid:
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse(text, params)
        with self.assertRaises(ValueError):
            parse('{n,plural,other {' * 10 + '{n}' + '}}' * 10, {'n':'u32'})

    def test_validation_and_legacy(self):
        with tempfile.TemporaryDirectory() as directory:
            output=pathlib.Path(directory)/'messages.hpp'
            values=validate(FIXTURE/'messages.yaml', [p for p in sorted(FIXTURE.glob('*.yaml')) if p.name!='messages.yaml'])
            emit_cpp(output,*values)
            first=output.read_bytes(); modified=output.stat().st_mtime_ns; emit_cpp(output,*values)
            self.assertEqual(first,output.read_bytes())
            self.assertEqual(modified,output.stat().st_mtime_ns)
            self.assertNotIn(b'pxa_i18n.h',first)
            emit_cpp(output,*values,cpp_namespace="MyApp::I18n")
            self.assertIn(b"namespace MyApp::I18n",output.read_bytes())
            for namespace in ["bad-name::messages","bad.name","std::class","demo::::bad"]:
                with self.assertRaises(ValueError): emit_cpp(output,*values,cpp_namespace=namespace)
            with self.assertRaises(ValueError): emit(output,*values)
            source=pathlib.Path(directory)/'source.yaml'
            for content in [
                'namespace: demo\ndefault_locale: en\nmessages:\n  a-b:\n    source: "x"\n    context: "x"\n  a_b:\n    source: "y"\n    context: "y"\n',
                'namespace: demo\ndefault_locale: en\nmessages:\n  x:\n    source: "x"\n    context: "x"\n    max_bytes: true\n',
                'namespace: demo\nnamespace: duplicate\ndefault_locale: en\n',
                'namespace: demo\ndefault_locale: en\nnumber_symbols:\n  digits: "123"\n',
            ]:
                source.write_text(content)
                with self.assertRaises(ValueError): validate(source,[])
            source.write_text('namespace: demo\ndefault_locale: en\nmessages:\n  welcome:\n    source: "Hello {name}"\n    context: "Greeting"\n    placeholders:\n      name: string\n')
            legacy=pathlib.Path(directory)/'messages.h'; emit(legacy,*validate(source,[]))
            unit=pathlib.Path(directory)/'test.c'; unit.write_text('#include "messages.h"\nint main(void){return pxa_app_i18n_bundle.catalog_count != 1;}\n')
            executable=pathlib.Path(directory)/'legacy'
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-attributes','-I'+str(ROOT/'sdk/guest-c/include'),str(unit),'-o',str(executable)],check=True)
            subprocess.run([str(executable)],check=True)

    def test_regional_rule_fallback(self):
        self.assertEqual(for_locale("pt-Latn-PT","cardinal"),for_locale("pt-PT","cardinal"))
        self.assertNotEqual(for_locale("pt-PT","cardinal"),for_locale("pt","cardinal"))
        self.assertEqual(for_locale("zz","cardinal"),[])

    def test_all_cldr_samples(self):
        # Execute samples from every rule group, including decimal visible zeros.
        lines=['#include <pxa/i18n_plural.hpp>', '#include <cassert>', '#include <cstdio>', 'using namespace pxa::i18n;']
        samples=[]; rules_count=0; skipped=0
        for kind, filename in [('cardinal','plurals.xml'),('ordinal','ordinals.xml')]:
            for group in ET.parse(DATA/filename).getroot().iter('pluralRules'):
                locale=group.attrib['locales'].split()[0].replace('_','-')
                rules=for_locale(locale,kind)
                name=f'rules_{rules_count}'; rules_count+=1
                if rules:
                    lines.append(f'static constexpr PluralRule {name}[]={{')
                    for category, program in rules:
                        lines.append(f'{{PluralCategory::{category},{{{cpp_bytes(program)},{len(program)}}}}},')
                    lines.append('};')
                    ref=name
                else:
                    ref='{}'
                for rule in group:
                    category=rule.attrib['count']
                    for section in re.split(r'@(?:integer|decimal)',rule.text or '')[1:]:
                        for sample in section.strip().split(','):
                            sample=sample.strip()
                            if not sample or sample=='…': continue
                            bounds=sample.split('~')
                            if not all(re.fullmatch(r'\d+(?:\.\d+)?',b) for b in bounds):
                                skipped+=1; continue # compact/scientific notation not a supported value kind
                            def value(text):
                                integer,sep,fraction=text.partition('.')
                                return int(integer+fraction),len(fraction)
                            low,scale=value(bounds[0]); high,other_scale=value(bounds[-1])
                            self.assertEqual(scale,other_scale)
                            points=range(low,high+1) if high-low <= 1000 else [low,low+1,high]
                            for coefficient in points:
                                if coefficient>2**63-1 or scale>18: skipped+=1; continue
                                samples.append(f'assert(plural_category({ref},Decimal{{{coefficient}LL,{scale}}})==PluralCategory::{category});')
        lines.append('int main(){')
        lines.extend(samples)
        lines.append(f'std::printf("CLDR 48: {len(samples)} cardinal/ordinal samples, {rules_count} groups; {skipped} compact/out-of-domain samples excluded\\n");}}')
        with tempfile.TemporaryDirectory() as directory:
            unit=pathlib.Path(directory)/'cldr.cpp'; unit.write_text('\n'.join(lines))
            executable=pathlib.Path(directory)/'cldr'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++2c','-O1','-Wno-attributes','-I'+str(ROOT/'sdk/guest-cpp/include'),str(unit),'-o',str(executable)],check=True)
            subprocess.run([str(executable)],check=True)
        self.assertNotEqual(for_locale('pt-PT','cardinal'),for_locale('pt','cardinal'))
        self.assertEqual(for_locale('unknown','cardinal'),[])


if __name__=='__main__':
    unittest.main()

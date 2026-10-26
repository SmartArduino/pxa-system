import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('gate',Path(__file__).with_name('check_system_licenses.py'))
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
class PolicyTest(unittest.TestCase):
    def test_expressions(self):
        for expression in ('MIT','Apache-2.0 OR GPL-2.0-or-later','(MIT OR MPL-2.0) AND BSD-3-Clause','Apache-2.0 WITH LLVM-exception'):
            self.assertTrue(gate.allowed_expression(expression),expression)
        for expression in ('MIT AND GPL-2.0-only','AGPL-3.0-only','LGPL-2.1-or-later','MPL-2.0','MIT OR','MIT;GPL','(MIT','Unknown','GPL-3.0-only WITH GCC-exception-3.1','MIT WITH Fictional-exception','GPL-3.0-only WITH LLVM-exception'):
            self.assertFalse(gate.allowed_expression(expression),expression)
        self.assertTrue(gate.allowed_expression('GPL-3.0-only WITH GCC-exception-3.1',True))
        self.assertFalse(gate.allowed_expression('GPL-2.0-only WITH GCC-exception-3.1',True))
    def test_nested_and_bare_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'test.c'
            for text in ('/* SPDX-License-Identifier: MIT */\n/* Mozilla Public License v2 */',
                         '/* GNU General Public License */',
                         '/* This Source Code Form is subject to the terms of the Mozilla Public\n * License, v. 2.0. If a copy of the MPL was not distributed */',
                         '/* SPDX-License-Identifier: GPL-3.0-only WITH GCC-exception-3.1 */'):
                path.write_text(text);self.assertIsNotNone(gate.inspect(path,{},[]))
            path.write_text('/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */\nint x;')
            self.assertIsNone(gate.inspect(path,{},[]))
    def test_review_requires_unchanged_hash(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'dual.c';path.write_text('/* MIT OR GPL; reviewed alternative */')
            review={hashlib.sha256(path.read_bytes()).hexdigest():{'selected':'MIT'}}
            self.assertIsNone(gate.inspect(path,review,[]))
            path.write_text(path.read_text()+'\n/* changed upstream */')
            self.assertIsNotNone(gate.inspect(path,review,[]))
if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Validate IPC contract generator inputs and stable generated output."""

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path


SDK = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SDK / "tools"))
import generate_ipc_contract as generator  # noqa: E402


class ContractGenerationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        source = SDK / "examples/ipc-stats/stats.contract.json"
        cls.document = json.loads(source.read_text(encoding="utf-8"))

    def test_checked_fixture(self) -> None:
        expected = (SDK / "examples/ipc-stats/stats_contract.hpp").read_text(
            encoding="utf-8")
        self.assertEqual(generator.render(self.document), expected)

    def test_bad_endpoint_and_identifier(self) -> None:
        for key, value in (("endpoint", "other;bad"), ("namespace", "class"),
                           ("name", "_reserved")):
            with self.subTest(key=key):
                document = copy.deepcopy(self.document)
                document[key] = value
                with self.assertRaises(ValueError):
                    generator.render(document)
        with self.assertRaises(ValueError):
            generator.render([])

    def test_bad_fields_and_budget(self) -> None:
        variants = []
        duplicate = copy.deepcopy(self.document)
        duplicate["response"][1]["id"] = 1
        variants.append(duplicate)
        reserved = copy.deepcopy(self.document)
        reserved["response"][1]["name"] = "class"
        variants.append(reserved)
        unbounded = copy.deepcopy(self.document)
        unbounded["response"][2]["max_bytes"] = 1025
        variants.append(unbounded)
        oversized = copy.deepcopy(self.document)
        oversized["response"] = [
            {"id": index, "name": f"field{index}", "type": "text",
             "max_bytes": 512} for index in range(1, 4)
        ]
        variants.append(oversized)
        invalid_type = copy.deepcopy(self.document)
        invalid_type["request"][0]["type"] = []
        variants.append(invalid_type)
        for document in variants:
            with self.subTest(document=document):
                with self.assertRaises(ValueError):
                    generator.render(document)

    def test_i32_codec_is_generated(self) -> None:
        document = copy.deepcopy(self.document)
        document["response"].insert(1, {"id": 2, "name": "delta", "type": "i32"})
        document["response"][2]["id"] = 3
        document["response"][3]["id"] = 4
        output = generator.render(document)
        self.assertIn("std::int32_t delta{};", output)
        self.assertIn("static_cast<std::int32_t>(pxa::wire::get32", output)


if __name__ == "__main__":
    unittest.main()

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
        required_after_optional = copy.deepcopy(self.document)
        required_after_optional["response"].append(
            {"id": 6, "name": "late", "type": "u32"})
        variants.append(required_after_optional)
        bad_optional = copy.deepcopy(self.document)
        bad_optional["response"][-1]["optional"] = "yes"
        variants.append(bad_optional)
        for document in variants:
            with self.subTest(document=document):
                with self.assertRaises(ValueError):
                    generator.render(document)

    def test_i32_codec_is_generated(self) -> None:
        document = copy.deepcopy(self.document)
        document["response"].insert(1, {"id": 2, "name": "delta", "type": "i32"})
        document["response"][2]["id"] = 3
        document["response"][3]["id"] = 4
        document["response"][4]["id"] = 5
        document["response"][5]["id"] = 6
        output = generator.render(document)
        self.assertIn("std::int32_t delta{};", output)
        self.assertIn("static_cast<std::int32_t>(pxa::wire::get32", output)

    def test_optional_field_is_bounded_and_omittable(self) -> None:
        output = generator.render(self.document)
        self.assertIn("std::optional<bool> cached{};", output)
        self.assertIn("std::optional<pxa::FixedText<12>> note{};", output)
        self.assertIn("if (value.cached)", output)
        self.assertIn("pxa::wire::record(writer, 32772, bytes)", output)
        self.assertIn("(*value.note).view()", output)
        self.assertIn("records.take_optional(4)", output)
        self.assertIn("records.finish(5)", output)


if __name__ == "__main__":
    unittest.main()

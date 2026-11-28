# SPDX-License-Identifier: MIT
import unittest

from linear_memory import LinearMemory, maximum_for_targets, memory_for_targets, targets_for_artifacts


class LinearMemoryTests(unittest.TestCase):
    def test_common_module_fits_every_target(self):
        settings = {"maximum_bytes": 3 << 20, "pinned": True,
                    "maximum_bytes_by_target": {"esp32p4": 4 << 20}}
        self.assertEqual(maximum_for_targets(settings, ["esp32p4"]), 4 << 20)
        self.assertEqual(maximum_for_targets(settings, ["esp32s31"]), 3 << 20)
        for targets in (["esp32p4", "esp32s31"], ["wasm", "esp32p4"]):
            self.assertEqual(maximum_for_targets(settings, targets), 3 << 20)
        self.assertEqual(maximum_for_targets(None, ["esp32p4"]), 0)

    def test_validates_even_unselected_limits(self):
        for mapping in ({"esp32p4": 1}, {"simulator": True}, {"bad-target": 65536}, 0):
            with self.subTest(mapping=mapping), self.assertRaises(ValueError):
                maximum_for_targets({"maximum_bytes": 65536, "pinned": False,
                                     "maximum_bytes_by_target": mapping}, ["esp32s31"])

    def test_initial_size_and_signed_pin_follow_the_shared_module(self):
        page = 65536
        settings = {"maximum_bytes": 48 * page, "pinned": True,
                    "maximum_bytes_by_target": {"esp32s31": 36 * page},
                    "initial_bytes_by_target": {"esp32s31": 22 * page},
                    "pinned_by_target": {"esp32s31": False}}
        self.assertEqual(memory_for_targets(settings, ["esp32s31"]),
                         LinearMemory(22 * page, 36 * page, False))
        self.assertEqual(memory_for_targets(settings, ["esp32p4"]),
                         LinearMemory(0, 48 * page, True))
        self.assertEqual(memory_for_targets(settings, ["wasm"]),
                         LinearMemory(0, 48 * page, True))
        for selected in (["esp32p4", "esp32s31"], ["esp32s31", "wasm"]):
            self.assertEqual(memory_for_targets(settings, selected),
                             LinearMemory(22 * page, 36 * page, True))
        names = targets_for_artifacts(["esp32-s31", "linux-x86_64"])
        self.assertEqual(names, ["esp32s31", "simulator"])

    def test_invalid_policy_is_rejected_before_compiling(self):
        page = 65536
        baseline = {"maximum_bytes": 8 * page, "pinned": False}
        for overrides in ({"pinned_by_target": {"esp32p4": 1}},
                          {"initial_bytes": 1}, {"initial_bytes": 9 * page},
                          {"initial_bytes_by_target": {"esp32s31": 9 * page}},
                          {"initial_bytes_by_target": {"unknown": page}}):
            with self.subTest(overrides=overrides), self.assertRaises(ValueError):
                memory_for_targets(baseline | overrides, ["wasm"])
        split = baseline | {"maximum_bytes_by_target": {"esp32s31": 3 * page},
                            "initial_bytes_by_target": {"esp32p4": 4 * page}}
        with self.assertRaisesRegex(ValueError, "incompatible"):
            memory_for_targets(split, ["esp32p4", "esp32s31"])

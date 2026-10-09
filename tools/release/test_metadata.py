#!/usr/bin/env python3
"""Exercise release identity and protocol compatibility as separate contracts."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

import metadata


class MetadataTest(unittest.TestCase):
    def test_semver_acceptance(self):
        for value in ("0.2.0", "0.2.0-rc.1", "1.2.3-dev.5+gabc", "1.0.0-0"):
            self.assertIsNotNone(metadata.SEMVER.fullmatch(value), value)
        for value in ("01.2.0", "1.2", "1.2.3-01", "1.2.3+", "1.2.3-", "1.2.3\n"):
            self.assertIsNone(metadata.SEMVER.fullmatch(value), value)

    def test_core_is_guest_v1(self):
        value = metadata.profile()
        self.assertEqual(value["core"], [1, 0])
        self.assertEqual(value["envelope_bytes"], 20)
        # Legacy mailbox directory's core 0.1 is deliberately excluded.
        self.assertNotIn("core", value["services"])
        self.assertEqual(value["services"]["assets"]["version"], [2, 1])

    def test_checked_in_versions(self):
        metadata.check()

    def test_stale_idf_version_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "VERSION").write_text(metadata.release())
            for name in ("config", "spec", "libpxa", "sdk"):
                # Read-only symlinks avoid copying the SDK. No generated writes.
                (root / name).symlink_to(metadata.ROOT / name, target_is_directory=True)
            (root / "idf_component.yml").write_text('version: "99.0.0"\n')
            with self.assertRaisesRegex(ValueError, "idf_component"):
                metadata.check(root)


if __name__ == "__main__":
    unittest.main()

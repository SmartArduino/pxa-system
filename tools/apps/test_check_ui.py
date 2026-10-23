#!/usr/bin/env python3
"""Metadata-only localization must not imply an engine-specific entry file."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class CatalogBoundaryTest(unittest.TestCase):
    def check_catalog(self, messages, source=None, app_id="pxa-fixture", shared=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "fixture"
            (app / "i18n").mkdir(parents=True)
            (app / "package.json").write_text(json.dumps({"id": app_id}))
            (app / "i18n/messages.yaml").write_text(messages)
            (app / "i18n/zh-CN.yaml").write_text("locale: zh-CN\nmetadata:\n  name: 示例\n")
            if source is not None:
                directory = root / "common/frontend" if shared else app
                directory.mkdir(parents=True, exist_ok=True)
                (directory / "main.cpp").write_text(source)
                if shared:
                    (directory / "Frontend.cmake").write_text("# shared frontend\n")
                    (app / "CMakeLists.txt").write_text('include("${CMAKE_CURRENT_SOURCE_DIR}/../common/frontend/Frontend.cmake")\n')
            return subprocess.run([sys.executable, str(Path(__file__).with_name("check_ui.py")), str(root)],
                                  capture_output=True, text=True)

    def test_metadata_only_without_main(self):
        self.assertEqual(self.check_catalog("namespace: app.fixture\ndefault_locale: en-US\n").returncode, 0)

    def test_runtime_catalog_requires_entry(self):
        result = self.check_catalog("messages:\n  title: {source: Title}\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing application entry source", result.stderr)

    def test_runtime_catalog_requires_lifecycle(self):
        result = self.check_catalog("messages:\n  title: {source: Title}\n", "int main() {}")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing locale lifecycle", result.stderr)

    def test_runtime_catalog_with_lifecycle(self):
        result = self.check_catalog("messages:\n  title: {source: Title}\n",
                                    "pxa_i18n_init_from_start_config(); pxa_i18n_handle_event();")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_message_list_fails(self):
        self.assertNotEqual(self.check_catalog("messages: []\n").returncode, 0)

    def test_shared_frontend_still_checks_lifecycle(self):
        self.assertEqual(self.check_catalog("messages: {title: {source: Title}}\n",
            "pxa_i18n_init_from_start_config(); pxa_i18n_handle_event();", shared=True).returncode, 0)
        self.assertNotEqual(self.check_catalog("messages: {title: {source: Title}}\n",
            "int main() {}", shared=True).returncode, 0)

    def test_canonical_identity_independent_of_checkout_directory(self):
        self.assertEqual(self.check_catalog("messages: {}\n", app_id="pxa-game-boy").returncode, 0)
        self.assertNotEqual(self.check_catalog("messages: {}\n", app_id="../bad").returncode, 0)


if __name__ == "__main__":
    unittest.main()

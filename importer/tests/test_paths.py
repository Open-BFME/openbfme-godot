from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from openbfme_importer.paths import (
    default_godot_content_root,
    default_state_root,
    ensure_external_to_repo,
    repo_root_from_module,
)


class PrivateRetailPathTests(unittest.TestCase):
    def test_defaults_stay_under_ignored_workspace(self) -> None:
        with patch.dict(
            os.environ,
            {"OPENBFME_IMPORT_ROOT": "", "OPENBFME_CONTENT_ROOT": ""},
            clear=False,
        ):
            private_root = (repo_root_from_module() / "workspace").resolve()
            self.assertTrue(default_state_root().is_relative_to(private_root))
            self.assertTrue(default_godot_content_root().is_relative_to(private_root))

    def test_only_private_subtree_is_allowed_inside_repository(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            repository = Path(raw).resolve()
            allowed = repository / "workspace" / "retail-work"
            self.assertEqual(ensure_external_to_repo(allowed, repository), allowed)
            with self.assertRaisesRegex(ValueError, "ignored workspace"):
                ensure_external_to_repo(repository / "game" / "retail", repository)

    def test_external_workspace_remains_supported(self) -> None:
        with tempfile.TemporaryDirectory() as repo_raw, tempfile.TemporaryDirectory() as state_raw:
            repository = Path(repo_raw).resolve()
            state = Path(state_raw).resolve()
            self.assertEqual(ensure_external_to_repo(state, repository), state)


if __name__ == "__main__":
    unittest.main()

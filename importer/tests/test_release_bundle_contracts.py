from __future__ import annotations

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.pipeline import ImportPipeline, _importer_recipe_report


class ReleaseBundleContractTests(unittest.TestCase):
    def test_custom_state_ffmpeg_precedes_machine_path(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            state = root / "state"
            pinned = state / "tools" / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe"
            pinned.parent.mkdir(parents=True)
            pinned.write_bytes(b"pinned")
            machine = root / "machine-ffmpeg.exe"
            machine.write_bytes(b"machine")
            pipeline = ImportPipeline(InstallCatalog(install, (), ()), state)

            with (
                mock.patch.dict(os.environ, {}, clear=False),
                mock.patch(
                    "openbfme_importer.pipeline.discover_executable",
                    return_value=machine,
                ) as discover,
            ):
                os.environ.pop("OPENBFME_FFMPEG", None)
                self.assertEqual(pipeline._ffmpeg_executable(), pinned.resolve())

            discover.assert_not_called()

    def test_bundled_recipe_uses_validated_release_identity(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "importer" / "openbfme_importer").mkdir(parents=True)
            (root / "importer" / "openbfme_importer" / "entry.py").write_text(
                "# fixture\n", encoding="utf-8"
            )
            (root / "release-identity.json").write_text(
                json.dumps(
                    {
                        "schema": "openbfme.bundled-source-identity",
                        "schemaVersion": 1,
                        "commit": "a" * 40,
                        "sourceClean": True,
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module",
                return_value=root,
            ):
                report = _importer_recipe_report()

            self.assertEqual(report["git_commit"], "a" * 40)
            self.assertTrue(report["git_worktree_clean"])

    def test_invalid_bundled_release_identity_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "importer" / "openbfme_importer").mkdir(parents=True)
            (root / "importer" / "openbfme_importer" / "entry.py").write_text(
                "# fixture\n", encoding="utf-8"
            )
            (root / "release-identity.json").write_text(
                json.dumps(
                    {
                        "schema": "openbfme.bundled-source-identity",
                        "schemaVersion": 1,
                        "commit": "a" * 40,
                        "sourceClean": False,
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module",
                return_value=root,
            ):
                with self.assertRaisesRegex(RuntimeError, "release identity"):
                    _importer_recipe_report()


if __name__ == "__main__":
    unittest.main()

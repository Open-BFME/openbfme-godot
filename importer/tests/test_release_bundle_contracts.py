from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

from openbfme_importer.bootstrap import FFMPEG_EXE_SHA256
from openbfme_importer.native_corpus import (
    NativeCorpusDependencyError,
    PINNED_FFMPEG_VERSION,
    _resolve_ffmpeg,
)
from openbfme_importer.pipeline import _importer_recipe_report
from openbfme_importer.tools import discover_executable


class ReleaseBundleContractTests(unittest.TestCase):
    def test_custom_state_ffmpeg_precedes_machine_path(self) -> None:
        """The import state's pinned FFmpeg wins over anything on PATH.

        Tool discovery moved out of ``ImportPipeline`` into
        :func:`openbfme_importer.tools.discover_executable`, which derives the
        pinned location from the configured state root. Precedence is asserted
        here; :meth:`test_machine_ffmpeg_is_refused_by_pinned_hash` asserts the
        stronger guarantee that took over when precedence is not enough.
        """

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            state = root / "state"
            pinned = state / "tools" / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe"
            pinned.parent.mkdir(parents=True)
            pinned.write_bytes(b"pinned")
            machine = root / "machine-ffmpeg.exe"
            machine.write_bytes(b"machine")

            with (
                mock.patch.dict(
                    os.environ, {"OPENBFME_IMPORT_ROOT": str(state)}, clear=False
                ),
                mock.patch("shutil.which", return_value=str(machine)) as which,
            ):
                os.environ.pop("OPENBFME_FFMPEG", None)
                self.assertEqual(
                    discover_executable("ffmpeg", "OPENBFME_FFMPEG"),
                    pinned.resolve(),
                )
                # Control: with no pinned ffprobe beside it, PATH *is* reached,
                # so the assertion below is about precedence, not a dead mock.
                os.environ.pop("OPENBFME_FFPROBE", None)
                self.assertEqual(
                    discover_executable("ffprobe", "OPENBFME_FFPROBE"),
                    machine.resolve(),
                )

            # PATH was never consulted while the state root held a pinned build.
            self.assertEqual(
                [call.args[0] for call in which.call_args_list], ["ffprobe"]
            )

    def test_machine_ffmpeg_is_refused_by_pinned_hash(self) -> None:
        """A machine FFmpeg standing in for the pinned one fails loudly.

        This is the property the old ``ImportPipeline._ffmpeg_executable``
        precedence test was really guarding, and it is now enforced by content
        rather than by lookup order: whichever binary discovery hands back is
        hashed against ``FFMPEG_EXE_SHA256`` and rejected on a mismatch. That
        closes the recorded incident where a state root that never reached tool
        discovery let a PATH FFmpeg through.
        """

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            machine = root / "machine-ffmpeg.exe"
            machine.write_bytes(b"machine ffmpeg, not the pinned build")
            self.assertNotEqual(
                hashlib.sha256(machine.read_bytes()).hexdigest(),
                FFMPEG_EXE_SHA256,
            )
            expected = re.escape(
                f"does not match the pinned {PINNED_FFMPEG_VERSION} hash"
            )

            # Handed the wrong binary explicitly.
            with self.assertRaisesRegex(NativeCorpusDependencyError, expected):
                _resolve_ffmpeg(machine)

            # Handed the wrong binary by discovery - the incident shape.
            with mock.patch.dict(
                os.environ, {"OPENBFME_FFMPEG": str(machine)}, clear=False
            ):
                with self.assertRaisesRegex(NativeCorpusDependencyError, expected):
                    _resolve_ffmpeg(None)

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

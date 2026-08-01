from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer import cli
from openbfme_importer.pipeline import ImportPipeline, update_selection_entry
from openbfme_importer.util import write_json_atomic
from tests.test_integrity import make_audited_pack


HEX_A = "a" * 64
HEX_B_OLD = "b" * 64
HEX_B_NEW = "c" * 64
HEX_C = "d" * 64


def _write_selection(content_root: Path, document: dict) -> bytes:
    write_json_atomic(content_root / "selection.json", document)
    return (content_root / "selection.json").read_bytes()


def _make_published_bundle(content_root: Path, pack_id: str, digest: str) -> None:
    bundle = content_root / pack_id / digest
    write_json_atomic(bundle / "pack.json", {"id": pack_id, "version": "1"})


class PublishWithoutSelectTests(unittest.TestCase):
    def test_publish_leaves_existing_selection_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            make_audited_pack(source)
            content = root / "content"
            before = _write_selection(
                content,
                {
                    "schema": "openbfme.pack-selection",
                    "schemaVersion": 0,
                    "activePack": f"other-pack/{HEX_A}",
                    "supplementalPacks": [f"pack-c/{HEX_C}"],
                    "operatorNote": "hand-tuned playtest stack",
                },
            )
            pipeline = ImportPipeline(None, root / "state")
            publication = pipeline.publish_to_godot(source, content, select=False)
            self.assertEqual(
                (content / "selection.json").read_bytes(), before
            )
            self.assertEqual(publication["pack_id"], "test-generic-pack")
            self.assertEqual(
                publication["pack_relative"],
                f"test-generic-pack/{publication['bundle_sha256']}",
            )
            self.assertTrue(Path(publication["published_pack"]).is_dir())
            self.assertNotIn("selection", publication)
            self.assertNotIn("active_pack", publication)

    def test_publish_without_select_never_creates_selection(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            make_audited_pack(source)
            content = root / "content"
            pipeline = ImportPipeline(None, root / "state")
            pipeline.publish_to_godot(source, content, select=False)
            self.assertFalse((content / "selection.json").exists())

    def test_publish_with_select_keeps_legacy_activation(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            make_audited_pack(source)
            content = root / "content"
            pipeline = ImportPipeline(None, root / "state")
            publication = pipeline.publish_to_godot(source, content, select=True)
            selection = json.loads(
                (content / "selection.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                selection["activePack"], publication["pack_relative"]
            )
            self.assertEqual(publication["active_pack"], publication["pack_relative"])

    def test_publish_faction_cli_defaults_to_no_select(self) -> None:
        args = cli.build_parser().parse_args(
            [
                "publish-faction-to-slice",
                "--install",
                "C:/BFME2",
                "--faction",
                "men",
            ]
        )
        self.assertFalse(args.select)


class UpdateSelectionEntryTests(unittest.TestCase):
    def _seed(self, content: Path) -> bytes:
        _make_published_bundle(content, "pack-b", HEX_B_NEW)
        return _write_selection(
            content,
            {
                "schema": "openbfme.pack-selection",
                "schemaVersion": 0,
                "activePack": f"pack-a/{HEX_A}",
                "supplementalPacks": [
                    f"pack-b/{HEX_B_OLD}",
                    f"pack-c/{HEX_C}",
                ],
                "operatorNote": "hand-tuned playtest stack",
            },
        )

    def test_updates_exactly_one_supplement_preserving_active_and_order(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            result = update_selection_entry(content, "pack-b", HEX_B_NEW)
            self.assertTrue(result["changed"])
            self.assertEqual(len(result["updated"]), 1)
            self.assertEqual(result["updated"][0]["role"], "supplementalPacks[0]")
            self.assertEqual(result["updated"][0]["previous"], f"pack-b/{HEX_B_OLD}")
            selection = json.loads(
                (content / "selection.json").read_text(encoding="utf-8")
            )
            self.assertEqual(selection["activePack"], f"pack-a/{HEX_A}")
            self.assertEqual(
                selection["supplementalPacks"],
                [f"pack-b/{HEX_B_NEW}", f"pack-c/{HEX_C}"],
            )
            self.assertEqual(selection["operatorNote"], "hand-tuned playtest stack")
            # Atomic rewrite leaves no temp litter behind.
            self.assertEqual(
                [item.name for item in content.iterdir() if item.is_file()],
                ["selection.json"],
            )

    def test_updates_active_pack_entry(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            _make_published_bundle(content, "pack-a", HEX_B_NEW)
            _write_selection(
                content,
                {
                    "schema": "openbfme.pack-selection",
                    "schemaVersion": 0,
                    "activePack": f"pack-a/{HEX_A}",
                },
            )
            result = update_selection_entry(content, "pack-a", HEX_B_NEW)
            self.assertEqual(len(result["updated"]), 1)
            self.assertEqual(result["updated"][0]["role"], "activePack")
            selection = json.loads(
                (content / "selection.json").read_text(encoding="utf-8")
            )
            self.assertEqual(selection["activePack"], f"pack-a/{HEX_B_NEW}")

    def test_idempotent_when_entry_already_current(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            update_selection_entry(content, "pack-b", HEX_B_NEW)
            before = (content / "selection.json").read_bytes()
            result = update_selection_entry(content, "pack-b", HEX_B_NEW)
            self.assertFalse(result["changed"])
            self.assertEqual((content / "selection.json").read_bytes(), before)

    def test_rejects_pack_id_with_no_selection_entry(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            _make_published_bundle(content, "pack-z", HEX_B_NEW)
            with self.assertRaises(ValueError):
                update_selection_entry(content, "pack-z", HEX_B_NEW)

    def test_rejects_missing_target_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            with self.assertRaises(FileNotFoundError):
                update_selection_entry(content, "pack-b", "e" * 64)

    def test_rejects_bundle_with_mismatched_pack_id(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            bundle = content / "pack-b" / ("f" * 64)
            write_json_atomic(bundle / "pack.json", {"id": "impostor"})
            with self.assertRaises(ValueError):
                update_selection_entry(content, "pack-b", "f" * 64)

    def test_rejects_malformed_digest_and_pack_id(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            with self.assertRaises(ValueError):
                update_selection_entry(content, "pack-b", "not-a-digest")
            with self.assertRaises(ValueError):
                update_selection_entry(content, "Pack/../b", HEX_B_NEW)

    def test_cli_update_selection_entry_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = cli.main(
                    [
                        "--json",
                        "update-selection-entry",
                        "--pack-id",
                        "pack-b",
                        "--bundle-sha256",
                        HEX_B_NEW,
                        "--godot-content-root",
                        str(content),
                    ]
                )
            self.assertEqual(result, 0)
            payload = json.loads(stdout.getvalue())
            self.assertTrue(payload["changed"])
            selection = json.loads(
                (content / "selection.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                selection["supplementalPacks"][0], f"pack-b/{HEX_B_NEW}"
            )

    def test_cli_reports_error_for_unreferenced_pack(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw) / "content"
            self._seed(content)
            _make_published_bundle(content, "pack-z", HEX_B_NEW)
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                result = cli.main(
                    [
                        "update-selection-entry",
                        "--pack-id",
                        "pack-z",
                        "--bundle-sha256",
                        HEX_B_NEW,
                        "--godot-content-root",
                        str(content),
                    ]
                )
            self.assertEqual(result, 1)
            self.assertIn("no selection entry", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()

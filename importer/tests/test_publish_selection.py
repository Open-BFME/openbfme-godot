from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
import unittest.mock

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

    def test_publish_with_select_refuses_to_replace_different_active_pack(self) -> None:
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
                    "supplementalPacks": [f"test-generic-pack/{HEX_B_OLD}"],
                },
            )
            pipeline = ImportPipeline(None, root / "state")
            with self.assertRaisesRegex(RuntimeError, "different activePack"):
                pipeline.publish_to_godot(source, content, select=True)
            self.assertEqual((content / "selection.json").read_bytes(), before)

    def test_publish_with_select_refuses_corrupt_selection_without_rewriting(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            make_audited_pack(source)
            content = root / "content"
            content.mkdir(parents=True)
            selection_path = content / "selection.json"
            before = b'{"schema":"openbfme.pack-selection","activePack":'
            selection_path.write_bytes(before)
            pipeline = ImportPipeline(None, root / "state")
            with self.assertRaisesRegex(RuntimeError, "malformed selection.json"):
                pipeline.publish_to_godot(source, content, select=True)
            self.assertEqual(selection_path.read_bytes(), before)

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


class LegacyPublishSelectionWriterTests(unittest.TestCase):
    """The publish path is a selection WRITER, so it obeys the same guarantees.

    It used to rewrite the workspace selection.json on its own: no lock, no
    durable mirror, and a freshly composed document that silently dropped every
    key it did not know about (operatorNote, strictParityProfile, anything a
    later profile adds). A selection written that way is indistinguishable from
    a deliberate one, which is the whole failure class this packet exists for.
    """

    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.base = Path(self._temp.name)
        self.source = self.base / "source"
        make_audited_pack(self.source)
        self.content = self.base / "content"
        self.pipeline = ImportPipeline(None, self.base / "state")
        # Hermetic durable location. Without this the tests would probe - and
        # lock - the machine owner's real Godot user cache.
        self.durable = self.base / "durable"
        patcher = unittest.mock.patch.dict(
            os.environ, {"OPENBFME_DURABLE_CONTENT": str(self.durable)}
        )
        patcher.start()
        self.addCleanup(patcher.stop)

    def _write_durable_selection(self, document: dict) -> None:
        self.durable.mkdir(parents=True, exist_ok=True)
        write_json_atomic(self.durable / "selection.json", document)

    def _selection(self) -> dict:
        return json.loads((self.content / "selection.json").read_text(encoding="utf-8"))

    def test_publish_with_select_preserves_unknown_and_operator_metadata(self) -> None:
        first = self.pipeline.publish_to_godot(self.source, self.content, select=True)
        document = self._selection()
        document["operatorNote"] = "hand-tuned playtest stack"
        document["strictParityProfile"] = True
        document["someFutureKey"] = {"kept": [1, 2, 3]}
        write_json_atomic(self.content / "selection.json", document)
        # Re-publishing the same bundle re-activates the same entry; every key
        # the operator (or a later profile) put there must survive.
        second = self.pipeline.publish_to_godot(self.source, self.content, select=True)
        self.assertEqual(second["pack_relative"], first["pack_relative"])
        after = self._selection()
        self.assertEqual(after["activePack"], first["pack_relative"])
        self.assertEqual(after["operatorNote"], "hand-tuned playtest stack")
        self.assertTrue(after["strictParityProfile"])
        self.assertEqual(after["someFutureKey"], {"kept": [1, 2, 3]})

    def test_publish_with_select_activates_despite_a_durable_selection(self) -> None:
        # Q86: the durable install cache is an independent document. A
        # workspace activation proceeds regardless of what any durable
        # selection says - the game loader fails closed on a broken workspace
        # instead of substituting durable bytes, so nothing can desync.
        publication = self.pipeline.publish_to_godot(
            self.source, self.content, select=True
        )
        self._write_durable_selection(
            {
                "schema": "openbfme.pack-selection",
                "schemaVersion": 0,
                "activePack": publication["pack_relative"],
            }
        )
        second = self.pipeline.publish_to_godot(self.source, self.content, select=True)
        self.assertEqual(second["pack_relative"], publication["pack_relative"])
        after = self._selection()
        self.assertEqual(after["activePack"], publication["pack_relative"])


    def test_publish_without_select_does_not_lock_the_durable_root(self) -> None:
        from openbfme_importer.pipeline import selection_transaction_lock

        self.durable.mkdir(parents=True, exist_ok=True)
        # Nothing about a non-activating publish concerns the durable mirror, so
        # it must not block on someone else's durable lock.
        with selection_transaction_lock(self.durable):
            publication = self.pipeline.publish_to_godot(
                self.source, self.content, select=False
            )
        self.assertTrue(Path(publication["published_pack"]).is_dir())

    def test_publish_with_select_leaves_no_staging_litter(self) -> None:
        self.pipeline.publish_to_godot(self.source, self.content, select=True)
        self.assertEqual(
            sorted(item.name for item in self.content.iterdir() if item.is_file()),
            ["selection.json"],
        )


class UpdateSelectionEntryLockTests(unittest.TestCase):
    """`update-selection-entry` is the other public selection writer (RULE P2).

    It stays role-preserving - that is exactly why it exists - but it may not
    read-modify-write a document another transaction is mid-swap on.
    """

    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.content = Path(self._temp.name) / "content"
        _make_published_bundle(self.content, "pack-b", HEX_B_NEW)
        _write_selection(
            self.content,
            {
                "schema": "openbfme.pack-selection",
                "schemaVersion": 0,
                "activePack": f"pack-a/{HEX_A}",
                "supplementalPacks": [f"pack-b/{HEX_B_OLD}"],
                "operatorNote": "hand-tuned playtest stack",
                "strictParityProfile": True,
                "someFutureKey": {"kept": True},
            },
        )

    def test_refuses_while_the_content_root_lock_is_held(self) -> None:
        from openbfme_importer.pipeline import (
            SelectionTransactionError,
            selection_transaction_lock,
        )

        before = (self.content / "selection.json").read_bytes()
        with selection_transaction_lock(self.content):
            with self.assertRaises(SelectionTransactionError) as caught:
                update_selection_entry(self.content, "pack-b", HEX_B_NEW)
        self.assertIn("lock", str(caught.exception).lower())
        self.assertEqual((self.content / "selection.json").read_bytes(), before)
        # The lock is the only obstacle: once released the update goes through.
        self.assertTrue(update_selection_entry(self.content, "pack-b", HEX_B_NEW)["changed"])

    def test_preserves_every_unknown_key_and_the_entry_role(self) -> None:
        update_selection_entry(self.content, "pack-b", HEX_B_NEW)
        document = json.loads(
            (self.content / "selection.json").read_text(encoding="utf-8")
        )
        self.assertEqual(document["activePack"], f"pack-a/{HEX_A}")
        self.assertEqual(document["supplementalPacks"], [f"pack-b/{HEX_B_NEW}"])
        self.assertEqual(document["operatorNote"], "hand-tuned playtest stack")
        self.assertTrue(document["strictParityProfile"])
        self.assertEqual(document["someFutureKey"], {"kept": True})

    def test_releases_the_lock_after_a_refusal(self) -> None:
        with self.assertRaises(FileNotFoundError):
            update_selection_entry(self.content, "pack-z", HEX_B_NEW)
        self.assertEqual(list(self.content.glob("*.lock")), [])
        self.assertTrue(update_selection_entry(self.content, "pack-b", HEX_B_NEW)["changed"])


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


class SelectionTransactionTests(unittest.TestCase):
    """Staged, atomic multi-pack selection transaction (workspace + durable).

    RULE P4: this class was written BEFORE the implementation it describes.

    Why it exists: the seven-entry faction materializer rewrote selection.json
    one entry at a time, so a failure part-way through left a HYBRID selection
    -- some entries pointing at the new bundles, some at the old ones, and the
    durable mirror at a third state. Nothing distinguishes such a tree from a
    deliberate selection, which is how an 80-row lobby survived review.

    Contract under test:
      * every named pack is validated (safe relative path, content-addressed,
        present, pack.json id matches) in EVERY target root before any write;
      * the complete document is staged for the workspace AND the durable
        mirror, then committed with exactly ONE os.replace per TARGET, never
        one per entry;
      * after commit both files are re-read and must equal the staged payload
        and each other byte for byte;
      * ANY failure restores the exact prior bytes of BOTH targets, or raises
        loudly naming the restore failure (RULE P7 - no silent fallback).
    """

    DIGEST_MEN = "1" * 64
    DIGEST_ELVES = "2" * 64
    DIGEST_MUSIC = "3" * 64

    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        base = Path(self._temp.name)
        self.workspace = base / "content-packs"
        self.durable = base / "durable" / "content-packs"
        self.workspace.mkdir(parents=True)
        self.durable.mkdir(parents=True)
        self.entries: list[str] = []
        for pack_id, digest in (
            ("rotwk-men-vslice", self.DIGEST_MEN),
            ("rotwk-elves-vslice", self.DIGEST_ELVES),
            ("rotwk-music-vslice", self.DIGEST_MUSIC),
        ):
            self._make_bundle(self.workspace, pack_id, digest)
            self._make_bundle(self.durable, pack_id, digest)
            self.entries.append(f"{pack_id}/{digest}")
        self.active = self.entries[0]
        self.supplements = self.entries[1:]
        prior = {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": self.active,
        }
        self.prior_workspace = _write_selection(self.workspace, prior)
        self.prior_durable = _write_selection(self.durable, prior)

    @staticmethod
    def _make_bundle(root: Path, pack_id: str, digest: str) -> None:
        write_json_atomic(
            root / pack_id / digest / "pack.json",
            {
                "schema": "openbfme.content-pack",
                "schemaVersion": 0,
                "id": pack_id,
                "priority": 900,
                "dataPolicy": {"externalPathsAllowed": False},
                "files": {},
            },
        )

    def _workspace_bytes(self) -> bytes:
        return (self.workspace / "selection.json").read_bytes()

    def _durable_bytes(self) -> bytes:
        return (self.durable / "selection.json").read_bytes()

    def _assert_rolled_back(self) -> None:
        self.assertEqual(self._workspace_bytes(), self.prior_workspace)
        self.assertEqual(self._durable_bytes(), self.prior_durable)

    def _apply(self, **kwargs):
        from openbfme_importer.pipeline import apply_selection_transaction

        return apply_selection_transaction(
            self.workspace,
            kwargs.pop("active_pack", self.active),
            kwargs.pop("supplemental_packs", self.supplements),
            durable_root=kwargs.pop("durable_root", self.durable),
            **kwargs,
        )

    def _error(self):
        from openbfme_importer.pipeline import SelectionTransactionError

        return SelectionTransactionError

    # -- happy path ------------------------------------------------------
    def test_happy_path_writes_both_targets_byte_identically(self) -> None:
        result = self._apply()
        self.assertTrue(result["changed"])
        self.assertTrue(result["verified"])
        self.assertEqual(self._workspace_bytes(), self._durable_bytes())
        document = json.loads(self._workspace_bytes().decode("utf-8"))
        self.assertEqual(document["activePack"], self.active)
        self.assertEqual(document["supplementalPacks"], self.supplements)
        self.assertEqual(document["schema"], "openbfme.pack-selection")
        self.assertEqual(document["schemaVersion"], 0)
        expected = hashlib.sha256(self._workspace_bytes()).hexdigest()
        self.assertEqual(result["payloadSha256"], expected)
        for target in result["targets"]:
            self.assertEqual(target["sha256"], expected)

    def test_commit_is_one_swap_per_target_not_one_per_entry(self) -> None:
        result = self._apply()
        self.assertEqual(result["swaps"], len(result["targets"]))
        self.assertEqual(result["swaps"], 2)
        self.assertEqual(len(result["entries"]), 3)

    def test_workspace_only_transaction_leaves_durable_untouched(self) -> None:
        result = self._apply(durable_root=None)
        self.assertEqual(result["swaps"], 1)
        self.assertTrue(result["verified"])
        self.assertEqual(self._durable_bytes(), self.prior_durable)

    def test_no_temp_litter_survives_a_successful_transaction(self) -> None:
        self._apply()
        for root in (self.workspace, self.durable):
            self.assertEqual(
                sorted(item.name for item in root.iterdir() if item.is_file()),
                ["selection.json"],
            )

    # -- rollback --------------------------------------------------------
    def test_failure_after_first_target_write_restores_both(self) -> None:
        def hook(stage: str) -> None:
            if stage == "after-first-commit":
                raise RuntimeError("injected mid-commit failure")

        with self.assertRaises(self._error()) as caught:
            self._apply(_stage_hook=hook)
        self.assertIn("injected mid-commit failure", str(caught.exception))
        self.assertIn("rolled back", str(caught.exception).lower())
        self._assert_rolled_back()

    def test_durable_verification_failure_restores_both(self) -> None:
        durable_selection = self.durable / "selection.json"

        def hook(stage: str) -> None:
            if stage == "committed":
                durable_selection.write_bytes(b'{"schema": "corrupted"}\n')

        with self.assertRaises(self._error()) as caught:
            self._apply(_stage_hook=hook)
        self.assertIn("verification", str(caught.exception).lower())
        self._assert_rolled_back()

    def test_rollback_failure_is_loud_and_names_both_causes(self) -> None:
        def hook(stage: str) -> None:
            if stage == "after-first-commit":
                raise RuntimeError("injected mid-commit failure")
            if stage == "rollback-begin":
                raise RuntimeError("injected restore failure")

        with self.assertRaises(self._error()) as caught:
            self._apply(_stage_hook=hook)
        message = str(caught.exception)
        self.assertIn("injected mid-commit failure", message)
        self.assertIn("injected restore failure", message)
        self.assertIn("MANUAL RECOVERY", message)

    # -- fail-closed validation -----------------------------------------
    def test_refuses_non_content_addressed_pack(self) -> None:
        self._make_bundle(self.workspace, "rotwk-maps-private", "goal-official-72")
        self._make_bundle(self.durable, "rotwk-maps-private", "goal-official-72")
        with self.assertRaises(self._error()) as caught:
            self._apply(
                supplemental_packs=[
                    *self.supplements,
                    "rotwk-maps-private/goal-official-72",
                ]
            )
        self.assertIn("goal-official-72", str(caught.exception))
        self.assertIn("content-addressed", str(caught.exception))
        self._assert_rolled_back()

    def test_refuses_missing_pack(self) -> None:
        with self.assertRaises(self._error()) as caught:
            self._apply(
                supplemental_packs=[
                    *self.supplements,
                    f"rotwk-wild-vslice/{self.DIGEST_MEN}",
                ]
            )
        self.assertIn("rotwk-wild-vslice", str(caught.exception))
        self._assert_rolled_back()

    def test_refuses_pack_present_in_workspace_but_missing_from_durable(self) -> None:
        self._make_bundle(self.workspace, "rotwk-mordor-vslice", self.DIGEST_MEN)
        with self.assertRaises(self._error()) as caught:
            self._apply(
                supplemental_packs=[
                    *self.supplements,
                    f"rotwk-mordor-vslice/{self.DIGEST_MEN}",
                ]
            )
        message = str(caught.exception)
        self.assertIn("rotwk-mordor-vslice", message)
        self.assertIn("durable", message.lower())
        self._assert_rolled_back()

    def test_refuses_pack_id_mismatch(self) -> None:
        for root in (self.workspace, self.durable):
            self._make_bundle(root, "rotwk-wild-vslice", self.DIGEST_ELVES)
        write_json_atomic(
            self.workspace
            / "rotwk-wild-vslice"
            / self.DIGEST_ELVES
            / "pack.json",
            {"schema": "openbfme.content-pack", "schemaVersion": 0, "id": "bfme2-wild-vslice"},
        )
        with self.assertRaises(self._error()) as caught:
            self._apply(
                supplemental_packs=[
                    *self.supplements,
                    f"rotwk-wild-vslice/{self.DIGEST_ELVES}",
                ]
            )
        self.assertIn("bfme2-wild-vslice", str(caught.exception))
        self._assert_rolled_back()

    def test_refuses_traversal_entry(self) -> None:
        with self.assertRaises(self._error()) as caught:
            self._apply(supplemental_packs=[*self.supplements, "../outside/pack"])
        self.assertIn("..", str(caught.exception))
        self._assert_rolled_back()

    def test_refuses_duplicate_entries(self) -> None:
        with self.assertRaises(self._error()) as caught:
            self._apply(supplemental_packs=[*self.supplements, self.active])
        self.assertIn("duplicate", str(caught.exception).lower())
        self._assert_rolled_back()

    def test_first_restore_failure_still_restores_every_other_target(self) -> None:
        # The rollback used to stop at the first restore exception, which left
        # exactly the hybrid state (workspace new, durable old) this transaction
        # exists to prevent. Every target is restored INDEPENDENTLY and verified.
        def hook(stage: str) -> None:
            if stage == "after-first-commit":
                raise RuntimeError("injected mid-commit failure")
            if stage == "rollback-target:workspace":
                raise RuntimeError("injected workspace restore failure")

        with self.assertRaises(self._error()) as caught:
            self._apply(_stage_hook=hook)
        message = str(caught.exception)
        # The durable target must still have been restored byte-exactly.
        self.assertEqual(self._durable_bytes(), self.prior_durable)
        self.assertIn("workspace", message)
        self.assertIn("injected workspace restore failure", message)
        self.assertIn("MANUAL RECOVERY", message)
        # The exact prior bytes survive on disk for the operator to restore.
        preimages = sorted(self.workspace.glob("selection.json.*.preimage"))
        self.assertEqual(len(preimages), 1)
        self.assertEqual(preimages[0].read_bytes(), self.prior_workspace)
        self.assertIn(preimages[0].name, message)

    def test_restore_that_lands_wrong_bytes_is_detected_not_assumed(self) -> None:
        workspace_selection = self.workspace / "selection.json"

        def hook(stage: str) -> None:
            if stage == "after-first-commit":
                raise RuntimeError("injected mid-commit failure")
            if stage == "rollback-restored:workspace":
                # Something else clobbered the file after the restore wrote it.
                workspace_selection.write_bytes(b'{"schema": "clobbered"}\n')

        with self.assertRaises(self._error()) as caught:
            self._apply(_stage_hook=hook)
        message = str(caught.exception)
        self.assertIn("MANUAL RECOVERY", message)
        self.assertIn("workspace", message)
        self.assertEqual(self._durable_bytes(), self.prior_durable)

    def test_recovery_preimages_exist_before_commit_and_vanish_on_success(self) -> None:
        seen: dict[str, list[str]] = {}

        def hook(stage: str) -> None:
            if stage == "staged":
                seen["workspace"] = sorted(p.name for p in self.workspace.glob("*.preimage"))
                seen["durable"] = sorted(p.name for p in self.durable.glob("*.preimage"))

        self._apply(_stage_hook=hook)
        self.assertEqual(len(seen["workspace"]), 1)
        self.assertEqual(len(seen["durable"]), 1)
        for root in (self.workspace, self.durable):
            self.assertEqual(list(root.glob("*.preimage")), [])

    def test_refuses_while_another_process_holds_the_transaction_lock(self) -> None:
        from openbfme_importer.pipeline import selection_transaction_lock

        with selection_transaction_lock(self.workspace):
            with self.assertRaises(self._error()) as caught:
                self._apply()
        message = str(caught.exception)
        self.assertIn("lock", message.lower())
        self._assert_rolled_back()

    def test_lock_is_released_after_a_failed_transaction(self) -> None:
        def hook(stage: str) -> None:
            if stage == "validated":
                raise RuntimeError("injected pre-stage failure")

        with self.assertRaises(self._error()):
            self._apply(_stage_hook=hook)
        self.assertEqual(list(self.workspace.glob("*.lock")), [])
        # The next transaction must be able to take the lock again.
        self.assertTrue(self._apply()["verified"])

    def test_refuses_when_another_process_holds_the_durable_lock(self) -> None:
        # Two transactions with DIFFERENT workspaces can share one durable root.
        # Locking only the workspace lets them race the shared mirror.
        from openbfme_importer.pipeline import selection_transaction_lock

        with selection_transaction_lock(self.durable):
            with self.assertRaises(self._error()) as caught:
                self._apply()
        message = str(caught.exception)
        self.assertIn("lock", message.lower())
        self.assertIn(str(self.durable), message)
        self._assert_rolled_back()

    def test_locks_every_target_root_for_the_whole_transaction(self) -> None:
        from openbfme_importer.pipeline import SELECTION_TRANSACTION_LOCK

        held: dict[str, list[bool]] = {}

        def hook(stage: str) -> None:
            if stage in {"validated", "committed"}:
                held[stage] = [
                    (self.workspace / SELECTION_TRANSACTION_LOCK).is_file(),
                    (self.durable / SELECTION_TRANSACTION_LOCK).is_file(),
                ]

        self._apply(_stage_hook=hook)
        self.assertEqual(held["validated"], [True, True])
        self.assertEqual(held["committed"], [True, True])
        for root in (self.workspace, self.durable):
            self.assertFalse((root / SELECTION_TRANSACTION_LOCK).exists())

    def test_lock_roots_are_a_total_order_independent_of_argument_order(self) -> None:
        # Deadlock avoidance: two transactions that share roots must acquire
        # them in the SAME sequence no matter what order they were handed in.
        from openbfme_importer.pipeline import canonical_lock_roots

        forward = canonical_lock_roots([self.workspace, self.durable])
        backward = canonical_lock_roots([self.durable, self.workspace])
        self.assertEqual([str(p) for p in forward], [str(p) for p in backward])
        self.assertEqual(
            [str(p) for p in forward],
            sorted((str(self.workspace), str(self.durable)), key=str.casefold),
        )
        # The same root named twice (or in two cases) is locked once.
        self.assertEqual(len(canonical_lock_roots([self.workspace, self.workspace])), 1)

    def test_lock_root_normalization_follows_filesystem_case_semantics(self) -> None:
        # Unconditional casefolding conflates two DISTINCT roots on a
        # case-sensitive filesystem and silently leaves one of them unlocked.
        from openbfme_importer.pipeline import canonical_lock_roots

        from openbfme_importer.pipeline import _lock_root_key

        mixed = Path(self._temp.name) / "MiXeDcAsE"
        # The key must be the filesystem's own idea of path identity, not a
        # blanket casefold. os.path.normcase is that answer per platform.
        self.assertEqual(_lock_root_key(mixed), os.path.normcase(str(mixed.resolve())))

        upper = Path(self._temp.name) / "CaseRoot"
        lower = Path(self._temp.name) / "caseroot"
        upper.mkdir()
        one_directory = os.path.normcase(str(upper)) == os.path.normcase(str(lower))
        if not one_directory:
            lower.mkdir()
        roots = canonical_lock_roots([upper, lower])
        self.assertEqual(len(roots), 1 if one_directory else 2)

    def test_transaction_preserves_operator_metadata_in_both_targets(self) -> None:
        document = json.loads(self.prior_workspace.decode("utf-8"))
        document["operatorNote"] = "hand-tuned playtest stack"
        document["strictParityProfile"] = True
        document["someFutureKey"] = {"kept": [1, 2, 3]}
        self.prior_workspace = _write_selection(self.workspace, document)
        self.prior_durable = _write_selection(self.durable, document)
        result = self._apply()
        self.assertEqual(
            sorted(result["preservedFields"]),
            ["operatorNote", "someFutureKey", "strictParityProfile"],
        )
        for raw in (self._workspace_bytes(), self._durable_bytes()):
            after = json.loads(raw.decode("utf-8"))
            self.assertEqual(after["activePack"], self.active)
            self.assertEqual(after["supplementalPacks"], self.supplements)
            self.assertEqual(after["operatorNote"], "hand-tuned playtest stack")
            self.assertTrue(after["strictParityProfile"])
            self.assertEqual(after["someFutureKey"], {"kept": [1, 2, 3]})
        self.assertEqual(self._workspace_bytes(), self._durable_bytes())

    def test_recovery_preimage_creation_never_overwrites_an_earlier_one(self) -> None:
        from openbfme_importer.pipeline import _write_recovery_preimage

        path = self.workspace / "selection.json"
        first = _write_recovery_preimage(path, b"first prior bytes\n", 0)
        second = _write_recovery_preimage(path, b"second prior bytes\n", 0)
        self.assertNotEqual(first, second)
        self.assertEqual(first.read_bytes(), b"first prior bytes\n")
        self.assertEqual(second.read_bytes(), b"second prior bytes\n")

    def test_refuses_to_start_while_unresolved_preimages_exist(self) -> None:
        def hook(stage: str) -> None:
            if stage == "after-first-commit":
                raise RuntimeError("injected mid-commit failure")
            if stage == "rollback-target:workspace":
                raise RuntimeError("injected workspace restore failure")

        with self.assertRaises(self._error()):
            self._apply(_stage_hook=hook)
        preimages = sorted(self.workspace.glob("*.preimage"))
        self.assertEqual(len(preimages), 1)
        preserved = preimages[0].read_bytes()

        # A later transaction in the SAME process must not run over unresolved
        # recovery bytes - that is how the only copy of the prior document dies.
        with self.assertRaises(self._error()) as caught:
            self._apply()
        message = str(caught.exception)
        self.assertIn("preimage", message.lower())
        self.assertIn("unresolved", message.lower())
        self.assertEqual(sorted(self.workspace.glob("*.preimage")), preimages)
        self.assertEqual(preimages[0].read_bytes(), preserved)

    def test_publish_takes_the_same_content_root_lock(self) -> None:
        from openbfme_importer.pipeline import selection_transaction_lock

        base = Path(self._temp.name)
        source = base / "publish-source"
        make_audited_pack(source)
        content = base / "publish-content"
        content.mkdir()
        pipeline = ImportPipeline(None, base / "publish-state")
        with selection_transaction_lock(content):
            with self.assertRaises(self._error()) as caught:
                pipeline.publish_to_godot(source, content, select=False)
        self.assertIn("lock", str(caught.exception).lower())
        # Once released the publish runs and leaves no lock behind.
        publication = pipeline.publish_to_godot(source, content, select=False)
        self.assertTrue(Path(publication["published_pack"]).is_dir())
        self.assertEqual(list(content.glob("*.lock")), [])

    def test_refuses_while_a_cook_is_active(self) -> None:
        (self.workspace / ".cook-active").write_text(
            "rotwk-men-vslice cook in progress\n", encoding="utf-8"
        )
        with self.assertRaises(self._error()) as caught:
            self._apply()
        self.assertIn("cook", str(caught.exception).lower())
        self._assert_rolled_back()

    # -- CLI -------------------------------------------------------------
    def test_cli_apply_selection_transaction_round_trip(self) -> None:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = cli.main(
                [
                    "--json",
                    "apply-selection-transaction",
                    "--godot-content-root",
                    str(self.workspace),
                    "--durable-root",
                    str(self.durable),
                    "--active-pack",
                    self.active,
                    *[
                        argument
                        for entry in self.supplements
                        for argument in ("--supplemental-pack", entry)
                    ],
                ]
            )
        self.assertEqual(code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertTrue(payload["verified"])
        self.assertEqual(payload["swaps"], 2)
        self.assertEqual(self._workspace_bytes(), self._durable_bytes())

    def test_cli_refuses_mutable_pack_and_leaves_both_selections(self) -> None:
        self._make_bundle(self.workspace, "rotwk-maps-private", "goal-official-72")
        self._make_bundle(self.durable, "rotwk-maps-private", "goal-official-72")
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            code = cli.main(
                [
                    "apply-selection-transaction",
                    "--godot-content-root",
                    str(self.workspace),
                    "--durable-root",
                    str(self.durable),
                    "--active-pack",
                    self.active,
                    "--supplemental-pack",
                    "rotwk-maps-private/goal-official-72",
                ]
            )
        self.assertEqual(code, 1)
        self.assertIn("content-addressed", stderr.getvalue())
        self._assert_rolled_back()

    def test_cli_has_no_select_flag(self) -> None:
        # RULE P2: selection changes never flow through publish --select.
        parser = cli.build_parser()
        args = parser.parse_args(
            [
                "apply-selection-transaction",
                "--godot-content-root",
                str(self.workspace),
                "--active-pack",
                self.active,
            ]
        )
        self.assertFalse(hasattr(args, "select"))


if __name__ == "__main__":
    unittest.main()

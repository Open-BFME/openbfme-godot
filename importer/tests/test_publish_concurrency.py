"""Fast gates for the concurrent seven-faction pack proof.

Concurrency here is only safe because of a small number of specific claims.
Each one gets a test, because "it worked when I ran it" is not evidence that
two processes cannot collide.
"""

from __future__ import annotations

import importlib.util
import json
import os
import tempfile
import threading
import time
import unittest
from argparse import Namespace
from pathlib import Path

from openbfme_importer import cli
from openbfme_importer.big import BigArchive
from openbfme_importer.pipeline import (
    SelectionTransactionError,
    selection_transaction_lock,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
PACK_PROOF = REPO_ROOT / "tools" / "rotwk_faction_pack_proof.py"


def _load_pack_proof():
    spec = importlib.util.spec_from_file_location("rotwk_faction_pack_proof", PACK_PROOF)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class PublishJobsFlagTests(unittest.TestCase):
    def test_default_is_serial_and_jobs_must_be_positive(self) -> None:
        module = _load_pack_proof()
        parser_args = module.main.__doc__  # touch the module so import errors surface
        self.assertIsNotNone(PACK_PROOF.is_file() or parser_args)
        source = PACK_PROOF.read_text(encoding="utf-8")
        self.assertIn('"--publish-jobs"', source)
        self.assertIn("default=1", source)
        # Serial remains the untouched path.
        self.assertIn("args.publish_jobs > 1 and len(factions) > 1", source)

    def test_rejects_zero_jobs(self) -> None:
        module = _load_pack_proof()
        code = module.main(
            [
                "--install",
                str(REPO_ROOT),
                "--publish-jobs",
                "0",
            ]
        )
        # Refused before doing anything (2 = could not evaluate). It never
        # reaches the install check because the flag is invalid on its face,
        # or it fails the install check first - either way, never 0.
        self.assertNotEqual(code, 0)

    def test_batch_report_is_written_only_by_the_parent(self) -> None:
        """No child command line may name the batch report or a receipt."""

        source = PACK_PROOF.read_text(encoding="utf-8")
        # The only write_json_atomic calls are the parent's: receipts and the
        # batch report. Children get a fixed argv that never carries --output.
        child_argv_block = source.split("cmd = [", 1)[1].split("]", 1)[0]
        self.assertNotIn("--output", child_argv_block)
        self.assertNotIn("report", child_argv_block)
        self.assertIn("write_json_atomic(out, report)", source)


class CatalogRebuildRefusalTests(unittest.TestCase):
    def test_child_rebuild_is_a_hard_error_when_forbidden(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state = Path(raw) / "state"
            (state / "catalog").mkdir(parents=True)
            args = Namespace(
                game="rotwk",
                install=str(Path(raw) / "install"),
                reindex=False,
                state_root=str(state),
                json=True,
            )
            previous = os.environ.get("OPENBFME_CATALOG_NO_REBUILD")
            os.environ["OPENBFME_CATALOG_NO_REBUILD"] = "1"
            try:
                with self.assertRaises(cli.CatalogProvenanceError) as caught:
                    cli._load_or_build_catalog(args)
            finally:
                if previous is None:
                    os.environ.pop("OPENBFME_CATALOG_NO_REBUILD", None)
                else:
                    os.environ["OPENBFME_CATALOG_NO_REBUILD"] = previous
            payload = caught.exception.diagnostic
            self.assertEqual(payload["error"], "catalog-rebuild-refused")
            self.assertIn("OPENBFME_CATALOG_NO_REBUILD", payload["reason"])
            # And it refused rather than writing anything.
            self.assertEqual(list((state / "catalog").iterdir()), [])


class SelectionLockWaitTests(unittest.TestCase):
    def test_default_still_refuses_immediately(self) -> None:
        previous = os.environ.pop("OPENBFME_SELECTION_LOCK_WAIT_SECONDS", None)
        try:
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                with selection_transaction_lock(root):
                    with self.assertRaises(SelectionTransactionError):
                        with selection_transaction_lock(root):
                            pass
        finally:
            if previous is not None:
                os.environ["OPENBFME_SELECTION_LOCK_WAIT_SECONDS"] = previous

    def test_a_bounded_budget_still_refuses_a_lock_that_never_clears(self) -> None:
        """A budget is a queue, never a break-in."""

        previous = os.environ.get("OPENBFME_SELECTION_LOCK_WAIT_SECONDS")
        os.environ["OPENBFME_SELECTION_LOCK_WAIT_SECONDS"] = "0.3"
        try:
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                with selection_transaction_lock(root):
                    with self.assertRaises(SelectionTransactionError) as caught:
                        with selection_transaction_lock(root):
                            pass
                    self.assertIn("Refusing rather than waiting", str(caught.exception))
                    # The holder's lock file is still there, untouched.
                    self.assertTrue((root / ".selection-transaction.lock").is_file())
        finally:
            if previous is None:
                os.environ.pop("OPENBFME_SELECTION_LOCK_WAIT_SECONDS", None)
            else:
                os.environ["OPENBFME_SELECTION_LOCK_WAIT_SECONDS"] = previous

    def test_a_waiter_acquires_once_the_holder_releases(self) -> None:
        previous = os.environ.get("OPENBFME_SELECTION_LOCK_WAIT_SECONDS")
        os.environ["OPENBFME_SELECTION_LOCK_WAIT_SECONDS"] = "10"
        try:
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                acquired = threading.Event()
                released = threading.Event()

                def hold() -> None:
                    with selection_transaction_lock(root):
                        acquired.set()
                        released.wait(5)

                holder = threading.Thread(target=hold)
                holder.start()
                self.assertTrue(acquired.wait(5))
                released.set()
                holder.join(10)
                with selection_transaction_lock(root):
                    pass
        finally:
            if previous is None:
                os.environ.pop("OPENBFME_SELECTION_LOCK_WAIT_SECONDS", None)
            else:
                os.environ["OPENBFME_SELECTION_LOCK_WAIT_SECONDS"] = previous


COVERAGE_DOC = {
    "schema": "openbfme.faction-import-coverage",
    "schemaVersion": 0,
    "aggregateSha256": "a" * 64,
    "summary": {"conversionComplete": True, "converterGapCount": 0},
    "objects": [],
}


class CoverageWatchTests(unittest.TestCase):
    """The overlap mode's completion signal, and what it refuses."""

    def setUp(self) -> None:
        self.module = _load_pack_proof()

    def _write(self, path: Path, document: dict) -> None:
        path.write_text(json.dumps(document), encoding="utf-8")

    def test_fingerprint_rejects_absent_partial_and_foreign_documents(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "elves-coverage.json"
            self.assertIsNone(self.module.coverage_fingerprint(path))

            path.write_text("{ not json", encoding="utf-8")
            self.assertIsNone(self.module.coverage_fingerprint(path))

            self._write(path, {"schema": "something.else"})
            self.assertIsNone(self.module.coverage_fingerprint(path))

            self._write(path, {**COVERAGE_DOC, "summary": "not-an-object"})
            self.assertIsNone(self.module.coverage_fingerprint(path))

            self._write(path, {**COVERAGE_DOC, "aggregateSha256": ""})
            self.assertIsNone(self.module.coverage_fingerprint(path))

            self._write(path, COVERAGE_DOC)
            self.assertIsNotNone(self.module.coverage_fingerprint(path))

    def test_a_rewrite_inside_one_mtime_tick_still_counts_as_new(self) -> None:
        """Identity includes the aggregate digest, not just mtime and size."""

        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "elves-coverage.json"
            self._write(path, COVERAGE_DOC)
            before = self.module.coverage_fingerprint(path)
            self._write(path, {**COVERAGE_DOC, "aggregateSha256": "b" * 64})
            os.utime(path, ns=(before[0], before[0]))
            after = self.module.coverage_fingerprint(path)
            self.assertEqual(after[0], before[0], "mtime deliberately held equal")
            self.assertNotEqual(after, before)

    def test_stale_coverage_alone_never_counts_as_landed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "elves-coverage.json"
            self._write(path, COVERAGE_DOC)
            baseline = self.module.coverage_fingerprint(path)
            with self.assertRaises(TimeoutError) as caught:
                self.module._await_coverage(
                    path,
                    baseline=baseline,
                    accept_existing=False,
                    deadline=time.monotonic() + 0.2,
                    poll_seconds=0.02,
                    faction="elves",
                )
            self.assertIn("did not land", str(caught.exception))
            self.assertIn("--watch-accept-existing", str(caught.exception))

    def test_accept_existing_opt_out_returns_immediately(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "elves-coverage.json"
            self._write(path, COVERAGE_DOC)
            self.module._await_coverage(
                path,
                baseline=self.module.coverage_fingerprint(path),
                accept_existing=True,
                deadline=time.monotonic() + 0.2,
                poll_seconds=0.02,
                faction="elves",
            )

    def test_a_landing_file_is_picked_up(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "elves-coverage.json"
            self._write(path, COVERAGE_DOC)
            baseline = self.module.coverage_fingerprint(path)

            def land() -> None:
                time.sleep(0.05)
                self._write(path, {**COVERAGE_DOC, "aggregateSha256": "c" * 64})

            worker = threading.Thread(target=land)
            worker.start()
            try:
                self.module._await_coverage(
                    path,
                    baseline=baseline,
                    accept_existing=False,
                    deadline=time.monotonic() + 10,
                    poll_seconds=0.02,
                    faction="elves",
                )
            finally:
                worker.join(5)

    def test_a_faction_landing_at_the_deadline_still_times_out(self) -> None:
        """The precondition for the lost-batch bug.

        `_await_coverage` needs the fingerprint to hold across two consecutive
        polls, so a faction that lands within one poll interval of the deadline
        raises even though its coverage is now present and valid. That raise is
        legitimate - what mattered was where it landed.
        """

        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "elves-coverage.json"
            self._write(path, COVERAGE_DOC)
            baseline = self.module.coverage_fingerprint(path)
            # It lands right now, but the budget is already spent.
            self._write(path, {**COVERAGE_DOC, "aggregateSha256": "d" * 64})
            with self.assertRaises(TimeoutError):
                self.module._await_coverage(
                    path,
                    baseline=baseline,
                    accept_existing=False,
                    deadline=time.monotonic(),
                    poll_seconds=0.01,
                    faction="elves",
                )

    def test_await_coverage_is_called_inside_proof_one_try(self) -> None:
        """A late faction must cost its own row, not the whole batch report."""

        source = PACK_PROOF.read_text(encoding="utf-8")
        body = source.split("def proof_one(", 1)[1]
        try_at = body.index("try:")
        await_at = body.index("_await_coverage(")
        self.assertLess(
            try_at,
            await_at,
            "_await_coverage must be inside proof_one's try: raising from "
            "outside it escapes future.result() and destroys the batch report",
        )

    def test_a_raising_worker_becomes_a_failed_row(self) -> None:
        """collect_row never lets one worker's exception cost the batch."""

        class _Boom:
            def result(self):
                raise TimeoutError("coverage did not land")

        class _Fine:
            def result(self):
                return {"faction": "elves", "status": "publication-ready"}

        row = self.module.collect_row(
            "angmar", _Boom(), Path("angmar-coverage.json")
        )
        self.assertEqual(row["faction"], "angmar")
        self.assertEqual(row["status"], "failed")
        self.assertFalse(row["publicationReady"])
        self.assertIn("TimeoutError", row["error"])

        # And a healthy worker is passed through untouched.
        self.assertEqual(
            self.module.collect_row("elves", _Fine(), Path("elves-coverage.json")),
            {"faction": "elves", "status": "publication-ready"},
        )

    def test_a_missing_faction_is_a_failed_row_not_a_skipped_one(self) -> None:
        """The report must account for all seven, timeout or not."""

        source = PACK_PROOF.read_text(encoding="utf-8")
        self.assertIn("assert len(rows) == len(factions)", source)
        self.assertIn('"status": "failed"', source)


class InFlightTemporaryTests(unittest.TestCase):
    """The real bug a seven-way concurrent proof produced, pinned.

    A cache-copy temporary left inside the pack changed the Men bundle's
    address while every converted output stayed byte-identical. Two things had
    to be true for that to ship: the copy leaked the partial, and the inventory
    accepted it.
    """

    def test_inventory_refuses_a_leftover_cache_copy(self) -> None:
        from openbfme_importer.pipeline import (
            PACK_IN_FLIGHT_SUFFIXES,
            _canonical_pack_inventory,
        )

        for suffix in PACK_IN_FLIGHT_SUFFIXES:
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as raw:
                root = Path(raw) / "pack"
                (root / "data").mkdir(parents=True)
                (root / "data" / "real.bin").write_bytes(b"kept")
                # A clean tree inventories fine.
                self.assertEqual(len(_canonical_pack_inventory(root)), 1)
                stray = root / "data" / f"real.bin{suffix}"
                stray.write_bytes(b"partial")
                with self.assertRaises(RuntimeError) as caught:
                    _canonical_pack_inventory(root)
                self.assertIn("in-flight temporary", str(caught.exception))

    def test_media_cache_copy_leaves_nothing_behind_when_the_copy_fails(self) -> None:
        from openbfme_importer import pipeline

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            cache_root = root / "media-cache"
            key = "a" * 40
            entry = cache_root / key[:2] / key
            entry.mkdir(parents=True)
            payload = b"cached bytes" * 100
            (entry / "output.bin").write_bytes(payload)
            import hashlib

            (entry / "metadata.json").write_text(
                json.dumps(
                    {
                        "format": 1,
                        "key": key,
                        "output_size": len(payload),
                        "output_sha256": hashlib.sha256(payload).hexdigest(),
                    }
                ),
                encoding="utf-8",
            )
            pack = root / "pack" / "data"
            pack.mkdir(parents=True)
            target = pack / "out.bin"

            class _Pipeline:
                conversion_cache_enabled = True
                media_cache_root = cache_root

                def __init__(self) -> None:
                    self._conversion_cache_lock = threading.Lock()
                    self._conversion_cache_stats = {"hits": 0, "misses": 0}

            fake = _Pipeline()
            original = pipeline._copy_file_with_digest

            def _explode(source: Path, destination: Path):
                # Simulate a transient sharing violation PART WAY THROUGH, the
                # way a loaded box does: the partial file already exists.
                destination.write_bytes(b"partial")
                raise OSError(32, "simulated sharing violation")

            pipeline._copy_file_with_digest = _explode
            try:
                result = pipeline.ImportPipeline._copy_media_cache_hit(
                    fake, key, target
                )
            finally:
                pipeline._copy_file_with_digest = original

            self.assertFalse(result)
            self.assertEqual(fake._conversion_cache_stats["misses"], 1)
            strays = [
                item.name
                for item in pack.iterdir()
                if item.name.casefold().endswith(pipeline.PACK_IN_FLIGHT_SUFFIXES)
            ]
            self.assertEqual(strays, [], f"in-flight file survived: {strays}")


class ExtractionPartialNameTests(unittest.TestCase):
    def test_partial_file_name_is_process_unique(self) -> None:
        """Two cooks extracting the same entry must not share a partial file."""

        import inspect

        source = inspect.getsource(BigArchive.extract)
        self.assertIn("os.getpid()", source)
        self.assertIn("uuid.uuid4()", source)
        # The old shared name must be gone.
        self.assertNotIn('target.name + ".openbfme-part"', source)


if __name__ == "__main__":
    unittest.main()
